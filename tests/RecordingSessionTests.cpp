// SPDX-License-Identifier: AGPL-3.0-only
#include "RecordingSession.h"
#include <iostream>
#include <stdexcept>
#include <cmath>
#include <vector>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <barrier>
#if JUCE_WINDOWS
#define NOMINMAX
#include <windows.h>
#include <sddl.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

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
void metersAndSavesSelectedStereoInOrder()
{
    Folder folder;
    qfr::RecordingSession session;
    auto settings = config(folder);
    settings.leftInputChannel = 3; settings.rightInputChannel = 1;
    require(session.configure(settings).wasOk(), "stereo configuration");
    std::array<float, 256> unused, left, right;
    unused.fill(1); left.fill(.5f); right.fill(-.25f);
    const float* inputs[] { unused.data(), right.data(), unused.data(), left.data() };
    session.processInput(inputs, 4, 256);
    auto meter = session.readLevels();
    require(meter.channels == 2 && meter.peak[0] == .5f && meter.peak[1] == .25f, "idle meters follow selected stereo channels");
    require(!meter.clipped[0] && !meter.clipped[1], "unselected clipping does not light selected-channel indicators");
    require(session.readLevels().peak[0] == 0, "meter peaks are consumed by the UI");
    require(session.start().wasOk(), "start stereo");
    left[0] = -1;
    session.processInput(inputs, 4, 256);
    meter = session.readLevels();
    require(meter.clipped[0] && !meter.clipped[1] && meter.peak[0] == 1, "selected-channel clipping during recording");
    session.requestStop(); session.waitForStop();
    auto outcome = session.snapshot();
    require(outcome.state == qfr::RecordingSession::State::succeeded, "stereo saved successfully");
    auto reader = decode(outcome.recordingFile);
    require(reader->numChannels == 2 && reader->lengthInSamples == 256 && reader->sampleRate == 48000 && reader->bitsPerSample == 16, "stereo FLAC metadata");
    juce::AudioBuffer<float> audio(2, 256);
    require(reader->read(&audio, 0, 256, 0, true, true), "decode stereo");
    require(audio.getSample(0, 0) == -1 && std::abs(audio.getSample(0, 1) - .5f) < 1.f / 32768.f
            && std::abs(audio.getSample(1, 0) + .25f) < 1.f / 32768.f, "stereo selection order and unchanged quantized signals");
}
struct DiskControl
{
    std::atomic<bool> failWrites { false }, failMetadata { false }, corruptMetadata { false };
    std::atomic<juce::int64> flushedBytes { 0 };
    std::atomic<bool> failNextFlush { false };
    std::mutex mutex;
    std::condition_variable changed;
    bool blockFinalization = false, enteredFinalization = false, release = false;
};
class ControlledDisk final : public juce::OutputStream
{
public:
    ControlledDisk(std::unique_ptr<juce::OutputStream> outputIn, std::shared_ptr<DiskControl> controlIn)
        : output(std::move(outputIn)), control(std::move(controlIn)) {}
    void flush() override
    {
        if (control->failNextFlush.exchange(false)) throw std::runtime_error("disk flush refused");
        output->flush();
        control->flushedBytes = output->getPosition();
        control->changed.notify_all();
    }
    juce::int64 getPosition() override { return output->getPosition(); }
    bool setPosition(juce::int64 position) override
    {
        if (position == 4)
        {
            std::unique_lock lock(control->mutex);
            control->enteredFinalization = true;
            control->changed.notify_all();
            if (control->blockFinalization) control->changed.wait(lock, [&] { return control->release; });
            if (control->failMetadata) control->failWrites = true;
        }
        return output->setPosition(position);
    }
    bool write(const void* data, size_t bytes) override
    {
        if (control->failWrites) return false;
        if (control->corruptMetadata && output->getPosition() == 8 && bytes == 34)
        {
            // FLAC STREAMINFO starts at byte 8. Its channel count is stored in byte 12, bits 1..3.
            std::vector<unsigned char> corrupted(static_cast<const unsigned char*>(data), static_cast<const unsigned char*>(data) + bytes);
            corrupted[12] = static_cast<unsigned char>((corrupted[12] & ~0x0e) | 0x02);
            return output->write(corrupted.data(), bytes);
        }
        return output->write(data, bytes);
    }
private:
    std::unique_ptr<juce::OutputStream> output;
    std::shared_ptr<DiskControl> control;
};
qfr::RecordingSession::Options diskOptions(std::shared_ptr<DiskControl> control, size_t capacity = 262144)
{
    return { capacity, [control](std::unique_ptr<juce::OutputStream> output) { return std::make_unique<ControlledDisk>(std::move(output), control); } };
}
void periodicallyFlushesGrowingPartialAudioWhileTheTakeIsActive()
{
    Folder folder;
    auto disk = std::make_shared<DiskControl>();
    auto options = diskOptions(disk);
    options.diskFlushInterval = std::chrono::milliseconds(20);
    qfr::RecordingSession session(options);
    require(session.configure(config(folder)).wasOk() && session.start().wasOk(), "start periodically flushed recording");
    const auto partial = session.snapshot().recordingFile;
    const auto initialBytes = partial.getSize();
    std::array<float, 16384> source; source.fill(.25f);
    const float* inputs[] { source.data() };
    const auto waitForFlushedGrowth = [&](juce::int64 previousBytes)
    {
        std::unique_lock lock(disk->mutex);
        return disk->changed.wait_for(lock, std::chrono::seconds(3), [&] { return disk->flushedBytes > previousBytes; });
    };
    session.processInput(inputs, 1, 16384);
    const bool firstFlush = waitForFlushedGrowth(initialBytes);
    const auto firstBytes = partial.getSize();
    const bool stillRecording = session.snapshot().state == qfr::RecordingSession::State::recording;
    session.processInput(inputs, 1, 16384);
    const bool secondFlush = firstFlush && waitForFlushedGrowth(firstBytes);
    const auto secondBytes = partial.getSize();
    session.requestStop(); session.waitForStop();
    require(firstFlush && stillRecording && firstBytes > initialBytes && secondFlush && secondBytes > firstBytes,
            "periodic output flush exposes growing encoded partial audio while capture remains active");
    const auto outcome = session.snapshot();
    require(outcome.state == qfr::RecordingSession::State::succeeded && !partial.existsAsFile()
            && decode(outcome.recordingFile)->lengthInSamples == 32768,
            "periodic disk flushing preserves the continuous FLAC and normal stop finalization");
}
void periodicDiskFlushFailureStopsAndRetainsAnIncompleteRecording()
{
    Folder folder;
    auto disk = std::make_shared<DiskControl>();
    auto options = diskOptions(disk);
    options.diskFlushInterval = std::chrono::milliseconds(20);
    qfr::RecordingSession session(options);
    require(session.configure(config(folder)).wasOk() && session.start().wasOk(), "start flush-failure recording");
    const auto partial = session.snapshot().recordingFile;
    std::array<float, 16384> source; source.fill(.25f);
    const float* inputs[] { source.data() };
    session.processInput(inputs, 1, 16384);
    disk->failNextFlush = true;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (session.snapshot().controlsLocked() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    session.requestStop(); session.waitForStop();
    const auto outcome = session.snapshot();
    require(outcome.state == qfr::RecordingSession::State::failed && outcome.incomplete
            && outcome.message.contains("flush") && outcome.message.contains("incomplete")
            && partial.existsAsFile() && !folder.file.getChildFile("take_01.flac").exists(),
            "periodic disk flush failure reports the failed flush and retains an unpromoted incomplete recording");
}
void failedInputPreservesFinalizableAudioAndReportsIncompleteFinalization()
{
    Folder folder;
    auto disk = std::make_shared<DiskControl>();
    qfr::RecordingSession session(diskOptions(disk));
    require(session.configure(config(folder)).wasOk() && session.start().wasOk(), "start controlled input-loss recording");
    std::array<float, 512> source; source.fill(.25f);
    const float* inputs[] { source.data() };
    session.processInput(inputs, 1, 512);
    session.inputLost(); session.waitForStop();
    auto outcome = session.snapshot();
    require(outcome.state == qfr::RecordingSession::State::failed && !outcome.incomplete && outcome.message.contains("lost"), "input loss reports a failed take with finalized captured audio");
    require(decode(outcome.recordingFile)->lengthInSamples == 512, "input loss drains captured frames");
    disk->failMetadata = true;
    require(session.start().wasOk(), "second input-loss recording");
    session.processInput(inputs, 1, 512);
    session.inputLost(); session.waitForStop();
    outcome = session.snapshot();
    require(outcome.state == qfr::RecordingSession::State::failed && outcome.incomplete && outcome.recordingFile.getFileName() == "take_02.partial.flac", "failed finalization retains partial file");
    require(outcome.message.contains("incomplete") && !outcome.message.contains("was finalized"), "compound input-loss/finalization failure reports retained incomplete audio");
    require(!folder.file.getChildFile("take_02.flac").exists(), "failed finalization is never promoted");
    auto failureMessage = outcome.message;
    require(session.refreshPreview().wasOk(), "refresh next filename after failure");
    require(session.snapshot().message == failureMessage && session.snapshot().state == qfr::RecordingSession::State::failed,
            "passive preview refresh preserves failure explanation");
    disk->failWrites = false; disk->failMetadata = false; disk->corruptMetadata = true;
    require(session.start().wasOk(), "start input-loss recording with corrupt final metadata");
    session.processInput(inputs, 1, 512);
    session.inputLost(); session.waitForStop();
    outcome = session.snapshot();
    require(outcome.state == qfr::RecordingSession::State::failed && outcome.incomplete
            && outcome.recordingFile.getFileName() == "take_03.partial.flac" && !folder.file.getChildFile("take_03.flac").exists(),
            "metadata validation failure after input loss cannot promote an invalid FLAC");
}
void driverDiscontinuityFailsTheTakeAndPreservesAcceptedAudio()
{
    Folder folder;
    qfr::RecordingSession session;
    require(session.configure(config(folder)).wasOk(), "configure driver-discontinuity recording");
    session.inputDiscontinuity();
    require(session.snapshot().state == qfr::RecordingSession::State::idle,
            "historical idle driver discontinuities do not fail the next take");
    require(session.start().wasOk(), "start driver-discontinuity recording");
    std::array<float, 512> source; source.fill(.25f);
    const float* inputs[] { source.data() };
    session.processInput(inputs, 1, 512);
    session.inputDiscontinuity();
    session.processInput(inputs, 1, 512);
    session.requestStop(); session.waitForStop();
    const auto outcome = session.snapshot();
    require(outcome.state == qfr::RecordingSession::State::failed && !outcome.incomplete
            && outcome.message.contains("discontinuity") && outcome.message.contains("missing samples")
            && !outcome.message.contains("Saved") && !outcome.message.contains("lost"),
            "driver missing samples report a specific failed take with finalized captured audio");
    require(decode(outcome.recordingFile)->lengthInSamples == 512,
            "discontinuity drains accepted frames and rejects later potentially discontinuous audio");
}
void finalizationLocksAnotherTakeAndPromotionDoesNotReplace()
{
    Folder folder;
    auto disk = std::make_shared<DiskControl>();
    disk->blockFinalization = true;
    qfr::RecordingSession session(diskOptions(disk));
    require(session.configure(config(folder)).wasOk() && session.start().wasOk(), "start gated finalization");
    session.requestStop();
    bool entered;
    {
        std::unique_lock lock(disk->mutex);
        entered = disk->changed.wait_for(lock, std::chrono::seconds(3), [&] { return disk->enteredFinalization; });
    }
    auto state = session.snapshot();
    auto canStart = session.start();
    auto canChange = session.configure(config(folder, "changed"));
    auto partialExists = state.recordingFile.existsAsFile();
    auto finalExists = folder.file.getChildFile("take_01.flac").exists();
    require(folder.file.getChildFile("take_01.flac").replaceWithText("existing final recording"), "create late promotion conflict");
    {
        std::lock_guard lock(disk->mutex);
        disk->release = true;
    }
    disk->changed.notify_all();
    session.waitForStop();
    require(entered && state.state == qfr::RecordingSession::State::finalizing && partialExists && !finalExists, "partial file and locked finalizing state until FLAC finishes");
    require(canStart.failed() && canChange.failed(), "another take and configuration are unavailable during finalization");
    state = session.snapshot();
    require(state.state == qfr::RecordingSession::State::failed && state.incomplete && state.message.contains("incomplete"), "promotion conflict retains incomplete recording and reports failure");
    require(folder.file.getChildFile("take_01.flac").loadFileAsString() == "existing final recording", "promotion never replaces existing final recording");
    require(decode(state.recordingFile)->lengthInSamples == 0, "finalized partial is inspectable after promotion conflict");
}
void overflowAndWriterFailuresStopWithoutClaimingSuccess()
{
    Folder folder;
    qfr::RecordingSession session({ 512, {} });
    require(session.configure(config(folder)).wasOk() && session.start().wasOk(), "start bounded recording");
    std::array<float, 1024> source; source.fill(.25f);
    const float* inputs[] { source.data() };
    session.processInput(inputs, 1, 256);
    session.processInput(inputs, 1, 1024);
    session.processInput(inputs, 1, 256);
    session.waitForStop();
    auto state = session.snapshot();
    require(state.state == qfr::RecordingSession::State::failed && state.message.contains("overflow"), "buffer overflow stops and reports failed capture");
    require(!state.incomplete && decode(state.recordingFile)->lengthInSamples == 256, "overflow preserves accepted frames and does not silently accept subsequent input");
    auto disk = std::make_shared<DiskControl>();
    qfr::RecordingSession broken(diskOptions(disk));
    require(broken.configure(config(folder, "writer")).wasOk() && broken.start().wasOk(), "start controlled writer-failure recording");
    disk->failWrites = true;
    std::vector<float> larger(16000, .25f);
    const float* largerInputs[] { larger.data() };
    broken.processInput(largerInputs, 1, 16000);
    broken.requestStop(); broken.waitForStop();
    state = broken.snapshot();
    require(state.state == qfr::RecordingSession::State::failed && state.incomplete && state.recordingFile.existsAsFile(), "disk writer failure retains incomplete recording");
    require(!folder.file.getChildFile("writer_01.flac").exists() && state.message.contains("failed"), "writer failure cannot claim a successful save");
}
void concurrentReservationRechecksChangedFolderContents()
{
    Folder folder;
    qfr::RecordingSession first, second;
    require(first.configure(config(folder)).wasOk() && second.configure(config(folder)).wasOk(), "two previews");
    require(folder.file.getChildFile("take_01.flac").replaceWithText("original"), "folder changes after preview");
    std::barrier begin(3);
    auto firstResult = juce::Result::fail("not started"), secondResult = firstResult;
    std::thread a([&] { begin.arrive_and_wait(); firstResult = first.start(); });
    std::thread b([&] { begin.arrive_and_wait(); secondResult = second.start(); });
    begin.arrive_and_wait(); a.join(); b.join();
    auto firstFile = first.snapshot().recordingFile, secondFile = second.snapshot().recordingFile;
    first.requestStop(); second.requestStop(); first.waitForStop(); second.waitForStop();
    require(firstResult.wasOk() && secondResult.wasOk() && firstFile != secondFile, "simultaneous sessions atomically reserve distinct names");
    require(folder.file.getChildFile("take_02.flac").existsAsFile() && folder.file.getChildFile("take_03.flac").existsAsFile(), "reservation rechecks folder and advances occupied counters");
    require(folder.file.getChildFile("take_01.flac").loadFileAsString() == "original", "concurrent starts do not replace existing recording");
}
void rejectsAnUnwritableExistingFolder()
{
    Folder folder;
#if JUCE_WINDOWS
    DWORD needed = 0;
    auto path = folder.file.getFullPathName();
    GetFileSecurityW(path.toWideCharPointer(), DACL_SECURITY_INFORMATION, nullptr, 0, &needed);
    std::vector<unsigned char> original(needed);
    require(GetFileSecurityW(path.toWideCharPointer(), DACL_SECURITY_INFORMATION, original.data(), needed, &needed), "read folder permissions");
    PSECURITY_DESCRIPTOR denied = nullptr;
    require(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:(D;OICI;GW;;;WD)(A;OICI;FA;;;WD)", SDDL_REVISION_1, &denied, nullptr), "create denied write permissions");
    auto installed = SetFileSecurityW(path.toWideCharPointer(), DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, denied);
    LocalFree(denied);
    require(installed, "deny file creation in existing folder");
#else
    if (getuid() == 0) { std::cout << "SKIP unwritable-folder permissions for root user\n"; return; }
    require(::chmod(folder.file.getFullPathName().toRawUTF8(), 0555) == 0, "deny file creation in existing folder");
#endif
    qfr::RecordingSession session;
    auto configured = session.configure(config(folder));
    auto started = session.start();
    auto files = folder.file.findChildFiles(juce::File::findFiles, false).size();
#if JUCE_WINDOWS
    require(SetFileSecurityW(path.toWideCharPointer(), DACL_SECURITY_INFORMATION | UNPROTECTED_DACL_SECURITY_INFORMATION, original.data()), "restore folder permissions");
#else
    require(::chmod(folder.file.getFullPathName().toRawUTF8(), 0755) == 0, "restore folder permissions");
#endif
    require(configured.failed() && started.failed() && files == 0, "unwritable target folder is rejected without creating recordings");
}
void startupDiskExceptionsReportFailureAndRetainReservation()
{
    Folder folder;
    qfr::RecordingSession::Options options;
    options.outputAdapter = [](std::unique_ptr<juce::OutputStream>) -> std::unique_ptr<juce::OutputStream>
    { throw std::runtime_error("disk unavailable"); };
    qfr::RecordingSession session(options);
    require(session.configure(config(folder)).wasOk(), "configure startup failure");
    auto started = session.start();
    auto outcome = session.snapshot();
    require(started.failed() && outcome.state == qfr::RecordingSession::State::failed && outcome.incomplete
            && outcome.recordingFile.existsAsFile() && !outcome.controlsLocked(), "startup exception reports failure and retains incomplete reservation");
}
void stopRacesAnInputBlockAndDeviceLossWithoutPartialAcceptance()
{
    Folder folder;
    qfr::RecordingSession session;
    require(session.configure(config(folder)).wasOk(), "configure stop race");
    std::vector<float> source(131072, .25f);
    const float* inputs[] { source.data() };
    for (int take = 0; take < 8; ++take)
    {
        require(session.start().wasOk(), "start stop-race take");
        std::barrier begin(3);
        std::thread input([&] { begin.arrive_and_wait(); session.processInput(inputs, 1, 131072); });
        std::thread lost([&] { begin.arrive_and_wait(); session.inputLost(); });
        begin.arrive_and_wait(); session.requestStop();
        input.join(); lost.join(); session.waitForStop();
        auto outcome = session.snapshot();
        require(!outcome.controlsLocked() && !outcome.incomplete && outcome.recordingFile.existsAsFile(), "racing stop finishes and preserves finalizable audio");
        auto reader = decode(outcome.recordingFile);
        require(reader->lengthInSamples == 0 || reader->lengthInSamples == 131072, "stop accepts the whole in-flight input block or none of it");
        session.requestStop(); session.inputLost();
        require(session.snapshot().state == outcome.state && session.snapshot().message == outcome.message, "late stop/device notification cannot overwrite a terminal outcome");
    }
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
        metersAndSavesSelectedStereoInOrder();
        std::cout << "PASS selected stereo and idle/active meters\n";
        periodicallyFlushesGrowingPartialAudioWhileTheTakeIsActive();
        std::cout << "PASS periodic disk flush and growing active partial audio\n";
        periodicDiskFlushFailureStopsAndRetainsAnIncompleteRecording();
        std::cout << "PASS periodic disk flush failure and incomplete retention\n";
        failedInputPreservesFinalizableAudioAndReportsIncompleteFinalization();
        std::cout << "PASS input loss and incomplete finalization\n";
        driverDiscontinuityFailsTheTakeAndPreservesAcceptedAudio();
        std::cout << "PASS driver discontinuity and preserved audio\n";
        finalizationLocksAnotherTakeAndPromotionDoesNotReplace();
        std::cout << "PASS finalization locks and promotion conflict\n";
        overflowAndWriterFailuresStopWithoutClaimingSuccess();
        std::cout << "PASS bounded capture overflow and disk writer failure\n";
        concurrentReservationRechecksChangedFolderContents();
        std::cout << "PASS concurrent reservation and stale preview\n";
        rejectsAnUnwritableExistingFolder();
        std::cout << "PASS unwritable target folder\n";
        startupDiskExceptionsReportFailureAndRetainReservation();
        std::cout << "PASS startup disk exception\n";
        stopRacesAnInputBlockAndDeviceLossWithoutPartialAcceptance();
        std::cout << "PASS stop/input/device-loss race\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}

