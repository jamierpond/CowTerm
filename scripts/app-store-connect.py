#!/usr/bin/env python3
# Usage: just ios-asc <command> [email] [--first --last] [--write]; without --write nothing is sent.
"""CowTerm for iPhone on App Store Connect: its builds, the TestFlight note and internal testers."""

import argparse
import atexit
import base64
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUNDLE_ID = "audio.pond.cowterm.mobile"
LOCALE = "en-US"
API = "https://api.appstoreconnect.apple.com"
PLATFORM = "IOS"
BETA_GROUP = "CowTerm"
WHAT_TO_TEST = ROOT / "Deploy/Apple-iOS/WhatToTest.txt"
TOKEN_LIFETIME_SECONDS = 15 * 60
TOKEN_RENEWAL_SECONDS = 10 * 60
WRITE = False
NEW_ID = "(new)"


def fail(message):
    print(f"error: {message}", file=sys.stderr)
    sys.exit(1)


def b64url(data):
    return base64.urlsafe_b64encode(data).rstrip(b"=").decode()


def der_to_raw(der):
    def read_int(at):
        if der[at] != 0x02:
            fail("unexpected ECDSA signature encoding")
        length = der[at + 1]
        value = der[at + 2:at + 2 + length]
        return value.lstrip(b"\0").rjust(32, b"\0"), at + 2 + length

    at = 3 if der[1] & 0x80 else 2
    r, at = read_int(at)
    s, _ = read_int(at)
    return r + s


def make_token():
    key_id = os.environ.get("COWTERM_ASC_KEY_ID")
    issuer = os.environ.get("COWTERM_ASC_ISSUER_ID")
    key = os.environ.get("COWTERM_ASC_KEY")
    if not key and os.environ.get("COWTERM_ASC_KEY_P8"):
        key = os.path.join(tempfile.mkdtemp(), f"AuthKey_{key_id}.p8")
        with open(key, "w") as f:
            f.write(os.environ["COWTERM_ASC_KEY_P8"] + "\n")
        os.chmod(key, 0o600)
        atexit.register(shutil.rmtree, os.path.dirname(key), ignore_errors=True)
    if not (key_id and issuer and key):
        fail("COWTERM_ASC_KEY_ID, COWTERM_ASC_ISSUER_ID and COWTERM_ASC_KEY must be set; "
             "run through `just ios-asc`")
    now = int(time.time())
    header = {"alg": "ES256", "kid": key_id, "typ": "JWT"}
    claims = {"iss": issuer, "iat": now, "exp": now + TOKEN_LIFETIME_SECONDS,
              "aud": "appstoreconnect-v1"}
    signing_input = (b64url(json.dumps(header).encode()) + "."
                     + b64url(json.dumps(claims).encode())).encode()
    der = subprocess.run(["openssl", "dgst", "-sha256", "-sign", key],
                         input=signing_input, capture_output=True, check=True).stdout
    return signing_input.decode() + "." + b64url(der_to_raw(der))


TOKEN = {"value": None, "made": 0}


def token():
    if time.time() - TOKEN["made"] > TOKEN_RENEWAL_SECONDS:
        TOKEN["value"] = make_token()
        TOKEN["made"] = time.time()
    return TOKEN["value"]


