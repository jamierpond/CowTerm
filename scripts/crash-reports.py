#!/usr/bin/env python3
"""Lists the crash reports CowTerm left that nobody has marked fixed, on this
machine and on others over ssh, and marks one fixed once its fix has landed.

  scripts/crash-reports.py list [HOST ...]
  scripts/crash-reports.py mark-fixed HOST REPORT_ID

HOST is an ssh destination (jamiepond@tamby) or `local` for this machine; list
reads only this machine when given none. A report not yet fixed sits in
<app support>/<executable>/Crashes/pending/<id>/; marking it fixed moves it to
handled/<id>/, as Crashout's ReportDatabase::markReportHandled does. list
prints nothing when there are none, and says on stderr which hosts it could
not reach.
"""

import argparse
import concurrent.futures
import datetime
import json
import re
import subprocess
import sys

EXECUTABLE_NAMES = ["CowTerm", "CowTermDaemon"]
LOCAL_HOST = "local"
SSH_CONNECT_TIMEOUT_SECONDS = 3
HOST_TIMEOUT_SECONDS = 10
TOP_FRAME_COUNT = 12
REPORT_FOLDER_MARKER = "@@crash-report-folder "
REPORT_ID_PATTERN = re.compile(r"^[0-9a-f-]{36}$")

# The app support folder is ~/Library/Application Support on macOS and
# %APPDATA% on Windows, where git-bash needs it as a Unix path.
FIND_APP_SUPPORT_FOLDERS = r"""
roots="$HOME/Library/Application Support"
if [ -n "$APPDATA" ]; then
    if command -v cygpath >/dev/null 2>&1; then
        roots=$(cygpath -u "$APPDATA")
    else
        roots="$APPDATA"
    fi
fi
"""

LIST_PENDING_REPORTS = FIND_APP_SUPPORT_FOLDERS + r"""
for executable in {executables}; do
    for folder in "$roots/$executable/Crashes/pending"/*/; do
        [ -d "$folder" ] || continue
        printf '%s%s\n' '{marker}' "$folder"
        [ -f "$folder/report.json" ] && cat "$folder/report.json"
        printf '\n'
    done
done
"""

MOVE_REPORT_TO_HANDLED = FIND_APP_SUPPORT_FOLDERS + r"""
for executable in {executables}; do
    crashes="$roots/$executable/Crashes"
    if [ -d "$crashes/pending/{report_id}" ]; then
        mkdir -p "$crashes/handled" && mv "$crashes/pending/{report_id}" "$crashes/handled/" && echo "$crashes/handled/{report_id}"
        exit $?
    fi
done
exit 3
"""


def runShellScriptOnHost(host, script):
    if host == LOCAL_HOST:
        command = ["sh", "-s"]
    else:
        command = ["ssh", "-o", "BatchMode=yes",
                   "-o", f"ConnectTimeout={SSH_CONNECT_TIMEOUT_SECONDS}",
                   host, "sh -s"]

    return subprocess.run(command, input=script, capture_output=True, text=True,
                          timeout=HOST_TIMEOUT_SECONDS)


def getShellExecutableList():
    return " ".join(EXECUTABLE_NAMES)


def readPendingReportsOnHost(host):
    script = LIST_PENDING_REPORTS.format(executables=getShellExecutableList(),
                                         marker=REPORT_FOLDER_MARKER)
    result = runShellScriptOnHost(host, script)

    if result.returncode != 0:
        errorLines = result.stderr.strip().splitlines() or ["it printed no error"]
        raise RuntimeError(errorLines[-1])

    return parsePendingReportListing(host, result.stdout)


def parsePendingReportListing(host, listing):
    reports = []
    folder = None
    lines = []

    for line in listing.splitlines() + [REPORT_FOLDER_MARKER]:
        if line.startswith(REPORT_FOLDER_MARKER):
            if folder is not None:
                reports.append(makePendingReport(host, folder, "\n".join(lines)))
            folder = line[len(REPORT_FOLDER_MARKER):].rstrip("/")
            lines = []
        else:
            lines.append(line)

    return reports


def makePendingReport(host, folder, reportText):
    try:
        report = json.loads(reportText) if reportText.strip() else None
    except json.JSONDecodeError:
        report = None

    return {"host": host, "folder": folder, "report": report}


