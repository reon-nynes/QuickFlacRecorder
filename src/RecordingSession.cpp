// SPDX-License-Identifier: AGPL-3.0-only
#include "RecordingSession.h"
#include <limits>
#include <cmath>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>
#include <cstring>
#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <stdio.h>
#if JUCE_LINUX
#include <linux/fs.h>
#include <sys/syscall.h>
#endif
#endif

namespace
{
using qfr::DiskOperation;
uint64_t nativeErrorCode()
{
#if JUCE_WINDOWS
    return GetLastError();
#else
    return static_cast<uint64_t>(errno);
#endif
}
juce::String nativeErrorMessage(uint64_t code)
{
#if JUCE_WINDOWS
    wchar_t* message = nullptr;
    const auto length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, static_cast<DWORD>(code), 0, reinterpret_cast<wchar_t*>(&message), 0, nullptr);
    const auto detail = length > 0 ? juce::String(message, length) : juce::String();
    LocalFree(message);
    return detail;
#else
    return juce::String::fromUTF8(std::strerror(static_cast<int>(code)));
#endif
}
struct IoStatus
{
    std::atomic<bool> failed { false }, flushFailed { false };
    juce::String detail; // Only the writer/control thread touches native diagnostic data.
    qfr::DiskDiagnostic disk;
    juce::File file;
    std::function<uint64_t(DiskOperation, const juce::File&)> nativeError;
    uint64_t injected(DiskOperation operation) const { return nativeError ? nativeError(operation, file) : 0; }
    void record(DiskOperation operation, uint64_t code)
    {
        failed = true;
        if (operation == DiskOperation::flush) flushFailed = true;
        if (disk.operation == DiskOperation::none)
        {
            disk = { operation, code, nativeErrorMessage(code) };
            if (detail.isEmpty()) detail = disk.detail;
        }
    }
};

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
        const auto injected = status->injected(DiskOperation::close);
#if JUCE_WINDOWS
        if (!CloseHandle(handle)) status->record(DiskOperation::close, nativeErrorCode());
#else
        if (::close(handle) != 0) status->record(DiskOperation::close, nativeErrorCode());
