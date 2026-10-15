// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "RecordingSession.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <atomic>
#include <array>
#include <functional>

namespace qfr
{
// The native UI and the hardware check share this input path. Input callbacks
// always contain the device's channels in order; the session selects the output.
class InputController final : private juce::AudioIODeviceCallback,
                              private juce::AudioIODeviceType::Listener,
                              private juce::AsyncUpdater
{
public:
    // An optional native device type replaces platform discovery at the device boundary.
    explicit InputController(RecordingSession&, std::unique_ptr<juce::AudioIODeviceType> = {});
    ~InputController() override;

    juce::StringArray drivers() const;
    juce::StringArray devices(const juce::String& driver) const;
    juce::String defaultDriver() const;
    juce::String defaultDevice(const juce::String& driver) const;
    RecorderResult open(const juce::String& driver, const juce::String& inputDevice);
    void close();
    void rescan();
    bool isAvailable() const;
    RecorderResult pollError();
    void audioDeviceError(const juce::String&) override; // Native driver diagnostic boundary; may run on its callback thread.

    juce::String driver() const { return selectedDriver; }
    juce::String inputDevice() const { return selectedDevice; }
    juce::StringArray channels() const { return channelNames; }
    double sampleRate() const noexcept { return streamRate.load(); }
    double sampleRateBeforeOpen() const noexcept { return rateBeforeOpen; }
    std::function<void()> onDevicesChanged;

private:
    juce::AudioIODeviceType* findType(const juce::String&) const;
    void audioDeviceIOCallbackWithContext(const float* const*, int, float* const*, int,
                                         int, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart(juce::AudioIODevice*) override;
    void audioDeviceStopped() override;
    void audioDeviceListChanged() override;
    void handleAsyncUpdate() override;

    RecordingSession& session;
    juce::OwnedArray<juce::AudioIODeviceType> types;
    std::unique_ptr<juce::AudioIODevice> device;
    juce::String selectedDriver, selectedDevice;
    juce::StringArray channelNames;
    juce::StringArray recordingChannelNames;
    bool selectedChannelsMatch(const juce::StringArray&) const;
    std::atomic<double> streamRate { 0 };
    double rateBeforeOpen = 0;
    std::atomic<juce::AudioIODevice*> callbackDevice { nullptr };
    std::atomic<int> lastXRunCount { -1 };
    std::atomic<bool> intentionalStop { true }, deviceLost { false }, started { false };
    std::atomic<bool> inputVerified { false };
    enum class DiagnosticState { empty, publishing, ready };
    struct InputDiagnostic
    {
        std::atomic<DiagnosticState> state { DiagnosticState::empty };
        std::atomic<uint64_t> occurrences { 0 };
        juce::String detail;
    };
    // Entries remain immutable until close() has stopped all native callbacks.
    // Repeated messages use one entry. Control-thread polling drains counts.
    std::array<InputDiagnostic, 32> diagnostics;
    std::atomic<uint64_t> diagnosticOverflow { 0 };
    juce::String firstDiagnostic() const;
    void drainDiagnostics();
    std::atomic<int> inputChannelCount { 0 };
};
}
