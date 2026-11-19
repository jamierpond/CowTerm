#!/usr/bin/env python3
"""Which of CowTerm's own code ran, from builds made with
-DCOWTERM_LINE_COVERAGE=ON (just install-coverage). Each instrumented program
adds its counts every minute and at exit to <data folder>/coverage/<id>.profraw,
one file per build of each program. Counts only map to lines through the
programs that wrote them, so each build's programs are kept in
<data folder>/coverage/programs/<installed date>-<hash>/ before an install
replaces them.

  line-coverage.py report [--instance NAME]
      Never-run files and functions, largest first, combined over every kept
      build (keeping the installed one first) on this machine and every host collected into build-coverage/hosts,
      and an HTML per-line view of each place's newest build.
  line-coverage.py keep [<user@host>] [--instance NAME] [--programs FOLDER]
      Keeps a copy of the installed instrumented programs (or those in FOLDER).
  line-coverage.py collect <user@host> [--instance NAME]
      Keeps the host's installed programs there, then copies its counts and
      kept programs here.
  line-coverage.py reset [<user@host>] [--instance NAME]
      Deletes the counts and the kept programs, here or on a host.
"""
import argparse
import hashlib
import io
import json
import platform
import re
import shutil
import subprocess
import sys
import tarfile
import time
from pathlib import Path

REPOSITORY = Path(__file__).resolve().parent.parent
HOSTS_FOLDER = REPOSITORY / "build-coverage" / "hosts"
REPORT_FOLDER = REPOSITORY / "build-coverage" / "report"
LOCAL_SET_NAME = "this-machine"
PROGRAMS_FOLDER_NAME = "programs"
CAVEAT = ("Code that never ran includes error handling and rare paths: "
          "these are candidates to look at, and you decide what goes.")
OWN_CODE = re.compile(r"/((?:Lib|Terminal|Daemon|Remote|LineCoverage)/.*)$")
NOT_OWN_CODE = re.compile(r"lockstep|_deps|/build[^/]*/(?:Lib|Terminal|Daemon|Remote)/|/iOS/|/Tests/|/Bench/|/Fuzz/")
FUNCTIONS_SHOWN = 60
PROGRAM_NAMES = ["CowTerm", "CowTermDaemon", "ct", "cowtermd"]
INSTALLED_PROGRAM_FOLDERS = [
    "~/.local/libexec/cowterm/CowTerm.app/Contents/MacOS",
    "~/.local/libexec/cowterm",
    "~/.local/libexec/cowterm-remote",
]
BUILD_HASH_LENGTH = 12
BUILD_DATE_FORMAT = "%Y-%m-%d-%H%M%S"
SHOWN_DATE_FORMAT = "%Y-%m-%d %H:%M"
REGION_START_LINE = 0
REGION_END_LINE = 2
REGION_FILE_INDEX = 5


def get_coverage_folder(instance):
    if platform.system() == "Darwin":
        data = Path.home() / "Library/Application Support/pond/CowTerm"
    else:
        data = Path.home() / ".local/share/pond/CowTerm"
    return data / (f"coverage-{instance}" if instance else "coverage")


def run_on_host(host, *arguments):
    command = ["ssh", "-o", "BatchMode=yes", host, "python3", "-", *arguments]
    with open(__file__, "rb") as script:
        return subprocess.run(command, stdin=script, check=True, capture_output=True).stdout


def run_llvm_tool(name, *arguments):
    prefix = ["xcrun"] if platform.system() == "Darwin" else []
    return subprocess.run(prefix + [name, *arguments], check=True,
                          capture_output=True, text=True).stdout


def has_coverage_counters(program):
    return program.is_file() and b"__llvm_covmap" in program.read_bytes()


def find_instrumented_programs(folder):
    return [folder / name for name in PROGRAM_NAMES if has_coverage_counters(folder / name)]


def get_build_name(programs):
    digest = hashlib.sha256()
    for program in programs:
        digest.update(program.read_bytes())
    installed = time.localtime(programs[0].stat().st_mtime)
    return f"{time.strftime(BUILD_DATE_FORMAT, installed)}-{digest.hexdigest()[:BUILD_HASH_LENGTH]}"


def keep_programs(coverage_folder, folder):
    programs = find_instrumented_programs(folder)
    if not programs:
        return
    build_name = get_build_name(programs)
    kept = coverage_folder / PROGRAMS_FOLDER_NAME
    if any(path.name.endswith(build_name[-BUILD_HASH_LENGTH:]) for path in kept.glob("*")):
        return
    (kept / build_name).mkdir(parents=True)
    for program in programs:
        shutil.copy2(program, kept / build_name / program.name)
    print(f"[coverage] kept {', '.join(program.name for program in programs)} as {kept / build_name}")


def keep(arguments):
    if arguments.host:
        sys.stdout.buffer.write(run_on_host(arguments.host, "keep", "--instance", arguments.instance))
        return
    folders = [arguments.programs] if arguments.programs else INSTALLED_PROGRAM_FOLDERS
    for folder in folders:
        keep_programs(get_coverage_folder(arguments.instance), Path(folder).expanduser())


