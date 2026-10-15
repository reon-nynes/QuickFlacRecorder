# Publish portable releases

Pushing a valid `v*` version tag approves publication of its source as a public [GitHub Release](https://github.com/reon-nynes/QuickFlacRecorder/releases). This policy was accepted in [issue #15](https://github.com/reon-nynes/QuickFlacRecorder/issues/15) and supersedes the initial requirement for a separate working-build review before publication. Tagging is the owner's approval step.

The [build, package, and release workflow](../.github/workflows/preview.yml) checks out the tag event's exact commit. It validates the version, runs release-tooling checks, builds and tests Windows, macOS, and Linux, inspects and packages the applications, and checks the Linux AppImage in a fresh Ubuntu 22.04 runtime container. The publishing job requires every check to succeed. A failed or skipped platform job cannot produce a newly public release.

The release contains these three downloads:

| Download | Portable application | Supported target |
| --- | --- | --- |
| `QuickFlacRecorder-windows-x64.zip` | `QuickFlacRecorder.exe` | Windows 10/11 x64 |
| `QuickFlacRecorder-macos-universal.zip` | `QuickFlacRecorder.app` | macOS 12+, Intel and Apple Silicon |
| `QuickFlacRecorder-linux-x64.tar.gz` | `QuickFlacRecorder-x86_64.AppImage` | Linux x64, Ubuntu 22.04+ baseline |

These public downloads do not require GitHub sign-in and do not have the Actions artifacts' 30-day retention limit. Each package retains the existing license notices, corresponding application/JUCE source, `SOURCE.txt`, application SHA-256 record, and inspection evidence. Release notes also record archive SHA-256 hashes, the source commit, and workflow run. Follow the [launch instructions](preview.md) for the platform's unsigned-application requirements.

## Version tags and application metadata

Use a `v` followed by a SemVer version, such as `v1.2.3` or `v1.2.3-rc.1`. Optional SemVer build metadata is accepted, such as `v1.2.3-rc.1+build.7`. Numeric core components must fit Windows' 16-bit version fields, from 0 through 65535. Leading zeros in core components or numeric prerelease identifiers are invalid. The workflow triggers on every `v*` tag so malformed tags such as `vtest` fail validation before platform builds or publication.

| Tag | App version and Windows version text | Numeric Windows fields and macOS bundle versions | GitHub release type |
| --- | --- | --- | --- |
| `v1.2.3` | `1.2.3` | `1.2.3` | Release |
| `v1.2.3-rc.1` | `1.2.3-rc.1` | `1.2.3` | Prerelease |

Windows numeric resources have a fourth component of zero. The macOS `CFBundleShortVersionString` and `CFBundleVersion` use the numeric core; `QFRApplicationVersion` in `Info.plist` preserves the full version, including a prerelease suffix or build metadata. The app's runtime version retains that same full identity on all platforms. Package inspection checks the Windows and macOS metadata against the configured version. `SOURCE.txt` records the full version and the CMake argument needed to rebuild it.

Branch and pull-request builds retain the default preview version `1.0.0` and publish only Actions artifacts. Manual workflow dispatch runs checks and produces artifacts, including when dispatched at a tag, but does not publish a release. Only a tag push enables the publishing job. All other jobs have read-only repository permissions; only that job receives `contents: write`.

## Publication and reruns

The publisher downloads the three named platform artifacts from the current workflow run. Each tag artifact includes a manifest with the package hash, tag, application version, source commit, and workflow run ID. Publication rejects missing or duplicate packages, mismatched manifests, changed package bytes, and a tag that no longer points at the built source. The Linux runtime job checks the same run's Linux artifact before publication.

A new release starts as a draft. It becomes public only after all three assets are uploaded, downloaded again, and verified. An upload or verification failure leaves the new release in draft. Tag runs do not cancel a previous run for the same tag while it is publishing.

To resume a failed upload, use GitHub Actions' **Re-run failed jobs** on the original tag run while its artifacts remain available. The publisher locates the existing draft or public release, verifies existing assets byte for byte, and uploads only missing downloads. It preserves other release assets and published release metadata. When publishing a draft, it refreshes generated release notes so their hashes and provenance describe the packages actually verified. An existing prerelease/release type mismatch fails for review rather than changing the release type.

An asset with the same filename and different contents causes failure. The publisher never deletes or replaces an asset, including one left by a failed upload. A new version tag is required for changed downloads. Rebuilding all jobs may change archive timestamps or signatures even for identical source, so reuse the original successful build artifacts when resuming publication. If those artifacts expire, a new version tag is the reliable way to publish rebuilt downloads.

## Validation limits and local checks

CI checks inspect Windows imports, both macOS architecture slices and deployment targets, and the Linux runtime baseline. macOS tests execute only the runner's native architecture. Linux baseline startup uses extraction and a virtual display rather than a FUSE mount. These checks do not establish real microphone access, permission prompts, input-device changes, or recording on every supported desktop. The [acceptance record](acceptance.md) retains those separate checks and their actual evidence.

The downloads still have no commercial publisher signing or macOS notarization. Windows and Linux packages have no publisher signature. The macOS app uses an ad-hoc signature to support Apple Silicon loading.

To check release behavior locally with Python 3:

```bash
python -m pip install -r tests/requirements-release.txt
python -B -m unittest discover -s tests -p 'test_release*.py'
python scripts/release.py version --tag v1.2.3-rc.1
```

The tests exercise the command and GitHub HTTP boundaries without publishing a real release. They cover version validation, workflow events and permissions, job dependencies, artifact selection, draft publication, and rerun preservation and rejection cases. The standard CTest suite also runs the dependency-free version and publication tests. Build and package instructions are in [build.md](build.md).