#endif
        if (injected != 0) status->record(DiskOperation::close, injected);
    }
    void flush() override
    {
        if (auto code = status->injected(DiskOperation::flush)) { status->record(DiskOperation::flush, code); return; }
#if JUCE_WINDOWS
        if (!FlushFileBuffers(handle)) status->record(DiskOperation::flush, nativeErrorCode());
#else
        if (::fsync(handle) != 0) status->record(DiskOperation::flush, nativeErrorCode());
#endif
    }
    bool setPosition(juce::int64 position) override
    {
        if (auto code = status->injected(DiskOperation::seek)) { status->record(DiskOperation::seek, code); return false; }
#if JUCE_WINDOWS
        LARGE_INTEGER offset; offset.QuadPart = position;
        bool ok = SetFilePointerEx(handle, offset, nullptr, FILE_BEGIN) != 0;
#else
        bool ok = ::lseek(handle, static_cast<off_t>(position), SEEK_SET) >= 0;
#endif
        if (!ok) status->record(DiskOperation::seek, nativeErrorCode());
        return ok;
    }
    juce::int64 getPosition() override
    {
        if (auto code = status->injected(DiskOperation::seek)) { status->record(DiskOperation::seek, code); return -1; }
#if JUCE_WINDOWS
        LARGE_INTEGER offset {}, position {};
        if (!SetFilePointerEx(handle, offset, &position, FILE_CURRENT)) { status->record(DiskOperation::seek, nativeErrorCode()); return -1; }
        return position.QuadPart;
#else
        auto position = ::lseek(handle, 0, SEEK_CUR);
        if (position < 0) status->record(DiskOperation::seek, nativeErrorCode());
        return static_cast<juce::int64>(position);
#endif
    }
    bool write(const void* data, size_t bytes) override
    {
        if (auto code = status->injected(DiskOperation::write)) { status->record(DiskOperation::write, code); return false; }
        auto* source = static_cast<const char*>(data);
        while (bytes > 0)
        {
#if JUCE_WINDOWS
            DWORD written = 0;
            if (!WriteFile(handle, source, static_cast<DWORD>(std::min<size_t>(bytes, 0x7fffffff)), &written, nullptr))
            { status->record(DiskOperation::write, nativeErrorCode()); return false; }
            if (written == 0)
            { status->record(DiskOperation::write, ERROR_WRITE_FAULT); return false; }
#else
            auto written = ::write(handle, source, bytes);
            if (written < 0 && errno == EINTR) continue;
            if (written < 0) { status->record(DiskOperation::write, nativeErrorCode()); return false; }
            if (written == 0)
            { status->record(DiskOperation::write, EIO); return false; }
#endif
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
    status->file = file;
    if (auto code = status->injected(DiskOperation::create)) { status->record(DiskOperation::create, code); return CreateResult::failed; }
#if JUCE_WINDOWS
    auto handle = CreateFileW(file.getFullPathName().toWideCharPointer(), GENERIC_WRITE | GENERIC_READ,
                              FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
    {
        auto error = GetLastError();
        if (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS) return CreateResult::occupied;
        status->record(DiskOperation::create, error);
        return CreateResult::failed;
    }
#else
    auto handle = ::open(file.getFullPathName().toRawUTF8(), O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
    if (handle < 0)
    {
        const auto error = errno;
        if (error == EEXIST) return CreateResult::occupied;
        status->record(DiskOperation::create, static_cast<uint64_t>(error));
        return CreateResult::failed;
    }
#endif
    output = std::make_unique<ReservedOutput>(handle, std::move(status));
    return CreateResult::created;
}

class CheckedOutput final : public juce::OutputStream
{
public:
    CheckedOutput(std::unique_ptr<juce::OutputStream> outputIn, std::shared_ptr<IoStatus> statusIn)
        : output(std::move(outputIn)), status(std::move(statusIn)) {}
    void flush() override
    {
        try { output->flush(); }
        catch (const std::exception& error) { status->failed = true; status->flushFailed = true; if (status->detail.isEmpty()) status->detail = juce::String::fromUTF8(error.what()); }
        catch (...) { status->failed = true; status->flushFailed = true; }
    }
    bool setPosition(juce::int64 position) override { return check(output->setPosition(position)); }
    juce::int64 getPosition() override { auto position = output->getPosition(); if (position < 0) status->failed = true; return position; }
    bool write(const void* data, size_t bytes) override { return check(output->write(data, bytes)); }
private:
    bool check(bool ok) { if (!ok) status->failed = true; return ok; }
    std::unique_ptr<juce::OutputStream> output;
    std::shared_ptr<IoStatus> status;
};

bool promoteWithoutReplacement(const juce::File& partial, const juce::File& final, IoStatus& status)
{
    if (auto code = status.injected(DiskOperation::promotion)) { status.record(DiskOperation::promotion, code); return false; }
#if JUCE_WINDOWS
    const bool ok = MoveFileExW(partial.getFullPathName().toWideCharPointer(), final.getFullPathName().toWideCharPointer(), MOVEFILE_WRITE_THROUGH) != 0;
#elif JUCE_MAC
    const bool ok = ::renamex_np(partial.getFullPathName().toRawUTF8(), final.getFullPathName().toRawUTF8(), RENAME_EXCL) == 0;
#else
    // No overwrite-capable fallback: unsupported filesystems retain the partial.
    const bool ok = ::syscall(SYS_renameat2, AT_FDCWD, partial.getFullPathName().toRawUTF8(),
                              AT_FDCWD, final.getFullPathName().toRawUTF8(), RENAME_NOREPLACE) == 0;
#endif
    if (!ok) status.record(DiskOperation::promotion, nativeErrorCode());
    return ok;
}

qfr::RecorderResult diskResult(qfr::ErrorCode code, const IoStatus& status)
{
    return { code, status.detail, {}, {}, status.disk };
}
qfr::RecorderResult validate(const qfr::RecordingSession::Configuration& config, const qfr::RecordingSession::Options& options)
{
    auto prefix = config.recordingPrefix;
    if (prefix.isEmpty() || prefix.getNumBytesAsUTF8() > 180 || prefix.endsWithChar('.') || prefix.endsWithChar(' '))
        return { qfr::ErrorCode::prefixLength };
    for (auto character : prefix)
        if (character < 32 || juce::String("<>:\"/\\|?*").containsChar(character))
            return { qfr::ErrorCode::prefixCharacters };
    auto base = prefix.upToFirstOccurrenceOf(".", false, false).toUpperCase();
    if (base == "CON" || base == "PRN" || base == "AUX" || base == "NUL"
        || (base.length() == 4 && (base.startsWith("COM") || base.startsWith("LPT")) && base[3] >= '1' && base[3] <= '9'))
        return { qfr::ErrorCode::prefixReserved };
    if (!config.targetFolder.isDirectory())
        return { qfr::ErrorCode::folderMissing };
    if (!config.targetFolder.hasWriteAccess())
        return { qfr::ErrorCode::folderNotWritable };
    // Directory access checks can report success even when its ACL forbids adding files.
    // Verify the exact operation needed for a recording, before reserving a recording name.
    auto probe = config.targetFolder.getChildFile(".qfr-write-check-" + juce::Uuid().toString() + ".tmp");
    auto probeStatus = std::make_shared<IoStatus>();
    probeStatus->nativeError = options.nativeError;
    std::unique_ptr<juce::OutputStream> probeOutput;
    if (reserve(probe, probeStatus, probeOutput) != CreateResult::created)
        return diskResult(qfr::ErrorCode::folderCreateDenied, *probeStatus);
    probeOutput.reset();
#if JUCE_WINDOWS
    const bool removed = DeleteFileW(probe.getFullPathName().toWideCharPointer()) != 0;
#else
    const bool removed = ::unlink(probe.getFullPathName().toRawUTF8()) == 0;
#endif
    if (!removed) probeStatus->record(DiskOperation::cleanup, nativeErrorCode());
    if (probeStatus->failed) return diskResult(qfr::ErrorCode::folderCleanupFailed, *probeStatus);
    if (!std::isfinite(config.inputStreamRate) || config.inputStreamRate < 1 || config.inputStreamRate > 655350
        || std::floor(config.inputStreamRate) != config.inputStreamRate)
        return { qfr::ErrorCode::invalidRate };
    if (config.leftInputChannel < 0 || config.rightInputChannel < -1 || config.rightInputChannel == config.leftInputChannel)
        return { qfr::ErrorCode::invalidChannels };
    return {};
}
juce::String numberedName(const juce::String& prefix, uint64_t counter, bool partial = false)
{
    auto number = juce::String(counter).paddedLeft('0', 2);
    return prefix + "_" + number + (partial ? ".partial.flac" : ".flac");
}
qfr::RecorderResult nextCounter(const qfr::RecordingSession::Configuration& config, uint64_t& next)
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
                return { qfr::ErrorCode::counterTooLarge };
            value = value * 10 + digit - '0';
        }
        highest = std::max(highest, value);
    }
    if (highest == std::numeric_limits<uint64_t>::max())
        return { qfr::ErrorCode::counterExhausted };
    next = highest + 1;
    return {};
}
}

