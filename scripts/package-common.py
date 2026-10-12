#!/usr/bin/env python3
"""Prepare notices and corresponding application/JUCE source for one preview."""

import argparse
import io
from pathlib import Path
import re
import shutil
import subprocess
import zipfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--juce-source", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    juce = args.juce_source.resolve()
    if not (juce / "LICENSE.md").is_file() or not (juce / "modules").is_dir():
        parser.error(f"JUCE source not found at {juce}")
    version_header = (juce / "modules/juce_core/system/juce_StandardHeader.h").read_text(encoding="utf-8")
    version = tuple(int(re.search(r"#define\s+" + macro + r"\s+(\d+)", version_header).group(1))
                    for macro in ["JUCE_MAJOR_VERSION", "JUCE_MINOR_VERSION", "JUCE_BUILDNUMBER"])
    if version != (9, 0, 3):
        parser.error(f"Expected JUCE 9.0.3 source, found {version}")

    def git(*arguments, binary=False, allowed_codes=(0,)):
        # Container-mounted workspaces can have a different owner. Trust only this
        # source root for this process; do not modify global Git configuration.
        command = ["git", "-c", "safe.directory=", "-c", f"safe.directory={root.as_posix()}", *arguments]
        result = subprocess.run(command, cwd=root, capture_output=True, text=not binary)
        if result.returncode not in allowed_codes:
            detail = result.stderr.decode(errors="replace") if binary else result.stderr
            parser.error(f"Git {' '.join(arguments)} failed in {root} (exit {result.returncode}): {detail.strip()}")
        return result

    top_level = Path(git("rev-parse", "--show-toplevel").stdout.strip()).resolve()
    if top_level != root:
        parser.error(f"Expected source repository root {root}, found {top_level}")
    # A committed source archive must match the application source used by the build.
    if git("diff", "--quiet", "HEAD", allowed_codes=(0, 1)).returncode == 1:
        parser.error("Commit tracked changes before packaging a corresponding-source preview")
    untracked = git("ls-files", "--others", "--exclude-standard").stdout.strip()
    if untracked:
        parser.error("Commit or ignore untracked files before packaging: " + untracked.replace("\n", ", "))
    destination = args.destination.resolve()
    destination.mkdir(parents=True, exist_ok=True)
    for source, name in [(root / "LICENSE", "LICENSE"), (root / "NOTICE.md", "NOTICE.md"),
                         (root / "docs/preview.md", "LAUNCH.md")]:
        shutil.copy2(source, destination / name)
    notices = destination / "licenses"
    notices.mkdir(exist_ok=True)
    shutil.copytree(root / "licenses", notices, dirs_exist_ok=True)
    shutil.copy2(juce / "LICENSE.md", notices / "JUCE-LICENSE.md")
    # Preserve the original hierarchy and the full texts supplied by JUCE.
    for source in (juce / "modules").rglob("*"):
        if source.is_file() and (re.search(r"licen[cs]e|copying|copyright", source.name, re.I)
                                 or source.name == "JUCE_UPSTREAM.txt"
                                 or source.relative_to(juce).as_posix() == "modules/juce_graphics/image_formats/jpglib/README"):
            target = notices / "JUCE" / source.relative_to(juce)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
    commit = git("rev-parse", "HEAD").stdout.strip()
    # Keep canonical source bytes, including LF shell scripts, on Windows too.
    archive = git("-c", "core.autocrlf=false", "archive", "--format=zip", "HEAD", binary=True).stdout
    source_name = "QuickFlacRecorder-source.zip"
    with zipfile.ZipFile(destination / source_name, "w", zipfile.ZIP_DEFLATED) as output:
        with zipfile.ZipFile(io.BytesIO(archive)) as application:
            for entry in application.infolist():
                output.writestr("QuickFlacRecorder/" + entry.filename, application.read(entry))
        for source in juce.rglob("*"):
            relative = source.relative_to(juce)
            if source.is_file() and not any(part in {".git", "build", ".DS_Store"} for part in relative.parts):
                output.write(source, "QuickFlacRecorder/vendor/JUCE/" + relative.as_posix())
    (destination / "SOURCE.txt").write_text(
        f"QuickFlacRecorder commit: {commit}\n"
        "JUCE version: 9.0.3\n"
        f"Corresponding application and JUCE source: {source_name}\n"
        "Source, build/packaging scripts, and notices are in the source archive.\n"
        "Build without fetching JUCE by adding this CMake configure argument:\n"
        "  -DFETCHCONTENT_SOURCE_DIR_JUCE=<absolute path to vendor/JUCE>\n"
        "See docs/build.md in the extracted source for platform prerequisites.\n"
        "For bundled Linux libraries, also see licenses/Ubuntu and linux-library-sources.txt.\n",
        encoding="utf-8")


if __name__ == "__main__":
    main()
