// SPDX-License-Identifier: AGPL-3.0-only
#include "InputController.h"
#include <cmath>

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
    if (recordingChannelNames.isEmpty() && session.snapshot().controlsLocked()) recordingChannelNames = channelNames;
    inputVerified = false;
    intentionalStop.store(true);
    if (device)
    {
        device->stop();
        callbackDevice.store(nullptr);
        device->close();
        device.reset();
    }
    started.store(false);
    lastXRunCount.store(-1);
    streamRate.store(0);
    channelNames.clear();
    drainDiagnostics();
}

RecorderResult InputController::open(const juce::String& driverName, const juce::String& deviceName)
{
    const auto snapshot = session.snapshot();
    if (!snapshot.controlsLocked()) recordingChannelNames.clear();
    else if (recordingChannelNames.isEmpty()) recordingChannelNames = channelNames;
    close();
    deviceLost.store(false);
    for (auto& diagnostic : diagnostics)
    {
        diagnostic.detail.clear(); diagnostic.occurrences = 0;
        diagnostic.state = DiagnosticState::empty;
    }
    diagnosticOverflow = 0;
    selectedDriver = driverName;
    selectedDevice = deviceName;
    auto* type = findType(driverName);
    if (type == nullptr)
        return { ErrorCode::driverUnavailable, {}, driverName, deviceName };
    if (deviceName.isEmpty() || ! type->getDeviceNames(true).contains(deviceName))
        return { ErrorCode::deviceUnavailable, {}, driverName, deviceName };

    device.reset(type->createDevice(type->hasSeparateInputsAndOutputs() ? juce::String() : deviceName,
                                   deviceName));
    if (! device)
        return { ErrorCode::deviceCreateFailed, {}, driverName, deviceName };
    channelNames = device->getInputChannelNames();
    inputChannelCount = channelNames.size();
    if (channelNames.isEmpty())
    {
        close();
        return { ErrorCode::noInputChannels, {}, driverName, deviceName };
    }
    if (!selectedChannelsMatch(channelNames))
    {
        close();
        return { snapshot.rightInputChannel < 0 ? ErrorCode::monoChannelMissing : ErrorCode::stereoChannelsMissing };
    }

    juce::BigInteger inputs;
    inputs.setRange(0, channelNames.size(), true);
    // ASIO exposes the hardware's existing rate before open; zero would instead
    // choose its first supported rate. Other drivers can request their default with zero.
    rateBeforeOpen = device->getCurrentSampleRate();
    const auto requestedRate = std::isfinite(rateBeforeOpen) && rateBeforeOpen > 0 ? rateBeforeOpen : 0.0;
    const auto error = device->open(inputs, {}, requestedRate, device->getDefaultBufferSize());
    if (error.isNotEmpty())
    {
        close();
        return { ErrorCode::deviceOpenFailed, error, driverName, deviceName };
    }
    streamRate.store(device->getCurrentSampleRate());
    intentionalStop.store(false);
    inputVerified = true;
    device->start(this);
    if (! device->isPlaying())
    {
        close();
        return { ErrorCode::deviceStartFailed, firstDiagnostic(), driverName, deviceName };
    }
    return {};
}

bool InputController::selectedChannelsMatch(const juce::StringArray& current) const
{
    if (recordingChannelNames.isEmpty()) return true;
    const auto snapshot = session.snapshot();
    const auto matches = [&](int channel)
    {
        return channel >= 0 && channel < recordingChannelNames.size() && channel < current.size()
            && recordingChannelNames[channel] == current[channel];
    };
    return matches(snapshot.leftInputChannel) && (snapshot.rightInputChannel < 0 || matches(snapshot.rightInputChannel));
}

bool InputController::isAvailable() const
{
    return device && inputVerified && ! deviceLost.load() && device->isOpen() && device->isPlaying();
}
juce::String InputController::firstDiagnostic() const
{
    const auto& first = diagnostics.front();
    return first.state.load(std::memory_order_acquire) == DiagnosticState::ready ? first.detail : juce::String();
}
void InputController::drainDiagnostics()
{
    for (auto& diagnostic : diagnostics)
    {
        if (diagnostic.state.load(std::memory_order_acquire) != DiagnosticState::ready) break;
        const auto occurrences = diagnostic.occurrences.exchange(0);
        if (occurrences != 0) session.retainInputDiagnostic(diagnostic.detail, occurrences);
    }
    const auto overflow = diagnosticOverflow.exchange(0);
    if (overflow != 0) session.retainInputDiagnostic("Native input diagnostic buffer full; additional distinct callback messages omitted", overflow);
}

RecorderResult InputController::pollError()
{
    if (device && device->isPlaying() && !inputVerified)
    {
        const auto current = device->getInputChannelNames();
        if (recordingChannelNames.isEmpty()) recordingChannelNames = channelNames;
        if (selectedChannelsMatch(current)) { channelNames = current; inputChannelCount = current.size(); inputVerified = true; }
        else { deviceLost = true; session.inputLost(); }
    }
    if (device && (! device->isOpen() || ! device->isPlaying()))
    {
        deviceLost.store(true);
        session.inputLost();
    }
    RecorderResult result;
    if (deviceLost.load())
    {
        drainDiagnostics();
        result = { ErrorCode::deviceLost, firstDiagnostic(), selectedDriver, selectedDevice };
    }
    if (session.takeInputRecoveryRequest())
    {
        const auto driverName = selectedDriver, deviceName = selectedDevice;
        rescan();
        result = open(driverName, deviceName);
        session.inputRecoveryFinished(result);
    }
    return result;
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
    if (auto* openedDevice = callbackDevice.load())
    {
        const auto count = openedDevice->getXRunCount();
        const auto previous = lastXRunCount.exchange(count);
        // Historical xruns do not belong to a later recording. An advancing count
        // documents missing samples while this callback's valid audio continues.
        if (count >= 0 && previous >= 0 && count > previous)
            session.inputDiscontinuity(static_cast<uint64_t>(count - previous));
    }
    if (inputVerified) session.processInput(inputs, inputChannels, frames);
}

void InputController::audioDeviceAboutToStart(juce::AudioIODevice* openedDevice)
{
    const auto newRate = openedDevice->getCurrentSampleRate();
    if (started.exchange(true))
    {
        inputVerified = false;
        session.inputLost();
    }
    streamRate.store(newRate);
    session.inputFormatChanged(newRate, inputChannelCount);
    callbackDevice.store(openedDevice);
    lastXRunCount.store(openedDevice->getXRunCount());
}

void InputController::audioDeviceStopped()
{
    callbackDevice.store(nullptr);
    if (! intentionalStop.load())
    {
        deviceLost.store(true);
        session.inputLost();
    }
}

void InputController::audioDeviceError(const juce::String& detail)
{
    bool published = false;
    for (auto& diagnostic : diagnostics)
    {
        auto current = diagnostic.state.load(std::memory_order_acquire);
        if (current == DiagnosticState::ready && diagnostic.detail == detail)
        {
            diagnostic.occurrences.fetch_add(1);
            published = true;
            break;
        }
        if (current == DiagnosticState::empty
            && diagnostic.state.compare_exchange_strong(current, DiagnosticState::publishing))
        {
            // JUCE String shares its immutable text. Publish without allocating
            // text, blocking, or performing I/O on the native callback thread.
            diagnostic.detail = detail;
            diagnostic.occurrences = 1;
            diagnostic.state.store(DiagnosticState::ready, std::memory_order_release);
            published = true;
            break;
        }
    }
    if (!published) diagnosticOverflow.fetch_add(1);
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