def findExecutableName(folder):
    for name in EXECUTABLE_NAMES:
        if f"/{name}/Crashes/" in folder:
            return name

    return "unknown executable"


def formatTimestamp(milliseconds):
    moment = datetime.datetime.fromtimestamp(milliseconds / 1000, tz=datetime.timezone.utc)
    return moment.strftime("%Y-%m-%d %H:%M:%S UTC")


def getReportTimestamp(pending):
    report = pending["report"] or {}
    return report.get("timestamp", 0)


def formatStackFrame(index, frame):
    module = frame.get("modulePath", "").replace("\\", "/").rsplit("/", 1)[-1]
    symbol = frame.get("symbol") or f"+{frame.get('offset', '?')}"
    return f"    #{index:<2} {module:<28} {symbol}"


def formatReportLines(executable, host, folder, report):
    metadata = report.get("metadata", {})
    version = metadata.get("release") or metadata.get("appVersion") or "?"
    message = report.get("message", "")
    thread = report.get("crashingThread", {})
    frames = thread.get("frames", [])[:TOP_FRAME_COUNT]

    lines = [f"{executable} {version} on {host} ({report.get('platform', '')}), "
             f"{formatTimestamp(report.get('timestamp', 0))}",
             f"  {report.get('kind', 'crash')}: {report.get('reason', '')}"
             + (f": {message}" if message else ""),
             f"  thread {thread.get('threadId', '?')}, top {len(frames)} frames:"]
    lines.extend(formatStackFrame(index, frame) for index, frame in enumerate(frames))
    lines.append(f"  report:   {folder}/report.json")
    lines.append(f"  minidump: {report.get('minidumpPath') or 'none was written'}")
    return lines


def formatPendingReport(pending):
    host = pending["host"]
    folder = pending["folder"]
    executable = findExecutableName(folder)
    reportId = folder.rsplit("/", 1)[-1]

    if pending["report"] is None:
        lines = [f"{executable} on {host}: crashed and left {folder} without a readable report.json"]
    else:
        lines = formatReportLines(executable, host, folder, pending["report"])

    lines.append(f"  mark fixed: scripts/crash-reports.py mark-fixed {host} {reportId}")
    return "\n".join(lines)


def listUnfixedCrashReports(hosts):
    pendingReports = []

    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, len(hosts))) as pool:
        readings = {pool.submit(readPendingReportsOnHost, host): host for host in hosts}

        for reading in concurrent.futures.as_completed(readings):
            try:
                pendingReports.extend(reading.result())
            except (RuntimeError, subprocess.TimeoutExpired, OSError) as error:
                print(f"crash-reports: could not read {readings[reading]}: {error}", file=sys.stderr)

    pendingReports.sort(key=getReportTimestamp)
    print("\n\n".join(formatPendingReport(pending) for pending in pendingReports), end="")

    if pendingReports:
        print()

    return 0


def markCrashReportFixed(host, reportId):
    if not REPORT_ID_PATTERN.match(reportId):
        print(f"crash-reports: {reportId} is not a report id (a lower-case UUID)", file=sys.stderr)
        return 2

    script = MOVE_REPORT_TO_HANDLED.format(executables=getShellExecutableList(), report_id=reportId)
    result = runShellScriptOnHost(host, script)

    if result.returncode != 0:
        print(f"crash-reports: {host} has no pending report {reportId}", file=sys.stderr)
        return 1

    print(f"Marked {reportId} fixed on {host}: moved to {result.stdout.strip()}")
    return 0


def parseArguments():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)

    listing = commands.add_parser("list", help="print every crash report not yet marked fixed")
    listing.add_argument("hosts", nargs="*", default=[LOCAL_HOST],
                         help="ssh destinations, or `local` for this machine (the default)")

    marking = commands.add_parser("mark-fixed",
                                  help="move a report from pending/ to handled/ on its host")
    marking.add_argument("host", help="the ssh destination the report is on, or `local`")
    marking.add_argument("report_id", help="the report's id, its folder name")

    return parser.parse_args()


def main():
    arguments = parseArguments()

    if arguments.command == "list":
        return listUnfixedCrashReports(arguments.hosts)

    return markCrashReportFixed(arguments.host, arguments.report_id)


if __name__ == "__main__":
    sys.exit(main())