namespace qfr
{
static_assert(std::atomic<uint64_t>::is_always_lock_free && std::atomic<float>::is_always_lock_free);
struct RecordingSession::Impl
{
    using Failure = FailureReason;
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
    RecorderResult updatePreview()
    {
        view.nextFile = juce::File{};
        auto valid = validate(config, options);
        if (valid.failed()) { view.resultCode = ResultCode::none; view.error = valid; return valid; }
        uint64_t next = 1;
        auto result = nextCounter(config, next);
        if (result.wasOk()) view.nextFile = config.targetFolder.getChildFile(numberedName(config.recordingPrefix, next));
        view.resultCode = result.failed() ? ResultCode::none : ResultCode::ready;
        view.error = result;
        return result;
    }
    void writeRecording(std::unique_ptr<juce::AudioFormatWriter> writer, juce::OutputStream* recordingOutput, std::shared_ptr<IoStatus> io,
                        juce::File partial, juce::File final, double rate, int channels)
    {
        std::array<float, 8192> block {};
        uint64_t encoded = 0;
        bool diskFlushFailed = false;
        auto nextDiskFlush = std::chrono::steady_clock::now() + options.diskFlushInterval;
        for (;;)
        {
            if (std::chrono::steady_clock::now() >= nextDiskFlush)
            {
                // FLAC has no codec flush API. Flush the encoded bytes through the
                // checked native stream; leave encoder state and metadata for Stop.
                try { recordingOutput->flush(); }
                catch (...) { io->failed = true; io->flushFailed = true; }
                if (io->failed) { diskFlushFailed = true; fail(Failure::diskFlush); break; }
                nextDiskFlush = std::chrono::steady_clock::now() + options.diskFlushInterval;
            }
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
        if (io->failed) fail(io->flushFailed ? Failure::diskFlush : Failure::writer);
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
        bool complete = finalizationValid && failure.load() != Failure::writer && failure.load() != Failure::diskFlush;
        const bool promotionFailed = complete && !promoteWithoutReplacement(partial, final, *io);
        if (promotionFailed) { fail(Failure::promotion); complete = false; }
        auto reason = failure.load();
        {
            std::lock_guard lock(mutex);
            view.recordingFile = complete ? final : partial;
            view.incomplete = !complete;
            view.failureReason = reason;
            view.finalization = complete ? FinalizationOutcome::finalized
                : promotionFailed ? FinalizationOutcome::promotionFailed
                : diskFlushFailed || io->flushFailed ? FinalizationOutcome::diskFlushFailed
                : FinalizationOutcome::writerFailed;
            view.error = diskResult(ErrorCode::none, *io);
            uint64_t next = 1;
            if (nextCounter(config, next).wasOk()) view.nextFile = config.targetFolder.getChildFile(numberedName(config.recordingPrefix, next));
        }
        state.store(reason == Failure::none ? State::succeeded : State::failed, std::memory_order_release);
    }
};
RecordingSession::RecordingSession() : RecordingSession(Options {}) {}
RecordingSession::RecordingSession(Options options) : impl(std::make_unique<Impl>(std::move(options))) {}
RecordingSession::~RecordingSession() { requestStop(); waitForStop(); }
RecorderResult RecordingSession::configure(const Configuration& config)
{
    std::lock_guard lock(impl->mutex);
    if (impl->locked()) return { qfr::ErrorCode::settingsLocked };
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
RecorderResult RecordingSession::refreshPreview()
{
    std::lock_guard lock(impl->mutex);
    if (impl->locked()) return { qfr::ErrorCode::filenameLocked };
    auto state = impl->state.load();
    auto error = impl->view.error;
    auto resultCode = impl->view.resultCode;
    auto result = impl->updatePreview();
    if (state == State::succeeded || state == State::failed)
    {
        impl->view.error = error;
        impl->view.resultCode = resultCode;
    }
    return result;
}
RecorderResult RecordingSession::start()
{
    std::shared_ptr<IoStatus> io;
    try
    {
        if (impl->locked()) return { qfr::ErrorCode::recordingBusy };
        waitForStop();
        std::lock_guard lock(impl->mutex);
        auto reject = [&](RecorderResult error)
        {
            impl->state = State::failed;
            impl->view.error = error;
            impl->view.failureReason = FailureReason::start;
            return error;
        };
        impl->view.recordingFile = juce::File{}; impl->view.incomplete = false; impl->capturedFrames = 0;
        impl->view.failureReason = FailureReason::none; impl->view.finalization = FinalizationOutcome::none;
        impl->view.inputDetail.clear();
        auto valid = impl->updatePreview();
        if (valid.failed()) return reject(valid);
        if (impl->options.captureCapacityFrames == 0 || impl->options.captureCapacityFrames > 16777216)
            return reject({ ErrorCode::invalidCapacity });
        if (impl->options.diskFlushInterval.count() <= 0)
            return reject({ ErrorCode::invalidFlushInterval });
        uint64_t next = 1;
        auto counter = nextCounter(impl->config, next);
        if (counter.failed()) return reject(counter);
        juce::File partial, final;
        io = std::make_shared<IoStatus>();
        io->nativeError = impl->options.nativeError;
        std::unique_ptr<juce::OutputStream> stream;
        for (int attempts = 0; attempts < 10000; ++attempts)
        {
            final = impl->config.targetFolder.getChildFile(numberedName(impl->config.recordingPrefix, next));
            partial = impl->config.targetFolder.getChildFile(numberedName(impl->config.recordingPrefix, next, true));
            auto result = final.exists() ? CreateResult::occupied : reserve(partial, io, stream);
            if (result == CreateResult::created) break;
            if (result == CreateResult::failed) return reject(diskResult(ErrorCode::createFailed, *io));
            if (next == std::numeric_limits<uint64_t>::max()) return reject({ ErrorCode::counterExhausted });
            ++next;
        }
        if (!stream) return reject({ ErrorCode::reservationFailed });
        impl->view.recordingFile = partial; impl->view.incomplete = true;
        if (impl->options.outputAdapter) stream = impl->options.outputAdapter(std::move(stream));
        if (!stream) return reject(diskResult(ErrorCode::outputOpenFailed, *io));
        stream = std::make_unique<CheckedOutput>(std::move(stream), io);
        auto* recordingOutput = stream.get(); // Owned by the successful writer until its destruction.
        juce::FlacAudioFormat flac;
        auto channels = impl->config.rightInputChannel < 0 ? 1 : 2;
        auto writer = flac.createWriterFor(stream, juce::AudioFormatWriterOptions().withSampleRate(impl->config.inputStreamRate)
                                                  .withNumChannels(channels).withBitsPerSample(16).withQualityOptionIndex(5));
        if (!writer || io->failed)
        {
            writer.reset(); stream.reset();
            return reject(diskResult(ErrorCode::writerStartFailed, *io));
        }
        impl->ring.resize(impl->options.captureCapacityFrames * 2);
        impl->head = 0; impl->tail = 0; impl->capturedFrames = 0; impl->failure = Impl::Failure::none;
        impl->view.error = {};
        impl->state = State::recording;
        impl->accepting = true;
        impl->worker = std::thread([p = impl.get(), writer = std::move(writer), recordingOutput, io, partial, final,
                                  rate = impl->config.inputStreamRate, channels]() mutable
        {
            try { p->writeRecording(std::move(writer), recordingOutput, io, partial, final, rate, channels); }
            catch (...)
            {
                p->accepting = false;
                while (p->inFlight.load() != 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
                std::lock_guard workerLock(p->mutex);
                p->view.recordingFile = partial; p->view.incomplete = true;
                p->view.failureReason = p->failure.load() == FailureReason::none ? FailureReason::writer : p->failure.load();
                p->view.finalization = io->flushFailed ? FinalizationOutcome::diskFlushFailed : FinalizationOutcome::writerFailed;
                p->view.error = diskResult(ErrorCode::none, *io);
                try { throw; }
                catch (const std::exception& error) { if (p->view.error.detail.isEmpty()) p->view.error.detail = juce::String::fromUTF8(error.what()); }
                catch (...) {}
                p->state = State::failed;
            }
        });
        return {};
    }
    catch (...)
    {
        impl->accepting = false;
        while (impl->inFlight.load() != 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        std::lock_guard lock(impl->mutex);
        impl->state = State::failed;
        impl->view.error = io ? diskResult(ErrorCode::startFailed, *io) : RecorderResult { ErrorCode::startFailed };
        try { throw; }
        catch (const std::exception& error) { if (impl->view.error.detail.isEmpty()) impl->view.error.detail = juce::String::fromUTF8(error.what()); }
        catch (...) {}
        impl->view.failureReason = FailureReason::start;
        return impl->view.error;
    }
}
void RecordingSession::requestStop()
{
    auto expected = State::recording;
    if (impl->state.compare_exchange_strong(expected, State::finalizing))
        impl->accepting = false;
}
void RecordingSession::waitForStop() { if (impl->worker.joinable()) impl->worker.join(); }
void RecordingSession::retainInputDiagnostic(const juce::String& detail)
{
    std::lock_guard lock(impl->mutex);
    if (impl->failure.load() == FailureReason::inputLost && impl->view.inputDetail.isEmpty())
        impl->view.inputDetail = detail;
}
RecordingSession::Snapshot RecordingSession::snapshot() const
{
    std::lock_guard lock(impl->mutex);
    auto result = impl->view;
    result.state = impl->state.load(std::memory_order_acquire);
    switch (result.state)
    {
        case State::idle: break;
        case State::recording: result.resultCode = ResultCode::recording; break;
        case State::finalizing: result.resultCode = ResultCode::finalizing; break;
        case State::succeeded: result.resultCode = ResultCode::saved; break;
        case State::failed: result.resultCode = ResultCode::failed; break;
    }
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
        if (capture) impl->fail(Impl::Failure::inputLost);
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
    if (impl->accepting.load()) impl->fail(Impl::Failure::inputLost);
    impl->inFlight.fetch_sub(1);
}
void RecordingSession::inputDiscontinuity() noexcept
{
    impl->inFlight.fetch_add(1);
    if (impl->accepting.load()) impl->fail(Impl::Failure::discontinuity);
    impl->inFlight.fetch_sub(1);
}
}
