# Build review and native acceptance

Record results for the exact source commit and binary SHA-256. An unchecked item is pending. A passing build or session test is evidence only for the behavior it exercised. It cannot establish microphone capture, permissions, or native controls on a different operating system.

The owner reviews the working Windows build and draft pull request before a public release. The CI rows link to the current PR's live check results. A successful job and its downloadable artifact supply evidence for that job; the job's logs retain failures as well.

## Build evidence

| Check | Required evidence | Current status |
| --- | --- | --- |
| Local Windows Release build and session tests | Commit, compiler/CMake versions, build output, `ctest` output | Passed, [Windows report](validation/windows.md) |
| Windows portable executable | `windows-dependencies.txt`, x64 result, binary hash, normal launch | Passed locally, [Windows report](validation/windows.md) |
| Windows CI | Successful run for the implementation commit and downloadable preview | [Current PR checks](https://github.com/reon-nynes/QuickFlacRecorder/pull/5/checks), job `windows` |
| macOS universal CI | Successful run, both architecture/minimum-version results, microphone description, signature verification, preview | [Current PR checks](https://github.com/reon-nynes/QuickFlacRecorder/pull/5/checks), job `macos` |
| Linux CI | Successful Ubuntu 22.04 container build/session tests and AppImage preview | [Current PR checks](https://github.com/reon-nynes/QuickFlacRecorder/pull/5/checks), job `linux` |
| Linux baseline CI | Fresh Ubuntu 22.04 report with dependency/GLIBC and packaged-GUI startup results | [Current PR checks](https://github.com/reon-nynes/QuickFlacRecorder/pull/5/checks), job `linux-baseline` |
| Source and notices | Matching commit in `SOURCE.txt`, source ZIP, original dependency notices | Passed locally, included in each preview package |
| Draft pull request and owner review | Draft PR URL, working Windows build supplied to owner, owner's review result | [Draft PR #5](https://github.com/reon-nynes/QuickFlacRecorder/pull/5), build supplied; owner review pending |

The CI macOS tests run on the runner's native architecture. The binary inspection checks both compiled slices. The Linux baseline job extracts the AppImage to avoid a FUSE mount inside a container. Normal AppImage launch remains a desktop check.

## Windows microphone check

The owner selected the UltraLite-mk5 input device, mono input channel 1, for the initial local Windows validation. Record the audio driver actually used and the opened input stream rate. Do not infer either from the device's advertised settings.

- [x] Launch the Release executable on Windows and select UltraLite-mk5, mono input channel 1.
- [ ] Confirm the idle level meter responds to physical microphone input on channel 1.
- [ ] Record a short spoken take, stop, and wait for successful finalization.
- [x] Inspect the saved FLAC with an independent tool for one channel, 16-bit depth, the displayed input stream rate, frame count/duration, and decodable contents.
- [ ] Listen to the recording and confirm it captured the intended input.
- [x] Record a second take with the same prefix and verify its counter advanced and the first file remains unchanged.
- [x] Record the commit, executable hash, Windows version, input device, audio driver, channel, input stream rate, tool output, and observed result below.

Windows recording evidence: [actual UltraLite channel 1 capture and independent FLAC inspection](validation/windows.md). Listening to confirm the physical microphone remains pending.

## Native UI checks

Run these on the local Windows build. Repeat on macOS and Linux before claiming their native UI is validated. Record screenshots or observations for the app's idle, recording, finalizing, and error states.

- [x] The dark content area is 600 by 400 logical pixels plus the native title bar. The 15-pixel body/control/status text, 14-pixel labels, and 13-pixel secondary text remain readable at Windows 150% scaling, with reserved footer space and no observed overlap.
- [x] The 32-pixel monospaced HH:MM:SS timer is centered in a dedicated 60-pixel-high area. Native live capture and controlled boundary values retain centered alignment across seconds, minutes, and hours.
- [x] Target-folder and recording-prefix fields appear above audio-driver, input-device, mode, and explicit channel controls.
- [ ] One peak bar appears for mono and two for a stereo pair. Each responds to its selected channel while idle and recording, reports dBFS, and briefly indicates clipping.
- [x] The 140 by 64 Record dot becomes a labelled Stop square during capture. Hover, pressed, focus, disabled, and recording states remain readable. Finishing... was observed disabled at the same bounds before Saved. The displayed input stream rate and fixed 16-bit output match the inspected FLAC.
- [x] Full-window idle, recording, stereo, error, hover, pressed, selector focus, popup, and long-path evidence has been refreshed for the Windows 150% layout. Finalization has a native observation because it completes between screenshot frames.
- [ ] Repeat readability, clipping, popup, focus, and long-value checks at other supported desktop scales and on macOS/Linux. The broader DPI matrix is incomplete.
- [ ] Mono channel selection captures the chosen channel. Stereo left/right selectors are separate and reject duplicate channels.
- [ ] The target folder, recording prefix, audio driver, input device, and channel selectors lock during capture. Another take stays disabled until finalization completes.
- [ ] Space toggles Record/Stop when no editable control has focus. Typing Space in the prefix or folder field and using selectors does not start or stop a take.
- [x] Closing during capture offers Continue recording and Stop and quit. Continue keeps the take active; Stop and quit finishes writing before the process exits.
- [ ] Changing the target folder or prefix refreshes the next filename. Invalid prefixes and missing/unwritable folders produce actionable errors without creating files elsewhere.
- [ ] Existing complete and incomplete files occupy their counters. Repeated takes retain the prefix and never replace either kind of file.
- [x] Reopening restores the target folder, audio driver, input device, channel choices, and window position. Each launch generates a fresh local date/time prefix.
- [x] A missing remembered folder requires another existing folder. A missing remembered input requires an explicit replacement; it does not silently select a different input.
- [ ] Disconnecting the selected input during a take stops capture, reports a failure, and preserves audio that can be finalized. An unsuccessful finalization retains its incomplete name and is not described as a successful save.
- [x] Incomplete-file fixtures remain available and have visible incomplete status. The app does not claim automatic recovery. An actual process-crash test is still pending.

Native UI evidence: [complete window screenshots and observed Windows behavior](validation/windows.md). Unchecked items still require their full native procedure; the report distinguishes partial observations from pending checks.

## Outstanding macOS hardware checks

- [ ] Launch the downloaded universal bundle on macOS 12 or later on both Intel and Apple Silicon.
- [ ] Verify first-use microphone consent, denied permission, subsequent grant, and the recorder's actionable status.
- [ ] Verify CoreAudio input enumeration, default selection, preference restoration, and missing remembered input behavior.
- [ ] Make mono and stereo recordings with a physical input. Inspect channel order, bit depth, rate, duration, and finalization; listen to the result.
- [ ] Change or disconnect a real input during capture and verify explicit failure and preservation of captured audio.
- [ ] Run the native UI checklist, including keyboard focus and close-during-recording behavior.

macOS hardware evidence: pending. Universal compilation, bundle metadata, and runner tests do not establish these results.

## Outstanding Linux hardware checks

- [ ] Launch the AppImage normally on an Ubuntu 22.04 desktop and at least one representative later Ubuntu/PipeWire desktop. Record distribution/version and whether FUSE or extraction was used.
- [ ] Verify ALSA input enumeration, device access, default selection, preference restoration, and missing remembered input behavior.
- [ ] Verify optional JACK on a real JACK setup, including device/channel selection and disconnect behavior.
- [ ] Verify PipeWire compatibility through the available ALSA/JACK paths. Record the audio path and host configuration rather than claiming a separate native PipeWire driver.
- [ ] Make mono and stereo recordings with a physical input. Inspect channel order, bit depth, rate, duration, and finalization; listen to the result.
- [ ] Verify denied device access and unavailable audio services produce actionable status.
- [ ] Run the native UI checklist, including keyboard focus and close-during-recording behavior.

Linux hardware evidence: pending. The fresh-container baseline checks do not establish these results or promise compatibility with other distributions.

## Evidence entry format

For each completed check, add the date, platform/version, source commit, binary hash, device/audio-driver details when relevant, result, and a link or path to the output or observation. Keep failed and partial outcomes visible. Do not mark unrelated checks complete because one platform passed.

Public-release review remains pending until the owner reviews the working build. The workflow produces review artifacts and does not publish a public release.
