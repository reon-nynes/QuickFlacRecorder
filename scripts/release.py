#!/usr/bin/env python3
"""Validate release versions and publish verified portable packages."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
from urllib.error import HTTPError
from urllib.parse import quote
import urllib.request


PACKAGES = ("QuickFlacRecorder-windows-x64.zip", "QuickFlacRecorder-macos-universal.zip",
            "QuickFlacRecorder-linux-x64.tar.gz")


def sha256(stream):
    digest = hashlib.sha256()
    for block in iter(lambda: stream.read(1024 * 1024), b""):
        digest.update(block)
    return digest.hexdigest()


def package_hash(path):
    with path.open("rb") as stream:
        return sha256(stream)


def application_version(version):
    match = re.fullmatch(r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)"
                         r"(?:-([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?"
                         r"(?:\+([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?", version)
    if not match or any(int(part) > 65535 for part in match.groups()[:3]):
        raise ValueError("Expected a SemVer application version with numeric components from 0 to 65535")
    prerelease = match[4]
    if prerelease and any(part.isdigit() and len(part) > 1 and part.startswith("0")
                          for part in prerelease.split(".")):
        raise ValueError("Numeric prerelease identifiers cannot have leading zeros")
    return {"version": version, "numeric_version": ".".join(match.groups()[:3]),
            "prerelease": prerelease is not None}


def tag_version(tag):
    if not tag.startswith("v"):
        raise ValueError("Release tags must start with v")
    return application_version(tag[1:])


class GitHub:
    def __init__(self, repository):
        if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repository):
            raise ValueError("Expected an owner/repository name")
        self.base = f"https://api.github.com/repos/{repository}"
        self.token = os.environ["GH_TOKEN"]

    def request(self, method, path, body=None, package=None, download=False):
        url = path if path.startswith("https://") else self.base + path
        headers = {"Accept": "application/octet-stream" if download else "application/vnd.github+json",
                   "X-GitHub-Api-Version": "2022-11-28", "User-Agent": "QuickFlacRecorder-release"}
        if package:
            headers.update({"Content-Type": "application/octet-stream", "Content-Length": str(package.stat().st_size)})
        elif body is not None:
            headers["Content-Type"] = "application/json"
        data = package.open("rb") if package else json.dumps(body).encode() if body is not None else None
        try:
            request = urllib.request.Request(url, data=data, headers=headers, method=method)
            # Do not forward the token to a redirected asset-storage host.
            request.add_unredirected_header("Authorization", f"Bearer {self.token}")
            with urllib.request.urlopen(request, timeout=120) as response:
                return sha256(response) if download else json.load(response)
        finally:
            if package:
                data.close()

    def assets(self, release_id):
        assets = []
        page = 1
        while True:
            batch = self.request("GET", f"/releases/{release_id}/assets?per_page=100&page={page}")
            assets.extend(batch)
            if len(batch) < 100:
                return assets
            page += 1

    def release(self, tag):
        try:
            return self.request("GET", f"/releases/tags/{quote(tag, safe='')}")
        except HTTPError as error:
            if error.code != 404:
                raise
        # The tag endpoint only finds published releases. Authenticated listing
        # also includes drafts, so interrupted publication can resume its draft.
        page = 1
        matches = []
        while True:
            batch = self.request("GET", f"/releases?per_page=100&page={page}")
            matches.extend(item for item in batch if item["tag_name"] == tag)
            if len(batch) < 100:
                break
            page += 1
        if len(matches) > 1:
            raise ValueError("Multiple releases already use this tag; resolve the ambiguity before rerunning")
        return matches[0] if matches else None

    def verify_tag(self, tag, commit):
        target = self.request("GET", f"/git/ref/tags/{quote(tag, safe='')}")["object"]
        for _ in range(10):
            if target["type"] != "tag":
                break
            target = self.request("GET", f"/git/tags/{target['sha']}")["object"]
        if target["type"] != "commit" or target["sha"] != commit:
            raise ValueError("The tag no longer identifies the workflow's source commit")


def write_manifest(args):
    version = tag_version(args.tag)["version"]
    if args.package.name not in PACKAGES:
        raise ValueError("Unexpected portable package name")
    root = Path(__file__).resolve().parents[1]
    commit = subprocess.check_output(["git", "-c", "safe.directory=", "-c", f"safe.directory={root.as_posix()}",
                                      "rev-parse", "HEAD"], cwd=root, text=True).strip()
    cache = (args.build / "CMakeCache.txt").read_text(encoding="utf-8")
    if commit != args.commit or f"QFR_VERSION:STRING={version}" not in cache.splitlines():
        raise ValueError("Package source or configured version differs from the release context")
    manifest = {"tag": args.tag, "version": version, "commit": commit, "run_id": args.run_id,
                "package": args.package.name, "sha256": package_hash(args.package)}
    args.package.with_name(args.package.name + ".json").write_text(json.dumps(manifest) + "\n", encoding="utf-8")


def release_packages(args):
    version = tag_version(args.tag)["version"]
    packages = {}
    # Validate the complete set before making any release mutations.
    for name in PACKAGES:
        matches = list(args.packages.rglob(name))
        if len(matches) != 1 or not matches[0].is_file() or matches[0].stat().st_size == 0:
            raise ValueError(f"Expected exactly one nonempty {name} from this workflow run")
        package = matches[0]
        manifest = json.loads(package.with_name(name + ".json").read_text(encoding="utf-8"))
        expected = {"tag": args.tag, "version": version, "commit": args.commit,
                    "run_id": args.run_id, "package": name, "sha256": package_hash(package)}
        if manifest != expected:
            raise ValueError(f"Package provenance or hash does not match this run/tag: {name}")
        packages[name] = (package, expected["sha256"])
    return packages


def verify_asset(api, asset, package, digest):
    if asset["state"] != "uploaded" or asset["size"] != package.stat().st_size \
            or api.request("GET", f"/releases/assets/{asset['id']}", download=True) != digest:
        raise ValueError(f"Published asset differs: {package.name}. Use a new version tag for changed downloads.")


def release_notes(args, packages):
    hashes = "\n".join(f"{digest}  {name}" for name, (_, digest) in packages.items())
    return ("Portable downloads for Windows 10/11 x64, macOS 12+ Intel/Apple Silicon, "
            "and Linux x64 with an Ubuntu 22.04 baseline.\n\n"
            f"Source commit: `{args.commit}`. Workflow run: {args.run_id}. "
            "Each package includes matching application/JUCE source, license notices, launch instructions, "
            "and application hashes.\n\n"
            "Windows and Linux downloads have no publisher signature. The macOS app has an ad-hoc "
            "signature, with no Developer ID or notarization. CI inspection and startup checks do not "
            "establish real microphone, permission, or device behavior on every platform.\n\n"
            f"Package SHA-256:\n```text\n{hashes}\n```\n")


def publish(args):
    version = tag_version(args.tag)
    packages = release_packages(args)
    api = GitHub(args.repository)
    api.verify_tag(args.tag, args.commit)
    existing = api.release(args.tag)
    if existing is None:
        existing = api.request("POST", "/releases", {"tag_name": args.tag, "target_commitish": args.commit,
            "name": f"QuickFlacRecorder {version['version']}", "body": release_notes(args, packages), "draft": True,
            "prerelease": version["prerelease"]})
    if existing["tag_name"] != args.tag or existing["prerelease"] != version["prerelease"]:
        raise ValueError("Existing release tag or prerelease status differs from the requested release")
    assets = api.assets(existing["id"])
    names = [asset["name"] for asset in assets]
    if len(names) != len(set(names)):
        raise ValueError("Existing release has duplicate asset names")
    by_name = {asset["name"]: asset for asset in assets}
    for name, (package, digest) in packages.items():
        if name in by_name:
            verify_asset(api, by_name[name], package, digest)
    upload_url = existing["upload_url"].split("{", 1)[0]
    for name, (package, _) in packages.items():
        if name not in by_name:
            api.request("POST", f"{upload_url}?name={quote(name, safe='')}", package=package)
    final_assets = {asset["name"]: asset for asset in api.assets(existing["id"])}
    for name, (package, digest) in packages.items():
        if name not in final_assets:
            raise ValueError(f"Release upload did not produce {name}")
        verify_asset(api, final_assets[name], package, digest)
    api.verify_tag(args.tag, args.commit)
    if existing["draft"]:
        api.request("PATCH", f"/releases/{existing['id']}", {"draft": False, "body": release_notes(args, packages)})
    print(f"Published verified downloads for {args.tag}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    version = commands.add_parser("version")
    source = version.add_mutually_exclusive_group(required=True)
    source.add_argument("--tag")
    source.add_argument("--version")
    version.add_argument("--github-output", type=Path)
    manifest = commands.add_parser("manifest")
    publication = commands.add_parser("publish")
    for command in [manifest, publication]:
        command.add_argument("--tag", required=True)
        command.add_argument("--commit", required=True)
        command.add_argument("--run-id", required=True)
    manifest.add_argument("--package", type=Path, required=True)
    manifest.add_argument("--build", type=Path, required=True)
    publication.add_argument("--repository", required=True)
    publication.add_argument("--packages", type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.command == "manifest":
            write_manifest(args)
            return
        if args.command == "publish":
            publish(args)
            return
        result = tag_version(args.tag) if args.tag is not None else application_version(args.version)
        print(json.dumps(result))
        if args.github_output:
            with args.github_output.open("a", encoding="utf-8") as output:
                for key, value in result.items():
                    output.write(f"{key}={str(value).lower() if isinstance(value, bool) else value}\n")
    except (ValueError, OSError, KeyError) as error:
        parser.exit(1, f"Release failed: {error}\n")


if __name__ == "__main__":
    main()
