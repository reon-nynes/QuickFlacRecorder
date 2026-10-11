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
    fontSize: "15px"
  label:
    fontFamily: "Segoe UI, system-ui, sans-serif"
    fontSize: "14px"
  secondary:
    fontFamily: "Segoe UI, system-ui, sans-serif"
    fontSize: "13px"
  mono:
    fontFamily: "Consolas, Menlo, DejaVu Sans Mono, monospace"
    fontSize: "13px"
  timer:
    fontFamily: "Consolas, Menlo, DejaVu Sans Mono, monospace"
    fontSize: "32px"
  action:
    fontFamily: "Segoe UI, system-ui, sans-serif"
    fontSize: "22px"
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

`background` is the window body; `surface` is the field, selector, finalizing action, and meter track. `border` separates controls. `text` holds entered values and primary labels; `muted` holds field labels, format metadata, and meter scale. `meter` marks incoming audio and successful completion. `record` supplies the idle Record dot and restrained action emphasis. Record mixes 15% record with 85% surface; active Stop mixes 35% record with 65% surface and has a white square. `warning` marks peaks above -6 dBFS and incomplete recordings; `danger` marks clipping and errors. All statuses also have text. The teal focus outline appears on enabled fields and actions.

The header maps each `colors.*` entry to the identically named `qfr::theme::*` constant. No alternate light theme is part of v1.

## Typography

`theme::body()` uses Segoe UI on Windows and the platform system sans elsewhere. Fields, selectors, selector popups, secondary buttons, and status use 15 logical pixels. Field labels use 14 pixels; format information, filenames, and meter numbers use 13 pixels. Record/Stop and Finishing... use 22 pixels. `theme::data()` uses JUCE's platform monospaced font for filenames, elapsed time, and meter numbers, with system font fallback. The elapsed timer uses 32 pixels and displays HH:MM:SS with stable-width digits. Its fixed area remains centered across seconds, minutes, and hours; labels never shrink the timer to fit.

The frontmatter maps sans, label, secondary, mono, timer, and action to `bodySize`, `labelSize`, `secondarySize`, `secondarySize`, `timerSize`, and `actionSize` in `RecorderTheme.h`. Native controls consume these through the shared LookAndFeel or `body`/`data` helpers. Paths remain editable in full. Folder, prefix, selected input/channel, filename, and status tooltips expose full values when the display truncates them.

## Layout

The content is 600 by 400 logical pixels, plus the native title bar. A 14-pixel outer margin bounds the controls. Two 28-pixel rows hold the target folder and recording prefix. Audio driver and input device share the next row with separate 18-pixel labels and 28-pixel selectors. Mono/stereo and explicit left/right channels share the row below. A dedicated 60-pixel-high timer area spans the content between the margins at y=178 and centers elapsed time horizontally and vertically. The selected channel meter sits left of the 140 by 64-pixel Record/Stop action at y=244. Format and filename each have an 18-pixel row, at y=310 and y=330. Status has a reserved 36-pixel area at y=350 for two lines of 15-pixel text. Label bounds include JUCE's four-pixel horizontal inset so text aligns with the controls.

`contentWidth`, `contentHeight`, `timerAreaHeight`, `recordWidth`, `recordHeight`, and `margin` in the runtime theme implement these values. Issue #8 approves this larger layout and typography in place of the original compact design. The implemented values retain the proposed baseline; the footer allocation and compact setup rows make room for it without increasing the window.

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

The primary action is a native JUCE TextButton named `recordingAction`, with its icon and centered text group drawn by the shared LookAndFeel. Record has a 14-pixel dot; Stop has a 14-pixel square; the icon-to-label gap is 10 pixels. Finishing... uses the same 140 by 64 area, no capture icon, muted text, a dimmed surface, and disabled semantics. Disabled Record uses a muted dot and label. Hover brightens the fill by 12%; pressed darkens it by 12%; keyboard focus adds a two-pixel teal outline. These states preserve text labels and never change geometry.

`Finishing...` and locked controls expose finalization. The close dialog initially focuses Continue recording; Return and Escape keep recording. Stop and quit waits for finalization. Space toggles capture only outside text fields, selectors, and modal menus/dialogs.

The meter polls the session at 25 Hz, displays selected-channel dBFS peaks, decays the displayed peak, and holds each clipping indicator for 1.2 seconds. Meter smoothing changes display only. There is no gain or other sound processing. Color has parallel channel labels, numeric peaks, and CLIP text. Native controls supply accessible names; status and meter descriptions expose their current values.

## Do's and Don'ts

- Use the glossary's recording and input terminology.
- Keep a missing remembered folder or input visible until the user selects a replacement.
- Keep the selected channel order explicit and use the opened input stream rate.
- Use status text to distinguish failed capture, an incomplete file, and a successfully saved recording.
- Do not add playback, a waveform, gain controls, or a second settings screen.
- Do not move the setup controls when their enabled state changes.
