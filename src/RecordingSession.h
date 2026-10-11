// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
#include <functional>
#include <memory>
#include <chrono>
#include "RecorderResult.h"

namespace qfr
{
class RecordingSession final
{
public:
    enum class State { idle, recording, finalizing, succeeded, failed };
    enum class ResultCode { none, ready, recording, finalizing, saved, failed };
    enum class FailureReason { none, inputLost, discontinuity, overflow, writer, diskFlush, promotion, start };
    enum class FinalizationOutcome { none, finalized, writerFailed, diskFlushFailed, promotionFailed };

    struct Configuration
    {
        juce::File targetFolder;
        juce::String recordingPrefix;
        double inputStreamRate = 0;
        int leftInputChannel = 0; // Zero-based indices into the callback's input array.
        int rightInputChannel = -1; // -1 selects mono; otherwise a distinct stereo right channel.
    };

    struct Options
    {
        size_t captureCapacityFrames = 262144;
        // Optional disk boundary adapter, also usable to simulate output failures.
        std::function<std::unique_ptr<juce::OutputStream>(std::unique_ptr<juce::OutputStream>)> outputAdapter;
        // Writer-thread durability policy; the application uses five seconds.
        std::chrono::milliseconds diskFlushInterval { 5000 };
    };

    struct Snapshot
    {
        State state = State::idle;
        ResultCode resultCode = ResultCode::none;
        RecorderResult error;
        juce::String inputDetail; // Verbatim native diagnostic retained independently of disk diagnostics.
        FailureReason failureReason = FailureReason::none;
        FinalizationOutcome finalization = FinalizationOutcome::none;
        juce::File nextFile;
        juce::File recordingFile;
        bool incomplete = false;
        double elapsedSeconds = 0;
        double inputStreamRate = 0;
        int outputChannels = 1;
        bool controlsLocked() const noexcept { return state == State::recording || state == State::finalizing; }
    };

    struct Levels
    {
        std::array<float, 2> peak { 0, 0 }; // Linear peaks since the last read, convert to dBFS in the UI.
        std::array<bool, 2> clipped { false, false };
        int channels = 1;
    };

    RecordingSession();
    explicit RecordingSession(Options);
    ~RecordingSession(); // Detach the input callback before destruction.
    RecordingSession(const RecordingSession&) = delete;
    RecordingSession& operator=(const RecordingSession&) = delete;

    // Call these methods from one control thread. Configuration updates recompute the filename preview.
    RecorderResult configure(const Configuration&);
    RecorderResult refreshPreview();
    RecorderResult start();
    void requestStop(); // Returns immediately; poll snapshot() until finalization ends.
    void waitForStop(); // Blocking, for tests and shutdown after capture has been stopped.
    Snapshot snapshot() const;
    Levels readLevels() noexcept;
    void retainInputDiagnostic(const juce::String&); // Control thread, after polling the native input boundary.

    // Exactly one audio producer. No allocation, filesystem I/O, encoding, or blocking locks.
    // Call while idle as well, for selected-channel metering.
    void processInput(const float* const* inputs, int inputChannels, int frames) noexcept;
    void inputLost() noexcept; // Safe from an input-device callback; preserves queued captured audio.
    void inputDiscontinuity() noexcept; // Report driver overruns/missing samples without claiming input loss.

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
