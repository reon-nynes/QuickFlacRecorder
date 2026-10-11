// SPDX-License-Identifier: AGPL-3.0-only
#include "InputController.h"

namespace qfr
{
InputController::InputController(RecordingSession& recordingSession) : session(recordingSession)
{
    const auto add = [this](juce::AudioIODeviceType* type)
    {
        if (type != nullptr)
        {
            types.add(type);
            type->scanForDevices();
            type->addListener(this);
        }
    };
   #if JUCE_WINDOWS
    add(juce::AudioIODeviceType::createAudioIODeviceType_WASAPI(juce::WASAPIDeviceMode::shared));
    add(juce::AudioIODeviceType::createAudioIODeviceType_WASAPI(juce::WASAPIDeviceMode::exclusive));
    add(juce::AudioIODeviceType::createAudioIODeviceType_ASIO());
   #elif JUCE_MAC
    add(juce::AudioIODeviceType::createAudioIODeviceType_CoreAudio());
   #elif JUCE_LINUX
    add(juce::AudioIODeviceType::createAudioIODeviceType_ALSA());
    add(juce::AudioIODeviceType::createAudioIODeviceType_JACK());
   #endif
}

InputController::~InputController()
{
    onDevicesChanged = {};
    cancelPendingUpdate();
    close();
    for (auto* type : types)
        type->removeListener(this);
}

juce::AudioIODeviceType* InputController::findType(const juce::String& name) const
{
    for (auto* type : types)
        if (type->getTypeName() == name)
            return type;
    return nullptr;
}

juce::StringArray InputController::drivers() const
{
    juce::StringArray result;
    for (auto* type : types)
        result.add(type->getTypeName());
    return result;
}

juce::StringArray InputController::devices(const juce::String& driverName) const
{
    if (auto* type = findType(driverName))
        return type->getDeviceNames(true);
    return {};
}

juce::String InputController::defaultDriver() const
{
    // The first type is the platform's normal path (WASAPI shared/CoreAudio/ALSA).
    return types.isEmpty() ? juce::String() : types[0]->getTypeName();
}

juce::String InputController::defaultDevice(const juce::String& driverName) const
{
    if (auto* type = findType(driverName))
    {
        const auto names = type->getDeviceNames(true);
        return names[type->getDefaultDeviceIndex(true)];
    }
    return {};
}

void InputController::close()
{
    intentionalStop.store(true);
    if (device)
    {
        device->stop();
        device->close();
        device.reset();
    }
    started.store(false);
    streamRate.store(0);
    channelNames.clear();
}

juce::Result InputController::open(const juce::String& driverName, const juce::String& deviceName)
{
    close();
    deviceLost.store(false);
    selectedDriver = driverName;
    selectedDevice = deviceName;
    auto* type = findType(driverName);
    if (type == nullptr)
        return juce::Result::fail("Audio driver unavailable. Choose an available audio driver.");
    if (deviceName.isEmpty() || ! type->getDeviceNames(true).contains(deviceName))
        return juce::Result::fail("Input device unavailable. Choose an available input device.");

    device.reset(type->createDevice(type->hasSeparateInputsAndOutputs() ? juce::String() : deviceName,
                                   deviceName));
    if (! device)
        return juce::Result::fail("Cannot create this input. Choose another input device or audio driver.");
    channelNames = device->getInputChannelNames();
    if (channelNames.isEmpty())
    {
        close();
        return juce::Result::fail("This device has no input channels. Choose an input device.");
    }

    juce::BigInteger inputs;
    inputs.setRange(0, channelNames.size(), true);
    // Zero asks JUCE for the current/default stream rate. All device input channels
    // stay in their native order; no application resampling or processing is added.
    const auto error = device->open(inputs, {}, 0.0, device->getDefaultBufferSize());
    if (error.isNotEmpty())
    {
        close();
       #if JUCE_WINDOWS
        return juce::Result::fail("Cannot open input: " + error
            + ". Check Windows Settings > Privacy & security > Microphone, then reselect the input.");
       #elif JUCE_MAC
        return juce::Result::fail("Cannot open input: " + error
            + ". Allow microphone access in System Settings > Privacy & Security > Microphone.");
       #else
        return juce::Result::fail("Cannot open input: " + error
            + ". Check device access permissions and whether another app is using this input.");
       #endif
    }
    streamRate.store(device->getCurrentSampleRate());
    intentionalStop.store(false);
    device->start(this);
    if (! device->isPlaying())
    {
        close();
        return juce::Result::fail("Input did not start. Check device access and reselect the input.");
    }
    return juce::Result::ok();
}

bool InputController::isAvailable() const
{
    return device && ! deviceLost.load() && device->isOpen() && device->isPlaying();
}

juce::String InputController::pollError()
{
    if (deviceLost.load())
        return "Input stopped or disconnected. Check the connection and reselect the input device.";
    if (device && (! device->isOpen() || ! device->isPlaying()))
    {
        deviceLost.store(true);
        session.inputLost();
        return "Input is unavailable. Check the connection and reselect the input device.";
    }
    return {};
}

void InputController::rescan()
{
    for (auto* type : types)
        type->scanForDevices();
    if (selectedDevice.isNotEmpty() && ! devices(selectedDriver).contains(selectedDevice))
    {
        deviceLost.store(true);
        session.inputLost();
    }
}

void InputController::audioDeviceIOCallbackWithContext(const float* const* inputs, int inputChannels,
    float* const* outputs, int outputChannels, int frames, const juce::AudioIODeviceCallbackContext&)
{
    for (int channel = 0; channel < outputChannels; ++channel)
        if (outputs[channel] != nullptr)
            juce::FloatVectorOperations::clear(outputs[channel], frames);
    session.processInput(inputs, inputChannels, frames);
}

void InputController::audioDeviceAboutToStart(juce::AudioIODevice* openedDevice)
{
    const auto newRate = openedDevice->getCurrentSampleRate();
    if (started.exchange(true))
    {
        deviceLost.store(true);
        session.inputLost(); // An unsolicited restart can change rate or lose samples.
    }
    streamRate.store(newRate);
}

void InputController::audioDeviceStopped()
{
    if (! intentionalStop.load())
    {
        deviceLost.store(true);
        session.inputLost();
    }
}

void InputController::audioDeviceError(const juce::String&)
{
    deviceLost.store(true);
    session.inputLost();
}

void InputController::audioDeviceListChanged() { triggerAsyncUpdate(); }

void InputController::handleAsyncUpdate()
{
    rescan();
    if (onDevicesChanged)
        onDevicesChanged();
}
}
