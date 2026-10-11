// SPDX-License-Identifier: AGPL-3.0-only
#include "InputController.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include <iostream>

namespace
{
void print(const juce::String& value) { std::cout << value.toStdString() << std::endl; }

int inspect(const juce::File& file, double expectedRate = 0, int expectedChannels = 0)
{
    juce::FlacAudioFormat flac;
    std::unique_ptr<juce::AudioFormatReader> reader(flac.createReaderFor(file.createInputStream().release(), true));
    if (! reader || reader->lengthInSamples <= 0 || reader->bitsPerSample != 16
        || (expectedRate > 0 && reader->sampleRate != expectedRate)
        || (expectedChannels > 0 && static_cast<int>(reader->numChannels) != expectedChannels))
    {
        print("ERROR: The saved FLAC has invalid metadata or contains no captured frames.");
        return 2;
    }
    double squareSum = 0;
    float peak = 0;
    juce::int64 sampleCount = 0;
    juce::AudioBuffer<float> buffer(static_cast<int>(reader->numChannels), 32768);
    for (juce::int64 offset = 0; offset < reader->lengthInSamples; offset += buffer.getNumSamples())
    {
        const auto frames = static_cast<int>(juce::jmin<juce::int64>(buffer.getNumSamples(), reader->lengthInSamples - offset));
        if (! reader->read(&buffer, 0, frames, offset, true, true))
        {
            print("ERROR: Cannot decode the saved FLAC.");
            return 2;
        }
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            for (int frame = 0; frame < frames; ++frame)
            {
                const auto value = buffer.getSample(channel, frame);
                peak = juce::jmax(peak, std::abs(value));
                squareSum += static_cast<double>(value) * value;
                ++sampleCount;
            }
    }
    print("FLAC " + file.getFullPathName());
    print("rate=" + juce::String(reader->sampleRate, 0)
        + " bits=" + juce::String(reader->bitsPerSample)
        + " channels=" + juce::String(reader->numChannels)
        + " frames=" + juce::String(reader->lengthInSamples)
        + " duration=" + juce::String(static_cast<double>(reader->lengthInSamples) / reader->sampleRate, 3));
    print("decoded_peak_dbfs=" + juce::String(juce::Decibels::gainToDecibels(peak, -160.0f), 2)
        + " decoded_rms_dbfs=" + juce::String(juce::Decibels::gainToDecibels(std::sqrt(squareSum / static_cast<double>(sampleCount)), -160.0), 2));
    if (peak <= 0.001f)
    {
        print("NO QUALIFYING SIGNAL: peak is at or below -60 dBFS. Check microphone routing and repeat while speaking.");
        return 3;
    }
    print("SIGNAL OBSERVED: real captured samples exceed -60 dBFS. Listen to the file to confirm the intended microphone.");
    return 0;
}
}

int main(int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI gui;
    juce::StringArray args;
    for (int i = 1; i < argc; ++i) args.add(juce::String::fromUTF8(argv[i]));
    const auto value = [&args](const juce::String& name, const juce::String& fallback = {})
    {
        const auto index = args.indexOf(name);
        return index >= 0 && index + 1 < args.size() ? args[index + 1] : fallback;
    };
    if (args.contains("--inspect")) return inspect(juce::File(value("--inspect")));

    qfr::RecordingSession session;
    qfr::InputController input(session);
    if (args.contains("--list-inputs"))
    {
        for (const auto& driver : input.drivers())
        {
            print("Audio driver: " + driver);
            for (const auto& device : input.devices(driver))
                print("  Input device: " + device + (device == input.defaultDevice(driver) ? " [system default]" : ""));
        }
        return 0;
    }
    if (! args.contains("--record-check") || value("--folder").isEmpty() || value("--input").isEmpty())
    {
        print("RecorderInputCheck --list-inputs");
        print("RecorderInputCheck --record-check --folder <existing folder> --input <exact name> [--driver <exact driver>] [--channel 1] [--right-channel 2] [--duration 5] [--prefix <name>]");
        print("RecorderInputCheck --inspect <saved FLAC>");
        return 1;
    }
    const auto driver = value("--driver", input.defaultDriver());
    const auto device = value("--input");
    const auto seconds = value("--duration", "5").getDoubleValue();
    const auto left = value("--channel", "1").getIntValue() - 1;
    const auto right = args.contains("--right-channel") ? value("--right-channel").getIntValue() - 1 : -1;
    if (! std::isfinite(seconds) || seconds <= 0 || seconds > 3600 || left < 0
        || (args.contains("--right-channel") && right < 0))
    {
        print("ERROR: duration must be greater than zero and at most 3600 seconds; channel numbers start at 1.");
        return 1;
    }
    if (! juce::RuntimePermissions::isGranted(juce::RuntimePermissions::recordAudio))
    {
        print("ERROR: microphone access is unavailable. Grant access to QuickFlacRecorder in system privacy settings first.");
        return 2;
    }
    const auto opened = input.open(driver, device);
    if (opened.failed()) { print("ERROR: " + opened.getErrorMessage()); return 2; }
    const auto channels = input.channels();
    if (left >= channels.size() || right >= channels.size())
    {
        print("ERROR: the selected channel is not available on this input device.");
        return 1;
    }
    qfr::RecordingSession::Configuration config;
    config.targetFolder = juce::File(value("--folder"));
    config.recordingPrefix = value("--prefix", "microphone-check_" + juce::Time::getCurrentTime().formatted("%Y-%m-%d_%H-%M-%S"));
    config.inputStreamRate = input.sampleRate();
    config.leftInputChannel = left;
    config.rightInputChannel = right;
    const auto configured = session.configure(config);
    if (configured.failed()) { print("ERROR: " + configured.getErrorMessage()); return 2; }
    print("driver=" + driver + " input=" + device + " left_channel=" + juce::String(left + 1)
          + " (" + channels[left] + ")" + (right >= 0 ? " right_channel=" + juce::String(right + 1) + " (" + channels[right] + ")" : ""));
    print("opened_input_stream_rate=" + juce::String(input.sampleRate(), 0) + " bits=16 duration_requested=" + juce::String(seconds));
    const auto started = session.start();
    if (started.failed()) { print("ERROR: " + started.getErrorMessage()); return 2; }
    print("RECORDING: speak into the selected microphone now.");
    const auto began = juce::Time::getMillisecondCounterHiRes();
    while (juce::Time::getMillisecondCounterHiRes() - began < seconds * 1000
        && session.snapshot().state == qfr::RecordingSession::State::recording)
    {
        if (input.pollError().isNotEmpty()) break;
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    }
    session.requestStop();
    input.close();
    session.waitForStop();
    const auto result = session.snapshot();
    if (result.state != qfr::RecordingSession::State::succeeded)
    {
        print("ERROR: " + result.message + " file=" + result.recordingFile.getFullPathName());
        return 2;
    }
    return inspect(result.recordingFile, config.inputStreamRate, right >= 0 ? 2 : 1);
}
