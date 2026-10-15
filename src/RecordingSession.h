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
    enum class State { idle, recording, recovering, finalizing, succeeded, failed };
    enum class ResultCode { none, ready, recording, recovering, finalizing, saved, failed };
    enum class FailureReason { none, inputLost, discontinuity, overflow, writer, diskFlush, promotion, start, log };
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
        // Controlled native-operation boundary. Zero runs the real operation; a
        // platform error simulates its failure. Close always releases the handle.
        // Called only on control/writer threads, never from an audio callback.
        std::function<uint64_t(DiskOperation, const juce::File&)> nativeError;
        // Optional folder-enumeration adapter for recording filename allocation.
        // Previews still scan the real target folder. Never called from audio input.
        std::function<juce::Array<juce::File>(const juce::File&)> allocationEntries;
        std::chrono::milliseconds storageRetryInterval { 1000 };
        std::chrono::milliseconds noProgressTimeout { 10000 };
        std::chrono::milliseconds callbackTimeout { 1000 };
        std::chrono::milliseconds recoveryInterval { 3000 };
    };

    struct Snapshot
    {
        State state = State::idle;
        ResultCode resultCode = ResultCode::none;
        RecorderResult error;
        RecorderResult logError; // Separate when required logging fails after an earlier recording disk error.
        juce::String inputDetail; // Verbatim native diagnostic retained independently of disk diagnostics.
        FailureReason failureReason = FailureReason::none;
        FinalizationOutcome finalization = FinalizationOutcome::none;
        juce::File nextFile;
        juce::File recordingFile;
        bool incomplete = false;
        double elapsedSeconds = 0;
        double inputStreamRate = 0;
        int outputChannels = 1;
        int leftInputChannel = 0, rightInputChannel = -1;
        juce::File logFile;
        bool hasRecordingErrors = false;
        unsigned recoveryAttempts = 0; // Actual input recovery requests taken since the last saved incoming audio.
        bool controlsLocked() const noexcept { return state == State::recording || state == State::recovering || state == State::finalizing; }
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
    // Control thread. A nonzero count records grouped callback occurrences;
    // zero publishes a diagnostic idempotently when polling or reopening input.
    void retainInputDiagnostic(const juce::String&, uint64_t occurrences = 0);
    // Control thread. Starts one attempt; finish it before taking another request.
    // Reopen only the originally selected input. Pending requests do not consume attempts.
    // Outstanding reopens remain serialized across saved-audio recovery resets;
    // their results belong to the interruption in which the request was taken.
    bool takeInputRecoveryRequest();
    void inputRecoveryFinished(const RecorderResult&);
    void inputFormatChanged(double inputStreamRate, int availableInputChannels) noexcept;

    // Exactly one audio producer. No allocation, filesystem I/O, encoding, or blocking locks.
    // Call while idle as well, for selected-channel metering.
    void processInput(const float* const* inputs, int inputChannels, int frames) noexcept;
    void inputLost() noexcept; // Safe from an input-device callback; preserves queued captured audio.
    void inputDiscontinuity(uint64_t count = 1) noexcept; // Xrun occurrences, never an exact lost-sample count.

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