def demangle(names):
    filter_program = shutil.which("c++filt") or shutil.which("llvm-cxxfilt")
    if filter_program is None:
        return names
    output = subprocess.run([filter_program], input="\n".join(names), capture_output=True,
                            text=True).stdout
    return [shorten_standard_names(name) for name in output.splitlines()]


def shorten_standard_names(name):
    name = name.replace("std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>",
                        "std::string")
    name = name.replace("std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> >",
                        "std::string")
    return name.replace("std::__1::", "std::")


def get_object_arguments(programs):
    others = [argument for program in programs[1:] for argument in ("-object", str(program))]
    return [str(programs[0]), *others]


def describe_build(build):
    return time.strftime(SHOWN_DATE_FORMAT, time.strptime(build.name[:-BUILD_HASH_LENGTH - 1], BUILD_DATE_FORMAT))


def export_build(profile, build):
    objects = get_object_arguments(sorted(build.iterdir()))
    exported = json.loads(run_llvm_tool("llvm-cov", "export", "-instr-profile", str(profile),
                                        "-skip-expansions", "-skip-branches", *objects))
    return exported["data"][0], objects


def write_line_view(output, profile, objects, exported):
    own = [entry["filename"] for entry in exported["files"] if get_own_path(entry["filename"])]
    if not own:
        return
    arguments = ["show", "-format=html", "-output-dir", str(output / "lines"),
                 "-instr-profile", str(profile), *objects]
    if not Path(own[0]).exists():
        source_root = own[0][: -len(get_own_path(own[0]))]
        arguments.append(f"-path-equivalence={source_root.rstrip('/')},{REPOSITORY}")
    try:
        run_llvm_tool("llvm-cov", *arguments, *own)
    except subprocess.CalledProcessError as error:
        print(f"[coverage] no per-line view for {output.name}: {error.stderr.strip()[:200]}")


def get_own_path(filename):
    match = OWN_CODE.search(filename)
    if match is None or NOT_OWN_CODE.search(filename):
        return None
    return match.group(1)


def read_build_files(data):
    files = {}
    for entry in data["files"]:
        path = get_own_path(entry["filename"])
        if path is not None:
            lines = entry["summary"]["lines"]
            files[path] = {"lines": lines["count"], "ran": lines["covered"] > 0}
    return files


def read_build_functions(data):
    functions = {}
    for function in data["functions"]:
        first_region = function["regions"][0]
        path = get_own_path(function["filenames"][first_region[REGION_FILE_INDEX]])
        if path is not None:
            key = (path, function["name"].split(":", 1)[-1])
            known = functions.setdefault(key, {"line": first_region[REGION_START_LINE], "ran": False,
                                               "lines": first_region[REGION_END_LINE] - first_region[REGION_START_LINE] + 1})
            known["ran"] = known["ran"] or function["count"] > 0
    return functions


def combine_builds(builds_newest_first):
    newest_files, newest_functions = builds_newest_first[0]
    for older_files, older_functions in builds_newest_first[1:]:
        for path, entry in newest_files.items():
            entry["ran"] = entry["ran"] or older_files.get(path, {}).get("ran", False)
        for key, entry in newest_functions.items():
            entry["ran"] = entry["ran"] or older_functions.get(key, {}).get("ran", False)
    return newest_files, newest_functions


def report_place(place_name, coverage_folder):
    counts = sorted(coverage_folder.glob("*.profraw"))
    builds = sorted((coverage_folder / PROGRAMS_FOLDER_NAME).glob("*"), reverse=True)
    if not counts or not builds:
        print(f"[coverage] {place_name}: {len(counts)} counts files, {len(builds)} kept builds; skipped")
        return None
    output = REPORT_FOLDER / place_name
    output.mkdir(parents=True, exist_ok=True)
    profile = output / "merged.profdata"
    run_llvm_tool("llvm-profdata", "merge", "-sparse", "-o", str(profile), *map(str, counts))
    per_build = []
    for build in builds:
        data, objects = export_build(profile, build)
        if build == builds[0]:
            write_line_view(output, profile, objects, data)
        per_build.append((read_build_files(data), read_build_functions(data)))
    print(f"[coverage] {place_name}: {len(builds)} builds installed {', '.join(map(describe_build, reversed(builds)))}; "
          f"lines and names are the newest build's")
    return combine_builds(per_build)


def combine_places(places):
    files, functions = {}, {}
    for place_files, place_functions in places:
        for path, entry in place_files.items():
            known = files.setdefault(path, dict(entry))
            known["ran"] = known["ran"] or entry["ran"]
        for key, entry in place_functions.items():
            known = functions.setdefault(key, dict(entry))
            known["ran"] = known["ran"] or entry["ran"]
    return files, functions


