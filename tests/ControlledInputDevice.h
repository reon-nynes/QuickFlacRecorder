// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <array>
#include <utility>

// Control the external JUCE device boundary while using the production input controller.
class ControlledInputType final : public juce::AudioIODeviceType
{
public:
    ControlledInputType() : AudioIODeviceType("Controlled {driver}") {}
    void scanForDevices() override {}
    juce::StringArray getDeviceNames(bool = false) const override { return available ? juce::StringArray { deviceName } : juce::StringArray {}; }
    int getDefaultDeviceIndex(bool) const override { return 0; }
    int getIndexOfDevice(juce::AudioIODevice*, bool) const override { return 0; }
    bool hasSeparateInputsAndOutputs() const override { return true; }
    juce::AudioIODevice* createDevice(const juce::String&, const juce::String&) override { return new Device(*this); }

    void sendAudio()
    {
        if (callback == nullptr) return;
        std::array<float, 512> samples;
        samples.fill(0.25f);
        const float* channels[] { samples.data() };
        callback->audioDeviceIOCallbackWithContext(channels, 1, nullptr, 0, 512, {});
    }
    void disconnect()
    {
        available = false;
        if (callback != nullptr) callback->audioDeviceError("Original native input {diagnostic}");
        callDeviceChangeListeners();
    }
    void reconnect()
    {
        available = true;
        callDeviceChangeListeners();
    }
    const juce::String deviceName = juce::String::fromUTF8("Input 设备 {name}");

private:
    class Device final : public juce::AudioIODevice
    {
    public:
        explicit Device(ControlledInputType& type) : AudioIODevice(type.deviceName, type.getTypeName()), owner(type) {}
        ~Device() override { stop(); }
        juce::StringArray getOutputChannelNames() override { return {}; }
        juce::StringArray getInputChannelNames() override { return { "Channel 1" }; }
        juce::Array<double> getAvailableSampleRates() override { return { 48000 }; }
        juce::Array<int> getAvailableBufferSizes() override { return { 512 }; }
        int getDefaultBufferSize() override { return 512; }
        juce::String open(const juce::BigInteger&, const juce::BigInteger&, double, int) override { opened = true; return {}; }
        void close() override { stop(); opened = false; }
        bool isOpen() override { return opened && owner.available; }
        void start(juce::AudioIODeviceCallback* value) override { owner.callback = value; value->audioDeviceAboutToStart(this); }
        void stop() override
        {
            if (auto* value = std::exchange(owner.callback, nullptr)) value->audioDeviceStopped();
        }
        bool isPlaying() override { return isOpen() && owner.callback != nullptr; }
        juce::String getLastError() override { return {}; }
        int getCurrentBufferSizeSamples() override { return 512; }
        double getCurrentSampleRate() override { return 48000; }
        int getCurrentBitDepth() override { return 16; }
        juce::BigInteger getActiveOutputChannels() const override { return {}; }
        juce::BigInteger getActiveInputChannels() const override { return juce::BigInteger(1); }
        int getOutputLatencyInSamples() override { return 0; }
        int getInputLatencyInSamples() override { return 0; }
    private:
        ControlledInputType& owner;
        bool opened = false;
    };
    bool available = true;
    juce::AudioIODeviceCallback* callback = nullptr;
};
