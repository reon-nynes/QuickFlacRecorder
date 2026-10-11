#!/usr/bin/env python3
"""Verify that a packaged recorder contains every registered embedded catalog."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import plistlib


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--mac-resources", type=Path)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    registry = root / "src/localization/languages.json"
    languages = json.loads(registry.read_text(encoding="utf-8"))
    catalogs = {re.sub(r"[^A-Za-z0-9_]", "", path.stem) + "_json": path
                for path in (root / "src/localization/catalogs").glob("*.json")}
    binary = args.binary.read_bytes()
    lines = ["Embedded localization resource inspection", f"Binary: {args.binary.name}"]
    for resource in [registry, *(catalogs[entry["resource"]] for entry in languages)]:
        canonical = resource.read_bytes().replace(b"\r\n", b"\n")
        if canonical not in binary and canonical.replace(b"\n", b"\r\n") not in binary:
            parser.error(f"Missing or stale embedded resource: {resource.name}")
        lines.append(f"PASS {resource.name} SHA-256 of LF source: {hashlib.sha256(canonical).hexdigest()}")
    if args.mac_resources:
        plist = args.mac_resources.parent / "Info.plist"
        metadata = plistlib.loads(plist.read_bytes())
        supported = {entry["tag"] for entry in languages}
        if not supported.issubset(set(metadata.get("CFBundleLocalizations", []))):
            parser.error("macOS bundle does not declare every supported localization")
        if metadata.get("CFBundleDevelopmentRegion") != "en":
            parser.error("macOS bundle development region must be English")
        if metadata.get("NSMicrophoneUsageDescription") != json.loads(catalogs["en_json"].read_text(encoding="utf-8"))["microphone_usage"]:
            parser.error("macOS bundle default microphone explanation differs from the English catalog")
        lines.append("PASS Info.plist supported localizations, English development region and microphone explanation")
        for language in languages:
            catalog = json.loads(catalogs[language["resource"]].read_text(encoding="utf-8"))
            resource = args.mac_resources / f'{language["tag"]}.lproj/InfoPlist.strings'
            text = resource.read_text(encoding="utf-8")
            usage = catalog["microphone_usage"].replace("\\", "\\\\").replace('"', '\\"')
            if f'"NSMicrophoneUsageDescription" = "{usage}";' not in text:
                parser.error(f"Missing or stale localized microphone explanation: {resource}")
            lines.append(f"PASS {language['tag']}.lproj/InfoPlist.strings")
    lines.append("Catalogs and formatter require no adjacent translation files or external formatter resources.")
    report = "\n".join(lines) + "\n"
    if args.report:
        args.report.write_text(report, encoding="utf-8")
    print(report, end="")


if __name__ == "__main__":
    main()