def print_unrun_files(files):
    unrun_files = sorted(((entry["lines"], path) for path, entry in files.items() if not entry["ran"]),
                         reverse=True)
    print(f"Files that never ran ({len(unrun_files)}, {sum(lines for lines, _ in unrun_files)} lines):")
    for lines, path in unrun_files:
        print(f"  {lines:6}  {path}")
    return {path for _, path in unrun_files}


def print_unrun_functions(functions, unrun_file_paths):
    unrun_functions = sorted(((entry["lines"], key, entry["line"]) for key, entry in functions.items()
                              if not entry["ran"] and key[0] not in unrun_file_paths), reverse=True)
    names = demangle([key[1] for _, key, _ in unrun_functions])
    listing = [f"  {lines:6}  {key[0]}:{line}  {name}"
               for (lines, key, line), name in zip(unrun_functions, names)]
    full_list = REPORT_FOLDER / "never-run-functions.txt"
    full_list.write_text("\n".join(listing) + "\n")
    print(f"\nFunctions that never ran, in files that did ({len(listing)}, "
          f"{sum(lines for lines, _, _ in unrun_functions)} lines), largest first:")
    print("\n".join(listing[:FUNCTIONS_SHOWN]))
    if len(listing) > FUNCTIONS_SHOWN:
        print(f"  ... {len(listing) - FUNCTIONS_SHOWN} more in {full_list}")


def print_report(files, functions, place_names):
    ran_functions = sum(entry["ran"] for entry in functions.values())
    print(CAVEAT)
    print(f"From: {', '.join(place_names)}. {ran_functions} of {len(functions)} functions "
          f"in CowTerm's own code ran.\n")
    unrun_file_paths = print_unrun_files(files)
    print_unrun_functions(functions, unrun_file_paths)
    print("\nPer-line view of each place's newest build:")
    for name in place_names:
        print(f"  {REPORT_FOLDER / name / 'lines' / 'index.html'}")


def find_places(arguments):
    if not arguments.instance:
        keep(argparse.Namespace(host=None, instance="", programs=None))
    places = [(LOCAL_SET_NAME, get_coverage_folder(arguments.instance))]
    if HOSTS_FOLDER.is_dir():
        places += [(host.name, host) for host in sorted(HOSTS_FOLDER.iterdir())]
    return places


def report(arguments):
    places, used = [], []
    for place_name, coverage_folder in find_places(arguments):
        combined = report_place(place_name, coverage_folder)
        if combined is not None:
            places.append(combined)
            used.append(place_name)
    if not used:
        sys.exit("[coverage] no counts yet: install with just install-coverage and use it for a while")
    print_report(*combine_places(places), used)


def pack(arguments):
    folder = get_coverage_folder(arguments.instance)
    with tarfile.open(fileobj=sys.stdout.buffer, mode="w|") as archive:
        for path in [*folder.glob("*.profraw"), folder / PROGRAMS_FOLDER_NAME]:
            if path.exists():
                archive.add(path, arcname=path.name)


def collect(arguments):
    sys.stdout.buffer.write(run_on_host(arguments.host, "keep", "--instance", arguments.instance))
    packed = run_on_host(arguments.host, "pack", "--instance", arguments.instance)
    destination = HOSTS_FOLDER / arguments.host.split("@")[-1]
    shutil.rmtree(destination, ignore_errors=True)
    destination.mkdir(parents=True)
    with tarfile.open(fileobj=io.BytesIO(packed), mode="r:") as archive:
        archive.extractall(destination, filter="data")
    builds = list((destination / PROGRAMS_FOLDER_NAME).glob("*"))
    print(f"[coverage] {destination}: {len(list(destination.glob('*.profraw')))} counts files, {len(builds)} builds")


def reset(arguments):
    if arguments.host:
        run_on_host(arguments.host, "reset", "--instance", arguments.instance)
        shutil.rmtree(HOSTS_FOLDER / arguments.host.split("@")[-1], ignore_errors=True)
    else:
        folder = get_coverage_folder(arguments.instance)
        for counts in folder.glob("*.profraw"):
            counts.unlink()
        shutil.rmtree(folder / PROGRAMS_FOLDER_NAME, ignore_errors=True)
    print(f"[coverage] counts and kept programs deleted {'on ' + arguments.host if arguments.host else 'here'}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    report_command = commands.add_parser("report")
    keep_command = commands.add_parser("keep")
    keep_command.add_argument("host", nargs="?")
    keep_command.add_argument("--programs")
    collect_command = commands.add_parser("collect")
    collect_command.add_argument("host")
    pack_command = commands.add_parser("pack")
    reset_command = commands.add_parser("reset")
    reset_command.add_argument("host", nargs="?")
    for command in (report_command, keep_command, collect_command, pack_command, reset_command):
        command.add_argument("--instance", default="")
    arguments = parser.parse_args()
    handlers = {"report": report, "keep": keep, "collect": collect, "pack": pack, "reset": reset}
    handlers[arguments.command](arguments)


if __name__ == "__main__":
    main()
