// SPDX-License-Identifier: AGPL-3.0-only
#include "RecordingSession.h"
#include "FolderWritePermission.h"
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
#include <cstdlib>
#include <cerrno>
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
    juce::File file = (std::getenv("QFR_TEST_TARGET_FOLDER") != nullptr
                         ? juce::File(juce::String::fromUTF8(std::getenv("QFR_TEST_TARGET_FOLDER")))
                         : juce::File::getSpecialLocation(juce::File::tempDirectory))
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
    require(session.snapshot().error.code == qfr::ErrorCode::folderMissing,
            "missing target folder has a stable result independent of wording");
    require(!missing.targetFolder.exists(), "missing target folder was not created");
    require(session.configure(config(folder)).wasOk(), "explicit folder correction recovers recording");
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
            && outcome.failureReason == qfr::RecordingSession::FailureReason::diskFlush
            && outcome.finalization == qfr::RecordingSession::FinalizationOutcome::diskFlushFailed
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
    session.retainInputDiagnostic(juce::String::fromUTF8("设备{error}"));
    auto outcome = session.snapshot();
    require(outcome.state == qfr::RecordingSession::State::failed && !outcome.incomplete && outcome.failureReason == qfr::RecordingSession::FailureReason::inputLost
            && outcome.finalization == qfr::RecordingSession::FinalizationOutcome::finalized, "input loss reports a failed take with finalized captured audio");
    require(decode(outcome.recordingFile)->lengthInSamples == 512, "input loss drains captured frames");
    require(outcome.inputDetail == juce::String::fromUTF8("设备{error}"), "native input detail survives independently of translated presentation");
    session.retainInputDiagnostic("late notification");
    require(session.snapshot().inputDetail == outcome.inputDetail, "later input diagnostics do not replace the first terminal cause");
    disk->failMetadata = true;
    require(session.start().wasOk(), "second input-loss recording");
    session.processInput(inputs, 1, 512);
    session.inputLost(); session.waitForStop();
    outcome = session.snapshot();
    require(outcome.state == qfr::RecordingSession::State::failed && outcome.incomplete && outcome.recordingFile.getFileName() == "take_02.partial.flac", "failed finalization retains partial file");
    require(outcome.failureReason == qfr::RecordingSession::FailureReason::inputLost
            && outcome.finalization == qfr::RecordingSession::FinalizationOutcome::writerFailed, "compound input-loss/finalization failure reports retained incomplete audio");
    require(!folder.file.getChildFile("take_02.flac").exists(), "failed finalization is never promoted");
    auto failureReason = outcome.failureReason; auto finalization = outcome.finalization; auto retained = outcome.recordingFile;
    require(session.refreshPreview().wasOk(), "refresh next filename after failure");
    require(session.snapshot().failureReason == failureReason && session.snapshot().finalization == finalization
            && session.snapshot().recordingFile == retained && session.snapshot().state == qfr::RecordingSession::State::failed,
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
            && outcome.failureReason == qfr::RecordingSession::FailureReason::discontinuity
            && outcome.finalization == qfr::RecordingSession::FinalizationOutcome::finalized,
            "driver missing samples report a specific failed take with finalized captured audio");
    require(decode(outcome.recordingFile)->lengthInSamples == 512,
            "discontinuity drains accepted frames and rejects later potentially discontinuous audio");
}
void inputInterruptionAndFinalFlushFailureRemainSeparate()
{
    Folder folder;
    auto disk = std::make_shared<DiskControl>();
    qfr::RecordingSession session(diskOptions(disk));
    require(session.configure(config(folder, juce::String::fromUTF8("录音{path}"))).wasOk() && session.start().wasOk(), "start compound flush failure");
    const auto partial = session.snapshot().recordingFile;
    disk->failNextFlush = true;
    session.inputDiscontinuity(); session.waitForStop();
    const auto outcome = session.snapshot();
    require(outcome.state == qfr::RecordingSession::State::failed && outcome.incomplete && outcome.recordingFile == partial
        && outcome.failureReason == qfr::RecordingSession::FailureReason::discontinuity
        && outcome.finalization == qfr::RecordingSession::FinalizationOutcome::diskFlushFailed
        && outcome.error.detail == "disk flush refused" && partial.existsAsFile(), "retain both interruption and final flush outcome with verbatim diagnostic");
    session.refreshPreview(); session.requestStop(); session.inputLost();
    require(session.snapshot().failureReason == outcome.failureReason && session.snapshot().finalization == outcome.finalization
        && session.snapshot().recordingFile == partial && session.snapshot().error.detail == outcome.error.detail,
        "late notifications and preview retain compound terminal result");
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
    auto canRefresh = session.refreshPreview();
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
    require(canStart.failed() && canChange.failed() && canRefresh.failed(), "another take, configuration, and preview changes are unavailable during finalization");
    state = session.snapshot();
    require(state.state == qfr::RecordingSession::State::failed && state.incomplete && state.finalization == qfr::RecordingSession::FinalizationOutcome::promotionFailed, "promotion conflict retains incomplete recording and reports failure");
    require(state.error.detail.isNotEmpty(), "promotion conflict retains the original native disk diagnostic");
    require(state.error.disk.operation == qfr::DiskOperation::promotion && state.error.disk.nativeCode != 0
        && state.error.disk.detail == state.error.detail, "promotion exposes its operation, native code, and verbatim platform message");
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
    require(state.state == qfr::RecordingSession::State::failed && state.failureReason == qfr::RecordingSession::FailureReason::overflow, "buffer overflow stops and reports failed capture");
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
    require(!folder.file.getChildFile("writer_01.flac").exists() && state.failureReason == qfr::RecordingSession::FailureReason::writer, "writer failure cannot claim a successful save");
}
void concurrentReservationRechecksChangedFolderContents()
{
    Folder folder;
    qfr::RecordingSession first, second;
    require(first.configure(config(folder)).wasOk() && second.configure(config(folder)).wasOk(), "two previews");
    require(folder.file.getChildFile("take_01.flac").replaceWithText("original"), "folder changes after preview");
    std::barrier begin(3);
    qfr::RecorderResult firstResult { qfr::ErrorCode::startFailed }, secondResult = firstResult;
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
            && outcome.recordingFile.existsAsFile() && !outcome.controlsLocked()
            && outcome.error.code == qfr::ErrorCode::startFailed && outcome.error.detail == "disk unavailable", "startup exception reports failure and retains incomplete reservation");
}
void nativeDiskFailuresPreserveFirstDiagnosticAndRecordingOutcomes()
{
#if JUCE_WINDOWS
    constexpr uint64_t firstCode = ERROR_DISK_FULL, laterCode = ERROR_INVALID_HANDLE;
#else
    constexpr uint64_t firstCode = ENOSPC, laterCode = EBADF;
#endif
    for (auto operation : { qfr::DiskOperation::create, qfr::DiskOperation::write, qfr::DiskOperation::seek,
                            qfr::DiskOperation::flush, qfr::DiskOperation::close, qfr::DiskOperation::promotion })
    {
        Folder folder;
        std::atomic<bool> armed { false }, injected { false };
        qfr::RecordingSession::Options options;
        options.nativeError = [&](qfr::DiskOperation current, const juce::File& file) -> uint64_t
        {
            if (!armed || !file.getFileName().endsWith(".partial.flac")) return 0;
            if (current == operation) { injected = true; return firstCode; }
            if (injected && (current == qfr::DiskOperation::flush || current == qfr::DiskOperation::close)) return laterCode;
            return 0;
        };
        qfr::RecordingSession session(options);
        require(session.configure(config(folder)).wasOk(), "native boundary configured with real writable folder");
        const bool startup = operation == qfr::DiskOperation::create || operation == qfr::DiskOperation::write;
        armed = startup;
        const auto started = session.start();
        if (!startup)
        {
            require(started.wasOk(), "start real FLAC before native operation fails");
            std::array<float, 512> samples; samples.fill(.25f);
            const float* input[] { samples.data() };
            session.processInput(input, 1, 512);
            armed = true;
            session.inputLost();
            session.retainInputDiagnostic("original input {error}");
            session.waitForStop();
        }
        else require(started.failed(), "native creation/header write failure rejects start");
        const auto outcome = session.snapshot();
        require(injected && outcome.state == qfr::RecordingSession::State::failed
            && outcome.error.disk.operation == operation && outcome.error.disk.nativeCode == firstCode
            && outcome.error.disk.detail.isNotEmpty() && outcome.error.detail == outcome.error.disk.detail,
            "structured native operation and first platform error survive later flush/close failures");
        if (operation == qfr::DiskOperation::create)
            require(outcome.recordingFile == juce::File() && !outcome.incomplete
                && folder.file.findChildFiles(juce::File::findFiles, false).isEmpty(), "failure before create has no retained recording");
        else
            require(outcome.incomplete && outcome.recordingFile.existsAsFile()
                && !folder.file.getChildFile("take_01.flac").exists(), "native saving failure keeps actual partial and never promotes it");
        if (!startup)
            require(outcome.failureReason == qfr::RecordingSession::FailureReason::inputLost
                && outcome.inputDetail == "original input {error}", "native input interruption remains separate from native disk failure");
        if (operation == qfr::DiskOperation::promotion || operation == qfr::DiskOperation::close)
            require(decode(outcome.recordingFile)->lengthInSamples == 512, "finalized captured audio remains inspectable after close/promotion failure");
        session.refreshPreview();
        require(session.snapshot().error.disk.nativeCode == firstCode && session.snapshot().recordingFile == outcome.recordingFile,
            "passive preview preserves native disk outcome");
    }
}
void passiveReadinessPreservesSuccessfulAndFailedTakeOutcomes()
{
    for (const bool failDisk : { false, true })
    {
        Folder folder;
        std::atomic<bool> armed { false };
        qfr::RecordingSession::Options options;
        options.nativeError = [&](qfr::DiskOperation operation, const juce::File& file) -> uint64_t
        {
            if (!armed || operation != qfr::DiskOperation::flush || !file.getFileName().endsWith(".partial.flac")) return 0;
           #if JUCE_WINDOWS
            return ERROR_DISK_FULL;
           #else
            return ENOSPC;
           #endif
        };
        qfr::RecordingSession session(options);
        require(session.configure(config(folder)).wasOk() && session.start().wasOk(), "start readiness take");
        std::array<float, 512> samples; samples.fill(.5f);
        const float* input[] { samples.data() };
        session.processInput(input, 1, 512);
        armed = failDisk;
        if (failDisk) { session.inputLost(); session.retainInputDiagnostic("original native input failure"); }
        else session.requestStop();
        session.waitForStop();
        const auto outcome = session.snapshot();
        const auto unchanged = [&]
        {
            const auto current = session.snapshot();
            require(current.state == outcome.state && current.resultCode == outcome.resultCode
                && current.failureReason == outcome.failureReason && current.finalization == outcome.finalization
                && current.recordingFile == outcome.recordingFile && current.incomplete == outcome.incomplete
                && current.elapsedSeconds == outcome.elapsedSeconds && current.inputDetail == outcome.inputDetail
                && current.error.code == outcome.error.code && current.error.detail == outcome.error.detail
                && current.error.disk.operation == outcome.error.disk.operation && current.error.disk.nativeCode == outcome.error.disk.nativeCode,
                "passive readiness preserves all terminal result fields and separate native diagnostics");
        };
        const auto moved = folder.file.getSiblingFile(folder.file.getFileName() + "-moved");
        require(folder.file.moveFileTo(moved), "external target folder rename");
        const auto missing = session.refreshPreview();
        unchanged();
        const bool cleared = session.snapshot().nextFile == juce::File();
        require(moved.moveFileTo(folder.file), "external target folder restoration");
        require(missing.code == qfr::ErrorCode::folderMissing && cleared, "missing folder returns current validation and clears preview");
        require(session.refreshPreview().wasOk() && session.snapshot().nextFile.getFileName() == "take_02.flac", "restored folder recovers next filename and retains occupied counter");
        unchanged();
       #if !JUCE_WINDOWS
        if (getuid() == 0) continue;
       #endif
        {
            FolderWritePermission permission(folder.file);
            const auto denied = session.refreshPreview();
            unchanged();
            permission.restore();
            require(denied.failed() && session.refreshPreview().wasOk(), "permission changes and recovery update public readiness without configuration edits");
            unchanged();
        }
        require(folder.file.getChildFile("take_99.partial.flac").replaceWithText("occupied"), "new occupied counter after recovery");
        require(session.refreshPreview().wasOk() && session.snapshot().nextFile.getFileName() == "take_100.flac", "recovery reflects externally occupied counters");
        unchanged();
    }
}
void adapterExceptionDoesNotSuppressNativeCloseDiagnostic()
{
    Folder folder;
    auto disk = std::make_shared<DiskControl>();
    auto options = diskOptions(disk);
    options.nativeError = [](qfr::DiskOperation operation, const juce::File& file) -> uint64_t
    {
        if (operation != qfr::DiskOperation::close || !file.getFileName().endsWith(".partial.flac")) return 0;
       #if JUCE_WINDOWS
        return ERROR_INVALID_HANDLE;
       #else
        return EBADF;
       #endif
    };
    qfr::RecordingSession session(options);
    require(session.configure(config(folder)).wasOk() && session.start().wasOk(), "start mixed adapter/native failure take");
    disk->failNextFlush = true;
    session.requestStop(); session.waitForStop();
    const auto result = session.snapshot();
    require(result.state == qfr::RecordingSession::State::failed && result.incomplete
        && result.error.detail == "disk flush refused" && result.error.disk.operation == qfr::DiskOperation::close
        && result.error.disk.nativeCode != 0 && result.error.disk.detail.isNotEmpty(),
        "adapter exception and first native close error remain independently available");
}
void counterExhaustionRequiresAnExplicitNewPrefix()
{
    Folder folder;
    qfr::RecordingSession session;
    require(folder.file.getChildFile("take_18446744073709551615.partial.flac").replaceWithText("occupied"), "seed exhausted counter");
    const auto exhausted = session.configure(config(folder));
    require(exhausted.code == qfr::ErrorCode::counterExhausted && session.start().failed(), "maximum counter cannot wrap or start a recording");
    require(session.configure(config(folder, "another")).wasOk(), "explicit new prefix recovers recording");
    require(folder.file.getChildFile("tooLarge_18446744073709551616.flac").replaceWithText("occupied"), "seed oversized counter");
    require(session.configure(config(folder, "tooLarge")).code == qfr::ErrorCode::counterTooLarge, "oversized counter has a distinct stable result");
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
        require(session.snapshot().state == outcome.state && session.snapshot().failureReason == outcome.failureReason
            && session.snapshot().finalization == outcome.finalization && session.snapshot().recordingFile == outcome.recordingFile, "late stop/device notification cannot overwrite a terminal outcome");
    }
}
}
int main(int argc, char** argv)
{
    try
    {
        if (argc == 2 && juce::String(argv[1]) == "--promotion-only")
        {
            previewUsesHighestOccupiedCounter();
            savesSelectedMonoAndDrainsBeforePromotion();
            metersAndSavesSelectedStereoInOrder();
            finalizationLocksAnotherTakeAndPromotionDoesNotReplace();
            concurrentReservationRechecksChangedFolderContents();
            nativeDiskFailuresPreserveFirstDiagnosticAndRecordingOutcomes();
            std::cout << "PASS filesystem mono/stereo FLAC, counter advancement, conflict, concurrent reservation, and native promotion failure\n";
            return 0;
        }
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
        inputInterruptionAndFinalFlushFailureRemainSeparate();
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
        nativeDiskFailuresPreserveFirstDiagnosticAndRecordingOutcomes();
        adapterExceptionDoesNotSuppressNativeCloseDiagnostic();
        std::cout << "PASS native create/write/seek/flush/close/promotion diagnostics\n";
        passiveReadinessPreservesSuccessfulAndFailedTakeOutcomes();
        std::cout << "PASS passive readiness retains successful/failed terminal results through external folder and permission changes\n";
        counterExhaustionRequiresAnExplicitNewPrefix();
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

