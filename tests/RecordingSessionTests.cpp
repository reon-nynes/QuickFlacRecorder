// SPDX-License-Identifier: AGPL-3.0-only
#include "RecordingSession.h"
#include <iostream>
#include <stdexcept>
#include <cmath>
#include <vector>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
struct Folder
{
    juce::File file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                         .getChildFile("qfr-tests-" + juce::Uuid().toString());
    Folder() { require(file.createDirectory().wasOk(), "create test folder"); }
    ~Folder() { file.deleteRecursively(); }
};
qfr::RecordingSession::Configuration config(const Folder& folder, juce::String prefix = "take")
{
    return { folder.file, prefix, 48000.0, 0, -1 };
}
void previewUsesHighestOccupiedCounter()
{
    Folder folder;
    qfr::RecordingSession session;
    require(session.configure(config(folder)).wasOk(), "valid session configuration");
    require(session.snapshot().nextFile.getFileName() == "take_01.flac", "first counter is 01");
    require(folder.file.getChildFile("take_02.flac").replaceWithText("existing"), "seed final recording");
    require(folder.file.getChildFile("take_99.partial.flac").replaceWithText("incomplete"), "seed incomplete recording");
    require(folder.file.getChildFile("other_999.flac").replaceWithText("other"), "seed unrelated prefix");
    require(folder.file.getChildFile("take_extra_999.flac").replaceWithText("other"), "seed longer prefix");
    require(session.refreshPreview().wasOk(), "refresh preview");
    require(session.snapshot().nextFile.getFileName() == "take_100.flac", "highest occupied counter plus one, exact prefix and 99 to 100");
    Folder second;
    require(session.configure(config(second, "new")).wasOk(), "change folder and prefix");
    require(session.snapshot().nextFile.getFileName() == "new_01.flac", "preview updates for folder and prefix");
}
void rejectsUnsafeConfigurations()
{
    Folder folder;
    qfr::RecordingSession session;
    for (auto prefix : { "", "../take", "a/b", "a\\b", "a:b", "take.", "take ", "CON", "nul.txt", "a?b", "a*b", "a|b", "a<b", "a\"b" })
    {
        require(session.configure(config(folder, prefix)).failed(), "unsafe prefix is rejected");
        require(session.snapshot().nextFile == juce::File(), "unsafe configuration clears filename preview");
        require(session.start().failed(), "unsafe configuration cannot start");
    }
    auto missing = config(folder);
    missing.targetFolder = folder.file.getChildFile("does-not-exist");
    require(session.configure(missing).failed(), "missing target folder is rejected");
    require(!missing.targetFolder.exists(), "missing target folder was not created");
    auto stereo = config(folder);
    stereo.rightInputChannel = stereo.leftInputChannel;
    require(session.configure(stereo).failed(), "stereo pair requires distinct input channels");
    stereo = config(folder);
    stereo.inputStreamRate = 0;
    require(session.configure(stereo).failed(), "unopened input stream rate is rejected");
}
std::unique_ptr<juce::AudioFormatReader> decode(const juce::File& file)
{
    juce::FlacAudioFormat flac;
    auto stream = file.createInputStream();
    require(stream != nullptr, "saved recording can be opened");
    auto reader = std::unique_ptr<juce::AudioFormatReader>(flac.createReaderFor(stream.release(), true));
    require(reader != nullptr, "saved recording is valid FLAC");
    return reader;
}
void savesSelectedMonoAndDrainsBeforePromotion()
{
    Folder folder;
    qfr::RecordingSession session;
    auto settings = config(folder);
    settings.inputStreamRate = 44100;
    settings.leftInputChannel = 2;
    require(session.configure(settings).wasOk(), "mono configuration");
    require(session.start().wasOk(), "start recording");
    require(session.snapshot().recordingFile.getFileName() == "take_01.partial.flac", "active recording is incomplete");
    require(!folder.file.getChildFile("take_01.flac").exists(), "no final filename during capture");
    require(session.configure(config(folder, "changed")).failed(), "configuration locks during capture");
    std::vector<float> unused(5000, .875f), selected(5000, .25f);
    selected[0] = 0; selected[1] = -.5f; selected[2] = 1; selected[3] = -1;
    const float* inputs[] { unused.data(), unused.data(), selected.data(), unused.data() };
    session.processInput(inputs, 4, 5000);
    session.requestStop();
    session.waitForStop();
    auto outcome = session.snapshot();
    require(outcome.state == qfr::RecordingSession::State::succeeded, "stop finalizes successfully");
    require(!outcome.incomplete && outcome.recordingFile.getFileName() == "take_01.flac", "successful finalization promotes filename");
    require(!folder.file.getChildFile("take_01.partial.flac").exists(), "partial filename removed after promotion");
    auto reader = decode(outcome.recordingFile);
    require(reader->sampleRate == 44100 && reader->numChannels == 1 && reader->bitsPerSample == 16, "actual rate and selected mono, 16-bit metadata");
    require(reader->lengthInSamples == 5000, "stop drains all queued frames");
    juce::AudioBuffer<float> audio(1, 5000);
    require(reader->read(&audio, 0, 5000, 0, true, false), "decode mono recording");
    const float expected[] { 0, -.5f, 32767.f / 32768.f, -1, .25f };
    for (int i = 0; i < 5; ++i)
        require(std::abs(audio.getSample(0, i) - expected[i]) < 1.f / 32768.f, "known mono samples preserve 16-bit quantization without processing");
    require(session.start().wasOk(), "start another take after finalization");
    require(session.snapshot().recordingFile.getFileName() == "take_02.partial.flac", "repeated take retains prefix and advances counter");
    session.requestStop(); session.waitForStop();
    require(session.snapshot().state == qfr::RecordingSession::State::succeeded, "zero-frame take finalizes as valid FLAC");
    require(decode(session.snapshot().recordingFile)->lengthInSamples == 0, "zero-frame FLAC is valid");
}
}
int main()
{
    try
    {
        previewUsesHighestOccupiedCounter();
        std::cout << "PASS filename preview\n";
        rejectsUnsafeConfigurations();
        std::cout << "PASS configuration validation\n";
        savesSelectedMonoAndDrainsBeforePromotion();
        std::cout << "PASS selected mono FLAC and stop/drain/promotion\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
