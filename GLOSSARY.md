# QuickFlacRecorder

QuickFlacRecorder captures audio from a selected input and saves recordings as FLAC files.

## Language

**Recording**:
A captured stretch of input audio saved as one FLAC file.
_Avoid_: Clip, project

**Target folder**:
The folder where recording files are saved.
_Avoid_: Output directory, destination directory

**Input device**:
The microphone or audio interface that supplies audio for recording.
_Avoid_: Driver, audio backend

**Level meter**:
The display of the strength of the incoming audio signal.
_Avoid_: Volume control

**Recording prefix**:
The editable part of a recording's save name before the counter and file extension, initially based on the local date and time and retained across takes.
_Avoid_: Full filename

**Counter**:
The number that distinguishes recordings with the same prefix in a target folder, starting at 01; incomplete recordings also occupy their numbers.
_Avoid_: Timestamp, recording ID

**Mono input**:
One selected input channel recorded into a one-channel FLAC file.
_Avoid_: All microphone channels

**Stereo pair**:
Two selected input channels recorded as the left and right channels of one FLAC file.
_Avoid_: Every device channel

**Audio driver**:
The selectable audio system used to access an input device, such as WASAPI, ASIO, CoreAudio, ALSA, or JACK.
_Avoid_: Microphone, input device

**Input stream rate**:
The actual number of audio samples per second reported by the opened input stream.
_Avoid_: Guaranteed hardware-native rate

**Incomplete recording**:
A captured take that has not successfully finished saving and still carries a .partial.flac name.
_Avoid_: Saved recording, recovered recording

**Recording interruption**:
A period when a recording cannot capture or save incoming audio and needs recovery before it can continue.
_Avoid_: Warning, stopped recording

**Recording gap**:
An interval of missing input audio within a recording, documented separately from the audio that was saved.
_Avoid_: Silence, complete audio

**Recording log**:
A file accompanying a recording that documents its errors, recording gaps, recovery attempts, and outcomes.
_Avoid_: Application log, crash report

**Recording rollover**:
An automatic transition from the current recording to a new recording with the next counter after an input-device change prevents continuing the current FLAC.
_Avoid_: Resampling, appending a different format
