# Launch an unsigned preview

Download the artifact for your operating system from a successful [build and package workflow run](https://github.com/reon-nynes/QuickFlacRecorder/actions/workflows/preview.yml). Use the run associated with the draft pull request's implementation commit. Extract GitHub's outer artifact archive, then extract the platform archive inside it. The platform archive contains the portable application, this guide, licenses, source, and a SHA-256 record. Workflow artifacts require GitHub sign-in and expire after 30 days.

These are review builds. They have no publisher certificate or macOS notarization. A public release follows the owner's review of the working Windows build. The [acceptance record](acceptance.md) tracks actual checks. Package inspection and CI success do not replace real microphone, permission, or device checks.

## Windows 10/11 x64

Extract `QuickFlacRecorder-windows-x64.zip` to an ordinary writable folder and launch `QuickFlacRecorder.exe`. The executable includes its C++ runtime and FLAC codec. Keep the accompanying licenses and source archive when redistributing it.

Windows may show a SmartScreen unknown-publisher prompt for an unsigned preview. After checking that the download came from the expected repository and commit, use More info and Run anyway if those controls are available and you choose to run it. If local policy prevents unsigned applications, build from source or use a permitted environment.

Allow microphone access in Windows Settings. Windows 11 uses Privacy & security, Microphone; Windows 10 uses Privacy, Microphone. Both require microphone access for desktop apps. Select the intended audio driver and input device in the recorder. WASAPI shared, WASAPI exclusive, and ASIO depend on the installed device support. ASIO requires the audio interface's own driver, which the preview does not install.

## macOS 12 or later

Extract `QuickFlacRecorder-macos-universal.zip`. Move `QuickFlacRecorder.app` to Applications or another folder you control, then open it. The same bundle contains Intel and Apple Silicon executables. It has an ad-hoc signature for loading, but no Developer ID signature or notarization.

If macOS blocks the unknown developer, first attempt to open the app, then use the app-specific Open Anyway control in Privacy & Security if you choose to trust this preview. macOS 12 calls this pane System Preferences, Security & Privacy; newer versions use System Settings, Privacy & Security. Follow [Apple's unknown-developer instructions](https://support.apple.com/guide/mac-help/open-a-mac-app-from-an-unknown-developer-mh40616/mac) for your system. Keep Gatekeeper enabled.

Grant microphone access when prompted. If you deny it, grant QuickFlacRecorder access in Privacy, Microphone and relaunch the app if necessary. Select the intended CoreAudio input device. Permission prompts, denied-access messages, device changes, and real recording remain native acceptance checks.

## Linux x64, Ubuntu 22.04 baseline

Extract `QuickFlacRecorder-linux-x64.tar.gz`. In its extracted folder:

```bash
chmod +x QuickFlacRecorder-x86_64.AppImage
./QuickFlacRecorder-x86_64.AppImage
```

If the AppImage reports that FUSE is unavailable, follow [AppImage's FUSE guidance](https://docs.appimage.org/user-guide/troubleshooting/fuse.html). Ubuntu 22.04 uses `libfuse2`; Ubuntu 24.04 calls that compatibility package `libfuse2t64`. You can also extract and run without a FUSE mount:

```bash
./QuickFlacRecorder-x86_64.AppImage --appimage-extract
./squashfs-root/AppRun
```

Run as your normal desktop user. Select ALSA or an available JACK audio driver and the intended input device. JACK requires a working JACK server or compatible host setup. A PipeWire desktop can provide JACK compatibility through `pw-jack` where installed. Follow [PipeWire's JACK instructions](https://docs.pipewire.org/page_man_pw-jack_1.html) for your distribution. The CI baseline startup check does not establish ALSA device access, JACK, PipeWire, or microphone capture on a real desktop.

## Make a recording

1. Connect the microphone or audio interface. Select the audio driver and input device you intend to use.
2. Confirm the target folder exists and is writable. The first-launch target is Downloads; choose another folder if it is unavailable.
3. Enter a recording prefix if you want a name other than the launch's local date/time. Check the next complete filename. The preview does not reserve the name until Record starts.
4. Choose one mono input channel, or distinct left and right channels for a stereo pair. Verify the selected-channel level meter responds. Check the displayed input stream rate and 16-bit FLAC output.
5. Press Record, then Stop when the take is finished. Wait for finalization before starting another take. A successful take has a `.flac` name; an incomplete take keeps `.partial.flac` and an error status.

The app retains the prefix for later takes and chooses a counter above the highest occupied counter for that exact prefix. Existing complete and incomplete recordings remain intact. Space toggles Record/Stop when no editable field or selector has focus. Closing during a take offers Continue recording or Stop and quit.

Inspect and listen to a short trial recording before relying on a preview for a long session. If capture fails, retain the reported message and any incomplete file when reporting the problem. The app does not repair incomplete recordings automatically.
