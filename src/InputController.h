// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "RecordingSession.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <atomic>
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
    explicit InputController(RecordingSession&);
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
    std::atomic<unsigned> diagnosticState { 0 }; // Empty, callback publishing, ready.
    std::atomic<int> inputChannelCount { 0 };
    juce::String nativeDetail;
};
}