def api(method, path, body=None, params=None, missing_ok=False):
    if method != "GET" and not WRITE:
        print(f"  would {method} {path}"
              + (f" {json.dumps(body)}" if body else ""))
        return {"data": {"id": NEW_ID, "type": "", "attributes": {}}}
    if NEW_ID in path:
        return {"data": []}
    url = path if path.startswith("http") else API + path
    if params:
        url += "?" + urllib.parse.urlencode(params, safe="[],")
    request = urllib.request.Request(
        url, method=method,
        data=json.dumps(body).encode() if body is not None else None,
        headers={"Authorization": f"Bearer {token()}",
                 "Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(request) as response:
            text = response.read()
            return json.loads(text) if text else {}
    except urllib.error.HTTPError as error:
        if error.code == 404 and missing_ok:
            return {"data": None}
        detail = error.read().decode()
        try:
            detail = json.dumps(json.loads(detail), indent=2)
        except ValueError:
            pass
        fail(f"{method} {path} -> HTTP {error.code}\n{detail}")


def get_all(path, params=None):
    items = []
    result = api("GET", path, params=params)
    while True:
        data = result.get("data") or []
        items += data if isinstance(data, list) else [data]
        next_url = result.get("links", {}).get("next")
        if not next_url:
            return items
        result = api("GET", next_url)


def resource(type_, id_):
    return {"data": {"type": type_, "id": id_}}


def create(type_, attributes, relationships):
    body = {"data": {"type": type_, "attributes": attributes,
                     "relationships": relationships}}
    return api("POST", f"/v1/{type_}", body)["data"]


def update(type_, id_, attributes, current=None):
    if current is not None:
        attributes = {k: v for k, v in attributes.items() if current.get(k) != v}
    if not attributes:
        print(f"  {type_} {id_}: up to date")
        return
    print(f"  {type_} {id_}: set {', '.join(attributes)}")
    api("PATCH", f"/v1/{type_}/{id_}",
        {"data": {"type": type_, "id": id_, "attributes": attributes}})


def version_string():
    match = re.search(r"^project\(CowTerm VERSION ([0-9]+\.[0-9]+\.[0-9]+)",
                      (ROOT / "CMakeLists.txt").read_text(), re.M)
    return match.group(1)


def find_app():
    apps = get_all("/v1/apps", {"filter[bundleId]": BUNDLE_ID})
    return apps[0] if apps else None


def find_build(app_id, version, number):
    params = {"filter[app]": app_id,
              "filter[preReleaseVersion.platform]": PLATFORM,
              "filter[preReleaseVersion.version]": version,
              "filter[expired]": "false", "sort": "-uploadedDate", "limit": 1}
    if number:
        params["filter[version]"] = number
    builds = api("GET", "/v1/builds", params=params)["data"]
    return builds[0] if builds else None


def localization(path, locale=LOCALE):
    for item in get_all(path):
        if item["attributes"].get("locale") == locale:
            return item
    return None


def cmd_check(args, app):
    for bundle in get_all("/v1/bundleIds", {"filter[identifier]": BUNDLE_ID}):
        a = bundle["attributes"]
        print(f"bundle id {a['identifier']}: registered as '{a['name']}' "
              f"({a['platform']}, seed {a.get('seedId')})")
    if not app:
        print(f"no app record for {BUNDLE_ID} in App Store Connect yet")
        return
    print(f"app {app['id']}: {app['attributes'].get('name')} "
          f"({app['attributes'].get('bundleId')}, "
          f"primary locale {app['attributes'].get('primaryLocale')})")
    builds = api("GET", "/v1/builds", params={"filter[app]": app["id"],
                                              "sort": "-uploadedDate",
                                              "include": "preReleaseVersion",
                                              "limit": 20})
    pre = {i["id"]: i["attributes"] for i in builds.get("included", [])}
    if not builds["data"]:
        print("  no builds")
    for b in builds["data"]:
        a = b["attributes"]
        rel = b.get("relationships", {}).get("preReleaseVersion", {}).get("data")
        p = pre.get(rel["id"], {}) if rel else {}
        beta = api("GET", f"/v1/builds/{b['id']}/buildBetaDetail",
                   missing_ok=True).get("data") or {}
        print(f"  build {p.get('version', '?')} ({a['version']}): {a['processingState']}, "
              f"uploaded {a.get('uploadedDate')}, usesNonExemptEncryption "
              f"{a.get('usesNonExemptEncryption')}, TestFlight internal "
              f"{beta.get('attributes', {}).get('internalBuildState')}, id {b['id']}")
    for g in get_all("/v1/betaGroups", {"filter[app]": app["id"]}):
        ga = g["attributes"]
        testers = get_all(f"/v1/betaGroups/{g['id']}/betaTesters")
        print(f"  beta group {ga['name']} {g['id']}: "
              f"{'internal' if ga['isInternalGroup'] else 'external'}, "
              f"all builds {ga.get('hasAccessToAllBuilds')}, {len(testers)} tester(s)")


def ensure_internal_group(app):
    groups = get_all("/v1/betaGroups", {"filter[app]": app["id"],
                                        "filter[name]": BETA_GROUP})
    if groups:
        print(f"beta group {BETA_GROUP} {groups[0]['id']}: exists")
        return groups[0]
    print(f"beta group {BETA_GROUP}: creating (internal, all builds)")
    return create("betaGroups", {"name": BETA_GROUP, "isInternalGroup": True,
                                 "hasAccessToAllBuilds": True},
                  {"app": resource("apps", app["id"])})


def add_internal_tester(group, user):
    a = user["attributes"]
    testers = get_all(f"/v1/betaGroups/{group['id']}/betaTesters")
    if any(t["attributes"].get("email", "").lower() == a["username"].lower()
           for t in testers):
        print(f"  tester {a['username']}: already in the group")
        return
    print(f"  adding tester {a['firstName']} {a['lastName']} <{a['username']}>")
    create("betaTesters", {"email": a["username"], "firstName": a["firstName"],
                           "lastName": a["lastName"]},
           {"betaGroups": {"data": [{"type": "betaGroups", "id": group["id"]}]}})


def cmd_testflight(args, app):
    build = find_build(app["id"], args.version_string, args.number)
    if not build or build["attributes"]["processingState"] != "VALID":
        fail(f"no VALID build {args.version_string} {args.number or ''}; "
             "`just ios-asc check` shows its state")
    print(f"build {args.version_string} ({build['attributes']['version']}) {build['id']}:")
    wanted = {"whatsNew": WHAT_TO_TEST.read_text().strip()}
    loc = localization(f"/v1/builds/{build['id']}/betaBuildLocalizations")
    if loc:
        update("betaBuildLocalizations", loc["id"], wanted, loc["attributes"])
    else:
        print(f"  creating the what-to-test note ({LOCALE})")
        create("betaBuildLocalizations", {"locale": LOCALE, **wanted},
               {"build": resource("builds", build["id"])})
    ensure_internal_group(app)


def cmd_invite(args, app):
    if not args.email:
        fail("invite needs the tester's email")
    email = args.email.lower()
    users = [u for u in get_all("/v1/users", {"limit": 200})
             if u["attributes"]["username"].lower() == email]
    if users:
        print(f"team user {email}: exists")
        add_internal_tester(ensure_internal_group(app), users[0])
        return
    invitations = get_all("/v1/userInvitations", {"filter[email]": email})
    if invitations:
        a = invitations[0]["attributes"]
        print(f"team invitation for {email}: pending, expires "
              f"{a.get('expirationDate')}; rerun once it is accepted")
        return
    if not (args.first and args.last):
        fail("invite needs --first and --last for a new team member")
    print(f"team invitation for {args.first} {args.last} <{email}>: creating "
          "(CUSTOMER_SUPPORT, CowTerm only)")
    create("userInvitations",
           {"email": email, "firstName": args.first, "lastName": args.last,
            "roles": ["CUSTOMER_SUPPORT"], "allAppsVisible": False,
            "provisioningAllowed": False},
           {"visibleApps": {"data": [{"type": "apps", "id": app["id"]}]}})
    print(f"  rerun once it is accepted to add the tester to {BETA_GROUP}")


COMMANDS = {"check": cmd_check, "testflight": cmd_testflight, "invite": cmd_invite}


def main():
    global WRITE
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=COMMANDS)
    parser.add_argument("email", nargs="?", help="invite: the tester's Apple ID email")
    parser.add_argument("--first")
    parser.add_argument("--last")
    parser.add_argument("--write", action="store_true",
                        help="send writes; without it they are only printed")
    parser.add_argument("--version-string", default=version_string())
    parser.add_argument("--number", help="build number (CFBundleVersion)")
    args = parser.parse_args()
    WRITE = args.write
    if not WRITE and args.command != "check":
        print("dry run: writes are printed, not sent (--write to send)")
    app = find_app()
    if not app and args.command != "check":
        fail(f"no app record for {BUNDLE_ID} in App Store Connect yet")
    COMMANDS[args.command](args, app)


if __name__ == "__main__":
    main()
