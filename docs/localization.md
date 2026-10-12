# Contributing translations

The recorder supports English and Simplified Chinese for normal recording, validation, errors, incomplete recordings, and recovery. The current user's interface-language preferences select one language per launch. English is the fallback. See [ADR 0003](adr/0003-system-interface-language-catalogs.md) for architecture and formatter limits.

## Register a catalog

1. Add one UTF-8 JSON file to `src/localization/catalogs`, named with a normalized language tag, such as `fr.json`. The file is a flat object of stable message keys and complete string templates. Copy the full key set from `en.json`; missing keys fail production validation.
2. Add an entry to `src/localization/languages.json` with `tag`, native `name`, matching `aliases`, JUCE BinaryData `resource`, and `pluralRule`. CMake includes catalog files automatically. JUCE removes punctuation when constructing identifiers, so `zh-Hans.json` becomes `zhHans_json`. The generated TranslationResources.h lists the actual resource names.
3. Use canonical tags and aliases. Exact and script matches precede generic aliases. Do not add an ambiguous generic alias for a second script catalog. The existing `zh` alias is the deliberate initial Simplified Chinese fallback.
4. Choose `oneOther` only when the language's cardinal rule matches the documented English rule, or `other` for a language with only that category. More complex rules belong inside the translation module with authoritative reference cases. Do not implement grammar conditionals in recorder controls.
5. Reconfigure and build the tests. The production catalog validator includes every registered catalog. No recorder caller or OS adapter changes are needed for registration.

## Define a message

Add a `QFR_MESSAGE` row to `src/localization/Messages.def` with a semantic C++ MessageId, a stable JSON key, a named argument schema, and translator context. Declare each argument as `name:text` or `name:number`. Explain what each value means and which technical content must remain verbatim. Add the message to every production catalog and use `Translation::text(MessageId, arguments)` in presentation. Do not use English text as a key or concatenate translated sentence fragments.

Templates support `{filename}`, `{channels, plural, one {# channel} other {# channels}}`, and `{mode, select, mono {complete mono message} other {complete alternative}}`. Alternatives may contain named arguments and nested choices. `other` is mandatory. Exact plural branches such as `=0 {No channels}` precede locale categories. `#` prints the surrounding plural argument. Arguments may be reordered and repeated; every declared argument must be used somewhere in the template. Literal `{`, `}`, `#`, and backslash use a backslash escape, doubled in JSON strings. ICU apostrophe quoting and other ICU features are unsupported.

Pass original filenames, paths, native audio-driver/device/channel names, and user-entered prefixes as text values. Format numeric timing and technical units as data. Number arguments drive cardinal variants; this module prints them with its documented numeric representation. A missing argument, extra argument, duplicate argument, wrong type, or nonfinite number raises an argument error. Invalid translated entries use the complete English message and its plural rule.

`RecorderResult` carries stable validation/input/start codes and original native diagnostics and identities. `RecordingSession::Snapshot` keeps failure reason, finalization outcome, actual retained file, incomplete status, native input detail, and disk detail separately. `RecordingPresentation` selects whole catalog templates. The compound failure template receives stable select values, never parsed English text. Incomplete-recording counts use the shared cardinal formatter. Add behavior assertions at the session/input interfaces and wording assertions at presentation; recording code and audio callbacks must not import the translation module.

## Validate and inspect

```text
cmake --build build-v1 --config Release --target QuickFlacRecorder RecorderSessionTests RecorderTranslationTests RecorderPresentationTests RecorderUiTests --parallel 2
ctest --test-dir build-v1 -C Release --output-on-failure --no-tests=error
```

Use your configured build directory. Single-configuration generators omit `--config Release` and `-C Release`. Linux GUI tests require a display, for example `xvfb-run -a ctest ...`. Catalog validation runs through the public translation interface. Tests reject missing/unknown/duplicate keys, malformed JSON/templates, incorrect argument names/types, and absent defaults, and check whole-message English fallback. A partial, test-only `en-XA` catalog exercises expanded text and reordered values without entering the production registry or binary.

For Windows rendering and real default-input recording evidence, use:

```text
build-v1/Release/RecorderUiTests.exe --evidence D:/Dev/QuickFlacRecorder/build-v1/localization-evidence --native-recording
```

This optional check records short takes in an isolated temporary folder and removes those files afterward. It creates isolated settings, preserves your real recorder preferences and OS language settings, exercises the native popup/tooltip/dialog and keyboard path, and writes PNG evidence at requested rendering scales. It requires a usable default input device. Ordinary CTest checks do not require recording hardware.

Inspect every enabled language at 100%, 150%, and 200% rendering scale. Check labels, Record/Stop/Finishing captions, mono/stereo selectors, complete ready/recording/finalizing/saved messages, long filenames/paths, tooltip wrapping, opened popups, close-dialog actions, meter glyphs, and accessible descriptions. Essential action labels must fit. Original data can truncate visually only when the full value remains available through editing or a tooltip. Confirm Record's dot, Stop's square, colors, locks, enabled state, and keyboard behavior independently of captions. Record platform/font limitations explicitly.

On macOS, verify `CFBundleLocalizations`, the English development region, the default microphone description, and each generated `<tag>.lproj/InfoPlist.strings` in the packaged app. CMake derives the declared languages from the shared registry and the descriptions from catalogs. The package inspector checks these values. On Linux, verify a Chinese-capable font on the Ubuntu baseline and a representative desktop. Packaging embeds catalogs in all binaries; translations must work from a portable package without source files beside it or a network request.

For the native macOS permission check, use an isolated account or test machine. Set the application's preferred language to English or Simplified Chinese in System Settings, quit it, and reset microphone consent for `com.reon-nynes.quickflacrecorder` with `tccutil reset Microphone com.reon-nynes.quickflacrecorder`. Launch the packaged app, inspect the usage explanation against that language's `microphone_usage` catalog entry, and deny consent. Verify localized recorder guidance and disabled Record. Grant permission in Privacy & Security > Microphone, reopen the app, explicitly select an input if needed, and record. Repeat in the other language and record OS version, architecture, bundle source commit, binary hash, prompt screenshots, and recording result. The consent actions and native folder-dialog controls follow the OS language. This procedure changes permission state only on the chosen test account or machine. Local Windows results do not establish macOS permission behavior.

The [recovery validation report](validation/recovery.md) records issue #11 evidence and pending platform checks.
