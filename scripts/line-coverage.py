#!/usr/bin/env python3
"""Which lines of CowTerm's own code ran, from a build made with
-DCOWTERM_LINE_COVERAGE=ON (just install-coverage). Each instrumented program
adds its counts every minute and at exit to <data folder>/coverage/<id>.profraw.

  line-coverage.py report [--instance NAME] [--binary PATH]...
      Never-run files and functions, largest first, from this machine's counts
      and every host collected into build-coverage/hosts, and an HTML per-line
      view of each. --binary replaces the installed programs as what ran.
  line-coverage.py collect <user@host> [--instance NAME] [--binaries FOLDER]
      Copies a host's counts and the programs that wrote them here.
  line-coverage.py reset [<user@host>] [--instance NAME]
      Deletes the counts, here or on a host.
"""
import argparse
import json
import platform
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

REPOSITORY = Path(__file__).resolve().parent.parent
HOSTS_FOLDER = REPOSITORY / "build-coverage" / "hosts"
REPORT_FOLDER = REPOSITORY / "build-coverage" / "report"
LOCAL_SET_NAME = "this-machine"
CAVEAT = ("Code that never ran includes error handling and rare paths: "
          "these are candidates to look at, and you decide what goes.")
OWN_CODE = re.compile(r"/((?:Lib|Terminal|Daemon|Remote|LineCoverage)/.*)$")
NOT_OWN_CODE = re.compile(r"lockstep|_deps|/build[^/]*/|/iOS/|/Tests/|/Bench/|/Fuzz/")
FUNCTIONS_SHOWN = 60
INSTALLED_BINARIES = [
    "~/.local/libexec/cowterm/CowTerm.app/Contents/MacOS/CowTerm",
    "~/.local/libexec/cowterm/CowTerm.app/Contents/MacOS/CowTermDaemon",
    "~/.local/libexec/cowterm/CowTerm",
    "~/.local/libexec/cowterm/CowTermDaemon",
    "~/.local/libexec/cowterm-remote/ct",
    "~/.local/libexec/cowterm-remote/cowtermd",
]
REMOTE_BINARY_FOLDERS = "~/.local/libexec/cowterm/CowTerm.app/Contents/MacOS ~/.local/libexec/cowterm ~/.local/libexec/cowterm-remote"
REGION_START_LINE = 0
REGION_END_LINE = 2
REGION_FILE_INDEX = 5


def get_coverage_folder_name(instance):
    return f"coverage-{instance}" if instance else "coverage"


def get_local_coverage_folder(instance):
    if platform.system() == "Darwin":
        data = Path.home() / "Library/Application Support/pond/CowTerm"
    else:
        data = Path.home() / ".local/share/pond/CowTerm"
    return data / get_coverage_folder_name(instance)


def get_remote_coverage_folder(instance):
    folder = get_coverage_folder_name(instance)
    return ('"$( [ "$(uname)" = Darwin ] && echo "$HOME/Library/Application Support" '
            f'|| echo "$HOME/.local/share")/pond/CowTerm/{folder}"')


def run_llvm_tool(name, *arguments):
    prefix = ["xcrun"] if platform.system() == "Darwin" else []
    return subprocess.run(prefix + [name, *arguments], check=True,
                          capture_output=True, text=True).stdout


def has_coverage_counters(binary):
    return b"__llvm_covmap" in binary.read_bytes()


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


def describe_counts(set_name, counts_folder, counts, binaries):
    newest = time.localtime(max(path.stat().st_mtime for path in counts))
    programs = ", ".join(binary.name for binary in binaries)
    print(f"[coverage] {set_name}: {len(counts)} counts files in {counts_folder}, newest written "
          f"{time.strftime('%Y-%m-%d %H:%M:%S', newest)}, from {programs}")


def get_object_arguments(binaries):
    others = [argument for binary in binaries[1:] for argument in ("-object", str(binary))]
    return [str(binaries[0]), *others]


def export_coverage(set_name, counts_folder, binaries):
    counts = sorted(counts_folder.glob("*.profraw"))
    binaries = [binary for binary in binaries if binary.is_file() and has_coverage_counters(binary)]
    if not counts or not binaries:
        print(f"[coverage] {set_name}: {len(counts)} counts files, {len(binaries)} instrumented programs; skipped")
        return None
    describe_counts(set_name, counts_folder, counts, binaries)
    output = REPORT_FOLDER / set_name
    output.mkdir(parents=True, exist_ok=True)
    profile = output / "merged.profdata"
    run_llvm_tool("llvm-profdata", "merge", "-sparse", "-o", str(profile), *map(str, counts))
    objects = get_object_arguments(binaries)
    exported = json.loads(run_llvm_tool("llvm-cov", "export", "-instr-profile", str(profile),
                                        "-skip-expansions", "-skip-branches", *objects))["data"][0]
    write_line_view(output, profile, objects, exported)
    return exported


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


def add_files_to_totals(data, files):
    for entry in data["files"]:
        path = get_own_path(entry["filename"])
        if path is not None:
            lines = entry["summary"]["lines"]
            known = files.setdefault(path, {"lines": 0, "covered": 0})
            known["lines"] = max(known["lines"], lines["count"])
            known["covered"] = max(known["covered"], lines["covered"])


def add_functions_to_totals(data, functions):
    for function in data["functions"]:
        first_region = function["regions"][0]
        path = get_own_path(function["filenames"][first_region[REGION_FILE_INDEX]])
        if path is not None:
            name = function["name"].split(":", 1)[-1]
            lines = first_region[REGION_END_LINE] - first_region[REGION_START_LINE] + 1
            known = functions.setdefault((path, first_region[REGION_START_LINE], name),
                                         {"lines": lines, "count": 0})
            known["count"] += function["count"]


