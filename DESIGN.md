---
version: alpha
name: QuickFlacRecorder
description: A compact native recorder with a dark audio utility panel and selected input peak meters.
colors:
  background: "#20272e"
  surface: "#2a333c"
  border: "#44515d"
  text: "#eff2f6"
  muted: "#b3bdc8"
  record: "#d96863"
  meter: "#63d8c2"
  warning: "#e5b864"
  danger: "#ff8681"
typography:
  sans:
    fontFamily: "Segoe UI, system-ui, sans-serif"
    fontSize: "13px"
  mono:
    fontFamily: "Consolas, Menlo, DejaVu Sans Mono, monospace"
    fontSize: "12px"
rounded:
  DEFAULT: "4px"
spacing:
  margin: "14px"
components:
  button: {}
  field: {}
  selector: {}
  meter: {}
  status: {}
---

# QuickFlacRecorder design

## Overview

The visual reference is the input strip of a desktop audio interface. This is a product utility for people capturing microphone or interface audio while other applications stay open. The maintained product contract is [docs/spec.md](docs/spec.md); the accepted framework and packaging decisions are in [docs/adr](docs/adr).

The v1 interface uses English copy, local time for the new recording prefix, and explicit input channel numbers. No regional market is implied. A quiet dark panel keeps the input setup legible; the selected channel meter and adjacent large Record/Stop control carry the emphasis. The app avoids decorative cards, marketing headlines, a waveform, and animated decoration.

Runtime tokens in [src/RecorderTheme.h](src/RecorderTheme.h) are canonical for this native JUCE application. The frontmatter mirrors their values and the sections below explain their roles. Theme changes update the header and this file together. Shared controls consume the single `qfr::theme` LookAndFeel, and meter/status drawing consumes the same semantic colors. There is one screen and no separate settings window.

## Colors

`background` is the window body; `surface` is the field, selector, idle action, and meter track. `border` separates controls. `text` holds entered values and primary labels; `muted` holds field labels, format metadata, and meter scale. `meter` marks incoming audio and successful completion. `record` marks the active Stop button; `warning` marks peaks above -6 dBFS and incomplete recordings; `danger` marks clipping and errors. All statuses also have text. The teal focus outline appears on enabled fields and actions.

The header maps each `colors.*` entry to the identically named `qfr::theme::*` constant. No alternate light theme is part of v1.

## Typography

`theme::body()` uses Segoe UI on Windows and the platform system sans elsewhere. Fields and controls use 13 logical pixels. Field labels and status use 12 pixels; Record/Stop uses 22 pixels. `theme::data()` uses JUCE's platform monospaced font for filenames, elapsed time, and meter numbers, with system font fallback. Paths remain editable in full and full filenames/statuses have tooltips when the compact display truncates them.

## Layout

The content is 520 by 300 logical pixels, plus the native title bar. A 14-pixel outer margin bounds the controls. Two 28-pixel rows hold the target folder and recording prefix. Audio driver and input device share the next row. Mono/stereo and explicit left/right channels share the row below. The selected channel meter sits left of the 128 by 53-pixel Record/Stop action. Format and elapsed time, filename preview, and a two-line status fill the final rows.

Mono uses the full channel selection width; stereo reveals separate ordered left and right selectors. Controls keep their positions during recording, finalization, success, and error. The native window is fixed size and remembers its position within available displays.

## Elevation & Depth

The app uses flat tonal surfaces and one-pixel borders. Native title-bar decoration and modal-window placement belong to the platform/JUCE. No shadows or blur are added to the content.

## Shapes

Controls use the shared four-pixel radius. Meter tracks use two pixels. Standard native title-bar buttons retain platform geometry.

## Components

| Capability | Canonical owner | Source of truth | Allowed variants | Verification |
| --- | --- | --- | --- | --- |
| Select/Listbox | JUCE ComboBox and theme::LookAndFeel | This file and docs/spec.md | Framework authored popup | Native keyboard and popup acceptance |
| Form | RecorderComponent fields and RecordingSession configuration | docs/spec.md and RecordingSession.h | Target folder, prefix, audio setup | Session tests and native acceptance |
| Status | RecorderComponent status label and RecordingSession snapshot | docs/spec.md | Ready, recording, finalizing, saved, failed, incomplete | Session tests and native acceptance |
| Dialog | JUCE AlertWindow in RecorderComponent | docs/spec.md | Continue recording or Stop and quit | Native acceptance |

JUCE owns selector semantics, keyboard selection, and popup placement/scrolling; the shared LookAndFeel owns colors and fonts. The native folder chooser owns directory navigation. Values are preserved after validation failures. The session owns filename and recording validation; native device integration adds actionable input/permission errors.

Buttons retain size when disabled or busy. `Finishing...` and locked controls expose finalization. The close dialog initially focuses Continue recording; Return and Escape keep recording. Stop and quit waits for finalization. Space toggles capture only outside text fields, selectors, and modal menus/dialogs.

The meter polls the session at 25 Hz, displays selected-channel dBFS peaks, decays the displayed peak, and holds each clipping indicator for 1.2 seconds. Meter smoothing changes display only. There is no gain or other sound processing. Color has parallel channel labels, numeric peaks, and CLIP text. Native controls supply accessible names; status and meter descriptions expose their current values.

## Do's and Don'ts

- Use the glossary's recording and input terminology.
- Keep a missing remembered folder or input visible until the user selects a replacement.
- Keep the selected channel order explicit and use the opened input stream rate.
- Use status text to distinguish failed capture, an incomplete file, and a successfully saved recording.
- Do not add playback, a waveform, gain controls, or a second settings screen.
- Do not move the setup controls when their enabled state changes.
