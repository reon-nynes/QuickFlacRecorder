// SPDX-License-Identifier: AGPL-3.0-only
#include "RecordingSession.h"
#include <limits>
#include <cmath>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>
#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#endif

namespace
{
struct IoStatus { std::atomic<bool> failed { false }; };

// The file handle comes from an exclusive create, and is never reopened by a pathname.
class ReservedOutput final : public juce::OutputStream
{
public:
#if JUCE_WINDOWS
    using Handle = HANDLE;
#else
    using Handle = int;
#endif
    ReservedOutput(Handle handleIn, std::shared_ptr<IoStatus> statusIn) : handle(handleIn), status(std::move(statusIn)) {}
    ~ReservedOutput() override
    {
        flush();
#if JUCE_WINDOWS
        if (!CloseHandle(handle)) status->failed = true;
#else
        if (::close(handle) != 0) status->failed = true;
#endif
    }
    void flush() override
    {
#if JUCE_WINDOWS
        if (!FlushFileBuffers(handle)) status->failed = true;
#else
        if (::fsync(handle) != 0) status->failed = true;
#endif
    }
    bool setPosition(juce::int64 position) override
    {
#if JUCE_WINDOWS
        LARGE_INTEGER offset; offset.QuadPart = position;
        bool ok = SetFilePointerEx(handle, offset, nullptr, FILE_BEGIN) != 0;
#else
        bool ok = ::lseek(handle, static_cast<off_t>(position), SEEK_SET) >= 0;
#endif
        if (!ok) status->failed = true;
        return ok;
    }
    juce::int64 getPosition() override
    {
#if JUCE_WINDOWS
        LARGE_INTEGER offset {}, position {};
        if (!SetFilePointerEx(handle, offset, &position, FILE_CURRENT)) { status->failed = true; return -1; }
        return position.QuadPart;
#else
        auto position = ::lseek(handle, 0, SEEK_CUR);
        if (position < 0) status->failed = true;
        return static_cast<juce::int64>(position);
#endif
    }
    bool write(const void* data, size_t bytes) override
    {
        auto* source = static_cast<const char*>(data);
        while (bytes > 0)
        {
#if JUCE_WINDOWS
            DWORD written = 0;
            if (!WriteFile(handle, source, static_cast<DWORD>(std::min<size_t>(bytes, 0x7fffffff)), &written, nullptr) || written == 0)
#else
            auto written = ::write(handle, source, bytes);
            if (written < 0 && errno == EINTR) continue;
            if (written <= 0)
#endif
            { status->failed = true; return false; }
            source += written;
            bytes -= static_cast<size_t>(written);
        }
        return true;
    }
private:
    Handle handle;
    std::shared_ptr<IoStatus> status;
};

enum class CreateResult { created, occupied, failed };
CreateResult reserve(const juce::File& file, std::shared_ptr<IoStatus> status, std::unique_ptr<juce::OutputStream>& output)
{
#if JUCE_WINDOWS
    auto handle = CreateFileW(file.getFullPathName().toWideCharPointer(), GENERIC_WRITE | GENERIC_READ,
                              FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
    {
        auto error = GetLastError();
        return error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS ? CreateResult::occupied : CreateResult::failed;
    }
#else
    auto handle = ::open(file.getFullPathName().toRawUTF8(), O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
    if (handle < 0) return errno == EEXIST ? CreateResult::occupied : CreateResult::failed;
#endif
    output = std::make_unique<ReservedOutput>(handle, std::move(status));
    return CreateResult::created;
}

class CheckedOutput final : public juce::OutputStream
{
public:
    CheckedOutput(std::unique_ptr<juce::OutputStream> outputIn, std::shared_ptr<IoStatus> statusIn)
        : output(std::move(outputIn)), status(std::move(statusIn)) {}
    void flush() override { output->flush(); }
    bool setPosition(juce::int64 position) override { return check(output->setPosition(position)); }
    juce::int64 getPosition() override { auto position = output->getPosition(); if (position < 0) status->failed = true; return position; }
    bool write(const void* data, size_t bytes) override { return check(output->write(data, bytes)); }
private:
    bool check(bool ok) { if (!ok) status->failed = true; return ok; }
    std::unique_ptr<juce::OutputStream> output;
    std::shared_ptr<IoStatus> status;
};

bool promoteWithoutReplacement(const juce::File& partial, const juce::File& final)
{
#if JUCE_WINDOWS
    return MoveFileExW(partial.getFullPathName().toWideCharPointer(), final.getFullPathName().toWideCharPointer(), MOVEFILE_WRITE_THROUGH) != 0;
#else
    // A hard link creates the final name atomically and fails if it already exists.
    // Both names are in the same folder/filesystem. A failed unlink retains the partial.
    if (::link(partial.getFullPathName().toRawUTF8(), final.getFullPathName().toRawUTF8()) != 0) return false;
    return ::unlink(partial.getFullPathName().toRawUTF8()) == 0;
#endif
}

juce::Result validate(const qfr::RecordingSession::Configuration& config)
{
    auto prefix = config.recordingPrefix;
    if (prefix.isEmpty() || prefix.getNumBytesAsUTF8() > 180 || prefix.endsWithChar('.') || prefix.endsWithChar(' '))
        return juce::Result::fail("Enter a recording prefix of 1 to 180 UTF-8 bytes without a trailing dot or space.");
    for (auto character : prefix)
        if (character < 32 || juce::String("<>:\"/\\|?*").containsChar(character))
            return juce::Result::fail("The recording prefix contains a control character or a filename character that is unsafe on Windows.");
    auto base = prefix.upToFirstOccurrenceOf(".", false, false).toUpperCase();
    if (base == "CON" || base == "PRN" || base == "AUX" || base == "NUL"
        || (base.length() == 4 && (base.startsWith("COM") || base.startsWith("LPT")) && base[3] >= '1' && base[3] <= '9'))
        return juce::Result::fail("Choose a recording prefix other than a reserved Windows device name.");
    if (!config.targetFolder.isDirectory())
        return juce::Result::fail("The target folder does not exist. Choose an existing writable folder.");
    if (!config.targetFolder.hasWriteAccess())
        return juce::Result::fail("The target folder is not writable. Choose another folder or change its permissions.");
    // Directory access checks can report success even when its ACL forbids adding files.
    // Verify the exact operation needed for a recording, before reserving a recording name.
    auto probe = config.targetFolder.getChildFile(".qfr-write-check-" + juce::Uuid().toString() + ".tmp");
    auto probeStatus = std::make_shared<IoStatus>();
    std::unique_ptr<juce::OutputStream> probeOutput;
    if (reserve(probe, probeStatus, probeOutput) != CreateResult::created)
        return juce::Result::fail("The target folder does not allow new files. Choose another folder or change its permissions.");
    probeOutput.reset();
    if (!probe.deleteFile() || probeStatus->failed)
        return juce::Result::fail("The target folder cannot safely create and remove files. Choose another folder or change its permissions.");
    if (!std::isfinite(config.inputStreamRate) || config.inputStreamRate < 1 || config.inputStreamRate > 655350
        || std::floor(config.inputStreamRate) != config.inputStreamRate)
        return juce::Result::fail("Open an input device with a valid input stream rate before recording.");
    if (config.leftInputChannel < 0 || config.rightInputChannel < -1 || config.rightInputChannel == config.leftInputChannel)
        return juce::Result::fail("Choose one input channel or two distinct channels for a stereo pair.");
    return juce::Result::ok();
}
juce::String numberedName(const juce::String& prefix, uint64_t counter, bool partial = false)
{
    auto number = juce::String(counter).paddedLeft('0', 2);
    return prefix + "_" + number + (partial ? ".partial.flac" : ".flac");
}
juce::Result nextCounter(const qfr::RecordingSession::Configuration& config, uint64_t& next)
{
    uint64_t highest = 0;
    const auto beginning = config.recordingPrefix + "_";
    for (const auto& file : config.targetFolder.findChildFiles(juce::File::findFilesAndDirectories, false))
    {
        auto name = file.getFileName();
        if (!name.startsWith(beginning)) continue;
        auto suffix = name.substring(beginning.length());
        if (suffix.endsWithIgnoreCase(".partial.flac")) suffix = suffix.dropLastCharacters(13);
        else if (suffix.endsWithIgnoreCase(".flac")) suffix = suffix.dropLastCharacters(5);
        else continue;
        if (suffix.isEmpty() || !suffix.containsOnly("0123456789")) continue;
        uint64_t value = 0;
        for (auto digit : suffix)
        {
            if (value > (std::numeric_limits<uint64_t>::max() - (digit - '0')) / 10)
                return juce::Result::fail("The recording counter is too large. Choose another recording prefix.");
            value = value * 10 + digit - '0';
        }
        highest = std::max(highest, value);
    }
    if (highest == std::numeric_limits<uint64_t>::max())
        return juce::Result::fail("The recording counter is exhausted. Choose another recording prefix.");
    next = highest + 1;
    return juce::Result::ok();
}
}

namespace qfr
{
static_assert(std::atomic<uint64_t>::is_always_lock_free && std::atomic<float>::is_always_lock_free);
struct RecordingSession::Impl
{
    enum class Failure { none, input, overflow, writer, promotion };
    explicit Impl(Options optionsIn) : options(std::move(optionsIn)) {}
    Options options;
    Configuration config;
    Snapshot view;
    mutable std::mutex mutex;
    std::atomic<State> state { State::idle };
    std::atomic<bool> accepting { false };
    std::atomic<unsigned> inFlight { 0 };
    std::atomic<uint64_t> selection { 0xffffffff00000000ULL };
    std::array<std::atomic<float>, 2> peaks { 0, 0 };
    std::array<std::atomic<bool>, 2> clipped { false, false };
    std::atomic<uint64_t> head { 0 }, tail { 0 }, capturedFrames { 0 };
    std::atomic<Failure> failure { Failure::none };
    std::vector<float> ring;
    std::thread worker;

    bool locked() const { auto current = state.load(); return current == State::recording || current == State::finalizing; }
    void fail(Failure reason) noexcept
    {
        auto expected = Failure::none;
        failure.compare_exchange_strong(expected, reason);
        accepting = false;
        state = State::finalizing;
    }
    juce::Result updatePreview()
    {
        view.nextFile = {};
        auto valid = validate(config);
        if (valid.failed()) { view.message = valid.getErrorMessage(); return valid; }
        uint64_t next = 1;
        auto result = nextCounter(config, next);
        if (result.wasOk()) view.nextFile = config.targetFolder.getChildFile(numberedName(config.recordingPrefix, next));
        view.message = result.failed() ? result.getErrorMessage() : "Ready to record.";
        return result;
    }
    void writeRecording(std::unique_ptr<juce::AudioFormatWriter> writer, std::shared_ptr<IoStatus> io,
                        juce::File partial, juce::File final, double rate, int channels)
    {
        std::array<float, 8192> block {};
        uint64_t encoded = 0;
        for (;;)
        {
            auto read = tail.load(std::memory_order_relaxed);
            auto available = head.load(std::memory_order_acquire) - read;
            if (available != 0)
            {
                auto count = static_cast<int>(std::min<uint64_t>(4096, available));
                for (int channel = 0; channel < channels; ++channel)
                    for (int frame = 0; frame < count; ++frame)
                        block[static_cast<size_t>(channel * 4096 + frame)] = ring[static_cast<size_t>(channel) * options.captureCapacityFrames
                                                                                           + (read + frame) % options.captureCapacityFrames];
                const float* audio[] { block.data(), block.data() + 4096 };
                if (!writer->writeFromFloatArrays(audio, channels, count) || io->failed)
                {
                    fail(Failure::writer);
                    break;
                }
                encoded += static_cast<uint64_t>(count);
                tail.store(read + static_cast<uint64_t>(count), std::memory_order_release);
                continue;
            }
            if (!accepting.load() && inFlight.load() == 0 && head.load(std::memory_order_acquire) == read) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        // A writer failure can stop the consumer while one callback is copying its block.
        // Keep the take locked until that producer is quiescent, before another start can resize/reset storage.
        while (inFlight.load() != 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        // JUCE writes FLAC metadata during destruction. Check every underlying operation,
        // including writes/seek/flush/close performed after the last successful audio block.
        writer.reset();
        if (io->failed) fail(Failure::writer);
        bool finalizationValid = !io->failed;
        if (!io->failed)
        {
            juce::FlacAudioFormat flac;
            auto stream = partial.createInputStream();
            auto reader = stream ? std::unique_ptr<juce::AudioFormatReader>(flac.createReaderFor(stream.release(), true)) : nullptr;
            if (!reader || reader->sampleRate != rate || reader->numChannels != static_cast<unsigned>(channels)
                || reader->bitsPerSample != 16 || reader->lengthInSamples != static_cast<juce::int64>(encoded))
            {
                finalizationValid = false;
                fail(Failure::writer);
            }
        }
        bool complete = finalizationValid && failure.load() != Failure::writer;
        if (complete && !promoteWithoutReplacement(partial, final)) { fail(Failure::promotion); complete = false; }
        auto reason = failure.load();
        {
            std::lock_guard lock(mutex);
            view.recordingFile = complete ? final : partial;
            view.incomplete = !complete;
            if (!complete)
            {
                auto interruption = reason == Failure::input ? juce::String("Input device was lost. ")
                                   : reason == Failure::overflow ? juce::String("The capture buffer overflowed. ") : juce::String();
                view.message = interruption
                             + (!finalizationValid || reason == Failure::writer ? "Writing or finalizing the FLAC failed. "
                                                                       : "The final recording filename could not be reserved without replacement. ")
                             + "The incomplete recording remains at " + partial.getFullPathName();
            }
            else switch (reason)
            {
                case Failure::none: view.message = "Saved " + final.getFileName(); break;
                case Failure::input: view.message = "Input device was lost. Captured audio was finalized as " + final.getFileName(); break;
                case Failure::overflow: view.message = "Recording stopped because the capture buffer overflowed. Captured audio was finalized as " + final.getFileName(); break;
                case Failure::writer: view.message = "Writing or finalizing the FLAC failed. The incomplete recording remains at " + partial.getFullPathName(); break;
                case Failure::promotion: view.message = "The final recording filename could not be reserved without replacement. The incomplete recording remains at " + partial.getFullPathName(); break;
            }
            uint64_t next = 1;
            if (nextCounter(config, next).wasOk()) view.nextFile = config.targetFolder.getChildFile(numberedName(config.recordingPrefix, next));
        }
        state.store(reason == Failure::none ? State::succeeded : State::failed, std::memory_order_release);
    }
};
RecordingSession::RecordingSession() : RecordingSession(Options {}) {}
RecordingSession::RecordingSession(Options options) : impl(std::make_unique<Impl>(std::move(options))) {}
RecordingSession::~RecordingSession() { requestStop(); waitForStop(); }
juce::Result RecordingSession::configure(const Configuration& config)
{
    std::lock_guard lock(impl->mutex);
    if (impl->locked()) return juce::Result::fail("Stop and finalize the recording before changing its settings.");
    impl->config = config;
    impl->state = State::idle;
    impl->view = {};
    impl->capturedFrames = 0;
    impl->view.inputStreamRate = config.inputStreamRate;
    impl->view.outputChannels = config.rightInputChannel < 0 ? 1 : 2;
    impl->selection.store(static_cast<uint64_t>(static_cast<uint32_t>(config.leftInputChannel))
                          | (static_cast<uint64_t>(static_cast<uint32_t>(config.rightInputChannel)) << 32));
    return impl->updatePreview();
}
juce::Result RecordingSession::refreshPreview()
{
    std::lock_guard lock(impl->mutex);
    if (impl->locked()) return juce::Result::fail("The recording filename is locked until finalization ends.");
    auto state = impl->state.load();
    auto message = impl->view.message;
    auto result = impl->updatePreview();
    if (state == State::succeeded || state == State::failed) impl->view.message = message;
    return result;
}
juce::Result RecordingSession::start() try
{
    if (impl->locked()) return juce::Result::fail("A recording is still capturing or finalizing.");
    waitForStop();
    std::lock_guard lock(impl->mutex);
    auto reject = [&](const juce::String& message)
    {
        impl->state = State::failed;
        impl->view.message = message;
        return juce::Result::fail(message);
    };
    impl->view.recordingFile = {}; impl->view.incomplete = false; impl->capturedFrames = 0;
    auto valid = impl->updatePreview();
    if (valid.failed()) return reject(valid.getErrorMessage());
    if (impl->options.captureCapacityFrames == 0 || impl->options.captureCapacityFrames > 16777216)
        return reject("The capture buffer size is invalid.");
    uint64_t next = 1;
    auto counter = nextCounter(impl->config, next);
    if (counter.failed()) return reject(counter.getErrorMessage());
    juce::File partial, final;
    auto io = std::make_shared<IoStatus>();
    std::unique_ptr<juce::OutputStream> stream;
    for (int attempts = 0; attempts < 10000; ++attempts)
    {
        final = impl->config.targetFolder.getChildFile(numberedName(impl->config.recordingPrefix, next));
        partial = impl->config.targetFolder.getChildFile(numberedName(impl->config.recordingPrefix, next, true));
        auto result = final.exists() ? CreateResult::occupied : reserve(partial, io, stream);
        if (result == CreateResult::created) break;
        if (result == CreateResult::failed) return reject("Cannot create a recording in the target folder. Check its permissions and free disk space.");
        if (next == std::numeric_limits<uint64_t>::max()) return reject("The recording counter is exhausted.");
        ++next;
    }
    if (!stream) return reject("Could not reserve a recording filename. Try another prefix.");
    impl->view.recordingFile = partial; impl->view.incomplete = true;
    if (impl->options.outputAdapter) stream = impl->options.outputAdapter(std::move(stream));
    if (!stream) return reject("The recording output could not be opened. The incomplete recording remains in the target folder.");
    stream = std::make_unique<CheckedOutput>(std::move(stream), io);
    juce::FlacAudioFormat flac;
    auto channels = impl->config.rightInputChannel < 0 ? 1 : 2;
    auto writer = flac.createWriterFor(stream, juce::AudioFormatWriterOptions().withSampleRate(impl->config.inputStreamRate)
                                              .withNumChannels(channels).withBitsPerSample(16).withQualityOptionIndex(5));
    if (!writer || io->failed) return reject("The FLAC writer could not start. The incomplete recording remains in the target folder.");
    impl->ring.resize(impl->options.captureCapacityFrames * 2);
    impl->head = 0; impl->tail = 0; impl->capturedFrames = 0; impl->failure = Impl::Failure::none;
    impl->view.message = "Recording " + partial.getFileName();
    impl->state = State::recording;
    impl->accepting = true;
    impl->worker = std::thread([p = impl.get(), writer = std::move(writer), io, partial, final,
                              rate = impl->config.inputStreamRate, channels]() mutable
    {
        try { p->writeRecording(std::move(writer), io, partial, final, rate, channels); }
        catch (...)
        {
            p->accepting = false;
            while (p->inFlight.load() != 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            std::lock_guard workerLock(p->mutex);
            p->view.recordingFile = partial; p->view.incomplete = true;
            p->view.message = "Recording stopped after a writer failure. The incomplete recording remains at " + partial.getFullPathName();
            p->state = State::failed;
        }
    });
    return juce::Result::ok();
}
catch (...)
{
    impl->accepting = false;
    while (impl->inFlight.load() != 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    std::lock_guard lock(impl->mutex);
    impl->state = State::failed;
    impl->view.message = "Recording could not start. Check the target folder, disk space, and available memory.";
    if (impl->view.incomplete) impl->view.message += " The incomplete recording remains at " + impl->view.recordingFile.getFullPathName();
    return juce::Result::fail(impl->view.message);
}
void RecordingSession::requestStop()
{
    auto expected = State::recording;
    if (impl->state.compare_exchange_strong(expected, State::finalizing))
        impl->accepting = false;
}
void RecordingSession::waitForStop() { if (impl->worker.joinable()) impl->worker.join(); }
RecordingSession::Snapshot RecordingSession::snapshot() const
{
    std::lock_guard lock(impl->mutex);
    auto result = impl->view;
    result.state = impl->state.load(std::memory_order_acquire);
    result.elapsedSeconds = result.inputStreamRate > 0 ? static_cast<double>(impl->capturedFrames.load()) / result.inputStreamRate : 0;
    return result;
}
RecordingSession::Levels RecordingSession::readLevels() noexcept
{
    Levels levels;
    levels.channels = static_cast<int32_t>(impl->selection.load() >> 32) < 0 ? 1 : 2;
    for (int channel = 0; channel < 2; ++channel)
    {
        levels.peak[channel] = impl->peaks[channel].exchange(0);
        levels.clipped[channel] = impl->clipped[channel].exchange(false);
    }
    return levels;
}
void RecordingSession::processInput(const float* const* inputs, int inputChannels, int frames) noexcept
{
    if (frames <= 0) return;
    impl->inFlight.fetch_add(1);
    auto selected = impl->selection.load();
    int left = static_cast<int32_t>(selected & 0xffffffffULL), right = static_cast<int32_t>(selected >> 32);
    const int channels = right < 0 ? 1 : 2;
    bool capture = impl->accepting.load();
    bool valid = inputs && left >= 0 && left < inputChannels && inputs[left]
                 && (channels == 1 || (right < inputChannels && inputs[right]));
    if (!valid)
    {
        if (capture) impl->fail(Impl::Failure::input);
        impl->inFlight.fetch_sub(1); return;
    }
    for (int channel = 0; channel < channels; ++channel)
    {
        const float* source = inputs[channel == 0 ? left : right];
        float peak = 0;
        for (int frame = 0; frame < frames; ++frame) peak = std::max(peak, std::abs(source[frame]));
        auto previous = impl->peaks[channel].load(std::memory_order_relaxed);
        while (previous < peak && !impl->peaks[channel].compare_exchange_weak(previous, peak, std::memory_order_relaxed)) {}
        if (peak >= 1) impl->clipped[channel].store(true, std::memory_order_relaxed);
    }
    if (capture)
    {
        auto write = impl->head.load(std::memory_order_relaxed);
        auto read = impl->tail.load(std::memory_order_acquire);
        if (static_cast<uint64_t>(frames) > impl->options.captureCapacityFrames - (write - read))
            impl->fail(Impl::Failure::overflow);
        else
        {
            for (int channel = 0; channel < channels; ++channel)
            {
                const float* source = inputs[channel == 0 ? left : right];
                for (int frame = 0; frame < frames; ++frame)
                    impl->ring[static_cast<size_t>(channel) * impl->options.captureCapacityFrames
                               + (write + frame) % impl->options.captureCapacityFrames] = source[frame];
            }
            impl->capturedFrames.fetch_add(static_cast<uint64_t>(frames));
            impl->head.store(write + static_cast<uint64_t>(frames), std::memory_order_release);
        }
    }
    impl->inFlight.fetch_sub(1);
}
void RecordingSession::inputLost() noexcept
{
    // Device error notifications can race with a requested stop just like an audio block.
    impl->inFlight.fetch_add(1);
    if (impl->accepting.load()) impl->fail(Impl::Failure::input);
    impl->inFlight.fetch_sub(1);
}
}