def print_unrun_files(files):
    unrun_files = sorted(((entry["lines"], path) for path, entry in files.items() if entry["covered"] == 0),
                         reverse=True)
    print(f"Files that never ran ({len(unrun_files)}, {sum(lines for lines, _ in unrun_files)} lines):")
    for lines, path in unrun_files:
        print(f"  {lines:6}  {path}")
    return {path for _, path in unrun_files}


def print_unrun_functions(functions, unrun_file_paths):
    unrun_functions = sorted(((entry["lines"], key) for key, entry in functions.items()
                              if entry["count"] == 0 and key[0] not in unrun_file_paths), reverse=True)
    names = demangle([key[2] for _, key in unrun_functions])
    listing = [f"  {lines:6}  {path}:{line}  {name}"
               for (lines, (path, line, _)), name in zip(unrun_functions, names)]
    full_list = REPORT_FOLDER / "never-run-functions.txt"
    full_list.write_text("\n".join(listing) + "\n")
    print(f"\nFunctions that never ran, in files that did ({len(listing)}, "
          f"{sum(lines for lines, _ in unrun_functions)} lines), largest first:")
    print("\n".join(listing[:FUNCTIONS_SHOWN]))
    if len(listing) > FUNCTIONS_SHOWN:
        print(f"  ... {len(listing) - FUNCTIONS_SHOWN} more in {full_list}")


def print_report(files, functions, set_names):
    total_lines = sum(entry["lines"] for entry in files.values())
    covered_lines = sum(entry["covered"] for entry in files.values())
    print(CAVEAT)
    print(f"From: {', '.join(set_names)}. {covered_lines} of {total_lines} lines of CowTerm's own code ran.\n")
    unrun_file_paths = print_unrun_files(files)
    print_unrun_functions(functions, unrun_file_paths)
    print("\nPer-line view:")
    for name in set_names:
        print(f"  {REPORT_FOLDER / name / 'lines' / 'index.html'}")


def find_coverage_sets(arguments):
    binaries = [Path(binary) for binary in arguments.binary] or \
               [Path(binary).expanduser() for binary in INSTALLED_BINARIES]
    sets = [(LOCAL_SET_NAME, get_local_coverage_folder(arguments.instance), binaries)]
    if HOSTS_FOLDER.is_dir():
        sets += [(host.name, host / "counts", sorted((host / "binaries").glob("*")))
                 for host in sorted(HOSTS_FOLDER.iterdir())]
    return sets


def report(arguments):
    files, functions, used = {}, {}, []
    for set_name, counts_folder, binaries in find_coverage_sets(arguments):
        data = export_coverage(set_name, counts_folder, binaries)
        if data is not None:
            add_files_to_totals(data, files)
            add_functions_to_totals(data, functions)
            used.append(set_name)
    if not used:
        sys.exit("[coverage] no counts yet: install with just install-coverage and use it for a while")
    print_report(files, functions, used)


def collect(arguments):
    destination = HOSTS_FOLDER / arguments.host.split("@")[-1]
    shutil.rmtree(destination, ignore_errors=True)
    (destination / "counts").mkdir(parents=True)
    (destination / "binaries").mkdir()
    folders = arguments.binaries or REMOTE_BINARY_FOLDERS
    remote = (f"cd {get_remote_coverage_folder(arguments.instance)} && "
              f"tar -c *.profraw -C / $(for folder in {folders}; do "
              "for name in CowTerm CowTermDaemon ct cowtermd; do "
              "[ -f $folder/$name ] && echo ${folder#/}/$name; done; done)")
    archive = subprocess.run(["ssh", "-o", "BatchMode=yes", arguments.host, remote],
                             check=True, capture_output=True).stdout
    subprocess.run(["tar", "-x", "-C", str(destination / "counts")], input=archive, check=True)
    for program in (destination / "counts").rglob("*"):
        if program.is_file() and program.suffix != ".profraw":
            program.rename(destination / "binaries" / program.name)
    print(f"[coverage] {destination}: {len(list((destination / 'counts').glob('*.profraw')))} counts files, "
          f"programs {', '.join(sorted(path.name for path in (destination / 'binaries').iterdir()))}")


def reset(arguments):
    if arguments.host:
        remote = f"rm -f {get_remote_coverage_folder(arguments.instance)}/*.profraw"
        subprocess.run(["ssh", "-o", "BatchMode=yes", arguments.host, remote], check=True)
        shutil.rmtree(HOSTS_FOLDER / arguments.host.split("@")[-1], ignore_errors=True)
    else:
        for counts in get_local_coverage_folder(arguments.instance).glob("*.profraw"):
            counts.unlink()
    print(f"[coverage] counts deleted {'on ' + arguments.host if arguments.host else 'here'}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    report_command = commands.add_parser("report")
    report_command.add_argument("--binary", action="append", default=[])
    collect_command = commands.add_parser("collect")
    collect_command.add_argument("host")
    collect_command.add_argument("--binaries")
    reset_command = commands.add_parser("reset")
    reset_command.add_argument("host", nargs="?")
    for command in (report_command, collect_command, reset_command):
        command.add_argument("--instance", default="")
    arguments = parser.parse_args()
    {"report": report, "collect": collect, "reset": reset}[arguments.command](arguments)


if __name__ == "__main__":
    main()
