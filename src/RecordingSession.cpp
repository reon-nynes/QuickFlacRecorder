// SPDX-License-Identifier: AGPL-3.0-only
#include "RecordingSession.h"
#include <limits>
#include <cmath>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>
#include <utility>
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
int64_t monotonicMillis()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
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
    std::function<bool(const juce::String&)> event;
    std::function<void()> interrupt;
    std::function<bool()> deadlineExpired;
    bool retries = false;
    juce::String lastDiagnostic;
    std::chrono::milliseconds retryInterval { 1000 }, timeout { 10000 };
    uint64_t injected(DiskOperation operation) const { return nativeError ? nativeError(operation, file) : 0; }
    void diagnose(DiskOperation operation, uint64_t code)
    {
        if (disk.operation == DiskOperation::none)
        {
            disk = { operation, code, nativeErrorMessage(code) };
            if (detail.isEmpty()) detail = disk.detail;
        }
    }
    void record(DiskOperation operation, uint64_t code)
    {
        diagnose(operation, code);
        failed = true;
        if (operation == DiskOperation::flush) flushFailed = true;
        if (event) event("Native disk operation failed; operation=" + juce::String(static_cast<int>(operation))
            + "; code=" + juce::String(code) + "; " + nativeErrorMessage(code));
    }
    bool retry(DiskOperation operation, const std::function<bool()>& action)
    {
        if (failed) return false;
        const auto began = std::chrono::steady_clock::now();
        const auto expired = [&] { return std::chrono::steady_clock::now() - began >= timeout || (deadlineExpired && deadlineExpired()); };
        for (int attempt = 0; ; ++attempt)
        {
            bool ok = false;
            juce::String diagnostic;
            try { ok = action(); }
            catch (const std::exception& error) { diagnostic = juce::String::fromUTF8(error.what()); if (detail.isEmpty()) detail = diagnostic; }
            catch (...) { diagnostic = "Output adapter exception"; }
            if (ok)
            {
                if (attempt > 0 && event && !event("Storage retry succeeded; operation=" + juce::String(static_cast<int>(operation)))) break;
                return true;
            }
            // The native stream already exhausted its own allowance. Retrying it
            // here would multiply the allowed retries after a fatal output error.
            if (failed) return false;
            if (interrupt) interrupt();
            if (event && !event("Storage operation failed; operation=" + juce::String(static_cast<int>(operation))
                + "; " + (diagnostic.isNotEmpty() ? diagnostic : lastDiagnostic.isNotEmpty() ? lastDiagnostic : detail))) break;
            if (!retries || attempt == 3 || expired()) break;
            if (event && !event("Storage retry " + juce::String(attempt + 1) + "/3; operation=" + juce::String(static_cast<int>(operation)))) break;
            const auto until = std::chrono::steady_clock::now() + retryInterval;
            while (std::chrono::steady_clock::now() < until && !expired())
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            if (expired()) break;
        }
        failed = true;
        if (operation == DiskOperation::flush) flushFailed = true;
        return false;
    }
    bool native(DiskOperation operation, const std::function<uint64_t()>& action)
    {
        return retry(operation, [&]
        {
            const auto code = injected(operation);
            const auto result = code != 0 ? code : action();
            if (result == 0) return true;
            diagnose(operation, result);
            lastDiagnostic = "Native error " + juce::String(result) + ": " + nativeErrorMessage(result);
            return false;
        });
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
        status->native(DiskOperation::flush, [&]() -> uint64_t
        {
#if JUCE_WINDOWS
            return FlushFileBuffers(handle) ? 0 : nativeErrorCode();
#else
            return ::fsync(handle) == 0 ? 0 : nativeErrorCode();
#endif
        });
    }
    bool setPosition(juce::int64 position) override
    {
        return status->native(DiskOperation::seek, [&]() -> uint64_t
        {
#if JUCE_WINDOWS
        LARGE_INTEGER offset; offset.QuadPart = position;
        bool ok = SetFilePointerEx(handle, offset, nullptr, FILE_BEGIN) != 0;
#else
        bool ok = ::lseek(handle, static_cast<off_t>(position), SEEK_SET) >= 0;
#endif
            return ok ? 0 : nativeErrorCode();
        });
    }
    juce::int64 getPosition() override
    {
        juce::int64 result = -1;
        status->native(DiskOperation::seek, [&]() -> uint64_t
        {
#if JUCE_WINDOWS
        LARGE_INTEGER offset {}, position {};
        if (!SetFilePointerEx(handle, offset, &position, FILE_CURRENT)) return nativeErrorCode();
        result = position.QuadPart;
#else
        auto position = ::lseek(handle, 0, SEEK_CUR);
        if (position < 0) return nativeErrorCode();
        result = static_cast<juce::int64>(position);
#endif
            return 0;
        });
        return result;
    }
    bool write(const void* data, size_t bytes) override
    {
        auto* source = static_cast<const char*>(data);
        return status->native(DiskOperation::write, [&]() -> uint64_t
        {
        while (bytes > 0)
        {
#if JUCE_WINDOWS
            DWORD written = 0;
            if (!WriteFile(handle, source, static_cast<DWORD>(std::min<size_t>(bytes, 0x7fffffff)), &written, nullptr))
            {
                const auto code = nativeErrorCode();
                source += written;
                bytes -= static_cast<size_t>(written);
                return code;
            }
            if (written == 0)
            { return ERROR_WRITE_FAULT; }
#else
            auto written = ::write(handle, source, bytes);
            if (written < 0 && errno == EINTR) continue;
            if (written < 0) return nativeErrorCode();
            if (written == 0)
            { return EIO; }
#endif
            source += written;
            bytes -= static_cast<size_t>(written);
        }
            return 0;
        });
    }
private:
    Handle handle;
    std::shared_ptr<IoStatus> status;
};

enum class CreateResult { created, occupied, failed, canceled };
CreateResult reserve(const juce::File& file, std::shared_ptr<IoStatus> status, std::unique_ptr<juce::OutputStream>& output,
                     const std::function<bool()>& canceled = {})
{
    status->file = file;
    const auto code = status->injected(DiskOperation::create);
    // Stop can cancel until this synchronous native allocation is admitted.
    // A successful reservation already in flight must still be finalized.
    if (canceled && canceled()) return CreateResult::canceled;
    if (code != 0) { status->record(DiskOperation::create, code); return CreateResult::failed; }
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
        status->retry(DiskOperation::flush, [&] { output->flush(); return !status->failed; });
    }
    bool setPosition(juce::int64 position) override { return status->retry(DiskOperation::seek, [&] { return output->setPosition(position); }); }
    juce::int64 getPosition() override
    {
        juce::int64 position = -1;
        status->retry(DiskOperation::seek, [&] { position = output->getPosition(); return position >= 0; });
        return position;
    }
    bool write(const void* data, size_t bytes) override
    {
        const auto begin = getPosition();
        auto* source = static_cast<const char*>(data);
        size_t remaining = bytes;
        return status->retry(DiskOperation::write, [&]
        {
            std::exception_ptr exception;
            try { if (output->write(source, remaining)) return true; }
            catch (...) { exception = std::current_exception(); }
            if (status->failed) return false;
            // Output adapters may report failure after a partial native write.
            // Resume only bytes whose advancement the reserved handle proves.
            const auto position = output->getPosition();
            if (begin < 0 || position < begin || position - begin > static_cast<juce::int64>(bytes))
            { status->failed = true; return false; }
            const auto saved = static_cast<size_t>(position - begin);
            source = static_cast<const char*>(data) + saved;
            remaining = bytes - saved;
            if (exception) std::rethrow_exception(exception);
            return remaining == 0;
        });
    }
private:
    bool check(bool ok) { if (!ok) status->failed = true; return ok; }
    std::unique_ptr<juce::OutputStream> output;
    std::shared_ptr<IoStatus> status;
};

// Only the control/writer thread accesses a log. Creation is lazy and exclusive.
class RecordingLog final
{
public:
    RecordingLog(juce::File fileIn, const qfr::RecordingSession::Options& options)
        : file(std::move(fileIn)), status(std::make_shared<IoStatus>()) { status->nativeError = options.nativeError; }
    bool append(double position, const juce::String& event)
    {
        if (status->failed) return false;
        if (!output && reserve(file, status, output) != CreateResult::created) return false;
        const auto line = juce::Time::getCurrentTime().formatted("%Y-%m-%dT%H:%M:%S%z")
            + " position=" + juce::String(position, 3) + "s " + event + "\n";
        if (!output->write(line.toRawUTF8(), line.getNumBytesAsUTF8())) return false;
        output->flush();
        return !status->failed;
    }
    bool finish() { output.reset(); return !status->failed; }
    juce::File file;
    std::shared_ptr<IoStatus> status;
private:
    std::unique_ptr<juce::OutputStream> output;
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
qfr::RecorderResult nextCounter(const qfr::RecordingSession::Configuration& config, uint64_t& next,
                              const std::function<juce::Array<juce::File>(const juce::File&)>& entries = {})
{
    uint64_t highest = 0;
    const auto beginning = config.recordingPrefix + "_";
    for (const auto& file : entries ? entries(config.targetFolder)
                                  : config.targetFolder.findChildFiles(juce::File::findFilesAndDirectories, false))
    {
        auto name = file.getFileName();
        if (!name.startsWith(beginning)) continue;
        auto suffix = name.substring(beginning.length());
        if (suffix.endsWithIgnoreCase(".partial.flac")) suffix = suffix.dropLastCharacters(13);
        else if (suffix.endsWithIgnoreCase(".flac")) suffix = suffix.dropLastCharacters(5);
        else if (suffix.endsWithIgnoreCase(".log")) suffix = suffix.dropLastCharacters(4);
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
    std::atomic<bool> terminalFailure { false }, hasErrors { false };
    std::atomic<uint64_t> discontinuities { 0 };
    std::atomic<uint64_t> droppedFrames { 0 };
    std::atomic<bool> stopping { false }, inputFault { false }, reopenRequested { false }, rolloverPending { false };
    std::atomic<uint64_t> faultHead { 0 };
    std::atomic<int64_t> startedAt { 0 }, endedAt { 0 }, lastCallback { 0 }, recoverySince { 0 };
    std::atomic<unsigned> recoveryAttempts { 0 };
    bool inputRecovering = false;
    // Protected by mutex; a request stays in flight until the writer collects its result.
    bool reopenInFlight = false, reopenStarted = false;
    uint64_t recoveryInterruption = 1;
    struct InputRecoveryAttempt
    {
        uint64_t interruption = 0;
        unsigned number = 0;
    } reopenAttempt;
    unsigned failedRecoveryAttempts = 0;
    std::atomic<double> actualRate { 0 }, writerRate { 0 };
    std::atomic<int> availableChannels { 0 };
    enum class ReopenOutcome { none, reopened, failed };
    ReopenOutcome reopenOutcome = ReopenOutcome::none;
    juce::String lastInputDetail;
    std::vector<juce::String> pendingInputDetails;
    uint64_t recoveryThreshold = 0;
    int64_t recoveryAttemptStart = 0, lastProgress = 0;
    std::shared_ptr<RecordingLog> log, failedLog;
    std::vector<float> ring;
    std::thread worker;

    struct FileContext
    {
        std::shared_ptr<IoStatus> io;
        std::unique_ptr<juce::AudioFormatWriter> writer;
        juce::OutputStream* output = nullptr;
        juce::File partial, final;
        bool reserved = false;
    };

    RecorderResult openFile(FileContext& file, double rate, bool rollover = false)
    {
        const auto canceled = [this, rollover] { return rollover && stopping; };
        if (canceled()) return {};
        uint64_t next = 1;
        auto counter = nextCounter(config, next, options.allocationEntries);
        if (canceled()) return {};
        if (counter.failed()) return counter;
        file.io = std::make_shared<IoStatus>();
        file.io->nativeError = options.nativeError;
        std::unique_ptr<juce::OutputStream> stream;
        for (int attempts = 0; attempts < 10000; ++attempts)
        {
            if (canceled()) return {};
            file.final = config.targetFolder.getChildFile(numberedName(config.recordingPrefix, next));
            file.partial = config.targetFolder.getChildFile(numberedName(config.recordingPrefix, next, true));
            const auto result = file.final.exists() || file.final.withFileExtension(".log").exists()
                ? CreateResult::occupied : reserve(file.partial, file.io, stream, canceled);
            if (result == CreateResult::canceled) return {};
            if (result == CreateResult::created) { file.reserved = true; break; }
            if (result == CreateResult::failed) return diskResult(ErrorCode::createFailed, *file.io);
            if (next == std::numeric_limits<uint64_t>::max()) return { ErrorCode::counterExhausted };
            ++next;
        }
        if (!stream) return { ErrorCode::reservationFailed };
        log = std::make_shared<RecordingLog>(file.final.withFileExtension(".log"), options);
        file.io->retries = true;
        file.io->retryInterval = options.storageRetryInterval;
        file.io->timeout = options.noProgressTimeout;
        file.io->event = [this, recordingLog = log](const juce::String& message)
        {
            return eventTo(recordingLog, message);
        };
        file.io->interrupt = [this]
        {
            if (recoverySince == 0 && !terminalFailure)
            {
                recoverySince = monotonicMillis(); recoveryThreshold = tail;
                auto expected = State::recording;
                state.compare_exchange_strong(expected, State::recovering);
            }
        };
        file.io->deadlineExpired = [this]
        {
            return recoverySince != 0 && monotonicMillis() - recoverySince >= options.noProgressTimeout.count();
        };
        if (options.outputAdapter) stream = options.outputAdapter(std::move(stream));
        if (!stream) return diskResult(ErrorCode::outputOpenFailed, *file.io);
        stream = std::make_unique<CheckedOutput>(std::move(stream), file.io);
        file.output = stream.get();
        juce::FlacAudioFormat flac;
        file.writer = flac.createWriterFor(stream, juce::AudioFormatWriterOptions().withSampleRate(rate)
            .withNumChannels(config.rightInputChannel < 0 ? 1 : 2).withBitsPerSample(16).withQualityOptionIndex(5));
        if (!file.writer || file.io->failed)
        {
            file.writer.reset(); stream.reset();
            return diskResult(ErrorCode::writerStartFailed, *file.io);
        }
        return {};
    }

    bool locked() const { auto current = state.load(); return current == State::recording || current == State::recovering || current == State::finalizing; }
    void fail(Failure reason) noexcept
    {
        if (reason == Failure::log) failure = reason;
        else { auto expected = Failure::none; failure.compare_exchange_strong(expected, reason); }
        terminalFailure = true;
        reopenRequested = false;
        accepting = false;
        state = State::finalizing;
    }
    bool event(const juce::String& message)
    {
        return eventTo(log, message);
    }
    bool eventTo(const std::shared_ptr<RecordingLog>& recordingLog, const juce::String& message)
    {
        hasErrors = true;
        try
        {
            if (recordingLog && recordingLog->append(startedAt > 0 ? static_cast<double>(monotonicMillis() - startedAt) / 1000 : 0, message))
                return true;
        }
        catch (...) {} // Failure to persist a required diagnostic is terminal.
        if (!failedLog) failedLog = recordingLog;
        fail(Failure::log);
        return false;
    }
    void finishLog(const std::shared_ptr<RecordingLog>& recordingLog)
    {
        if (recordingLog && !recordingLog->finish())
        {
            if (!failedLog) failedLog = recordingLog;
            fail(Failure::log);
        }
    }
    // Caller holds mutex when publishing diagnostics to the control-thread view.
    void publishLogFailure()
    {
        if (!failedLog) return;
        view.failureReason = Failure::log;
        view.logFile = failedLog->file;
        view.logError = diskResult(ErrorCode::none, *failedLog->status);
        if (view.error.disk.operation == DiskOperation::none)
            view.error = diskResult(view.error.code, *failedLog->status);
    }
    void logCallbackErrors()
    {
        const auto count = discontinuities.exchange(0);
        if (count != 0)
            event("Missing input samples; count=" + juce::String(count) + "; gap duration unknown (driver supplied an xrun count).");
        const auto dropped = droppedFrames.exchange(0);
        if (dropped != 0)
            event("Capture buffer overflow; omitted frames=" + juce::String(dropped)
                + "; known gap duration=" + juce::String(static_cast<double>(dropped) / config.inputStreamRate, 6) + "s.");
    }
    void beginRecovery(std::vector<juce::String>& events, uint64_t threshold, const juce::String& message, int64_t since = 0)
    {
        if (stopping || terminalFailure || inputRecovering) return;
        if (recoverySince == 0) { recoverySince = since > 0 ? since : monotonicMillis(); recoveryThreshold = threshold; }
        inputRecovering = true;
        recoveryAttemptStart = monotonicMillis();
        auto expected = State::recording;
        state.compare_exchange_strong(expected, State::recovering);
        events.push_back(message + "; recording gap begins; missing duration estimated from elapsed time.");
    }
    void logInputDiagnostics()
    {
        std::vector<juce::String> diagnostics;
        {
            std::lock_guard lock(mutex);
            diagnostics.swap(pendingInputDetails);
        }
        for (const auto& diagnostic : diagnostics) event("Native input diagnostic: " + diagnostic);
    }
    // Caller holds mutex. Capture event text with the state it describes, then
    // let the writer persist it after releasing the control-state mutex.
    void collectRecoveryOutcome(std::vector<juce::String>& events)
    {
        if (reopenStarted)
        {
            reopenStarted = false;
            events.push_back("Input recovery attempt " + juce::String(reopenAttempt.number)
                + "; interruption=" + juce::String(reopenAttempt.interruption));
        }
        const auto outcome = std::exchange(reopenOutcome, ReopenOutcome::none);
        if (outcome == ReopenOutcome::none) return;
        reopenInFlight = false;
        const bool current = reopenAttempt.interruption == recoveryInterruption;
        if (outcome == ReopenOutcome::failed && current && recoverySince != 0) ++failedRecoveryAttempts;
        const juce::String message = outcome == ReopenOutcome::reopened ? "Selected input reopened" : "Selected input recovery attempt failed";
        events.push_back(message + "; interruption=" + juce::String(reopenAttempt.interruption)
            + "; attempt=" + juce::String(reopenAttempt.number)
            + (!current ? "; earlier interruption; current allowance unchanged."
                : outcome == ReopenOutcome::reopened ? "; waiting for saved incoming audio." : "."));
    }
    void logRecoveryOutcome()
    {
        std::vector<juce::String> events;
        {
            std::lock_guard lock(mutex);
            collectRecoveryOutcome(events);
        }
        for (const auto& message : events) event(message);
    }
    // Caller holds mutex. This only updates state and collects writer-owned text.
    void collectRecoveryEvents(std::vector<juce::String>& events)
    {
        collectRecoveryOutcome(events);
        if (stopping || terminalFailure) return;
        if (failedRecoveryAttempts >= 3) { fail(Failure::inputLost); return; }
        if (inputFault.exchange(false))
        {
            const auto threshold = faultHead.load();
            // A notification can arrive while the writer is persisting a log
            // event. Audio saved beyond its capture position already recovered
            // that interruption; do not replay it after savedProgress reset it.
            if (threshold >= tail.load())
                beginRecovery(events, threshold, "Input device interrupted");
            else
                events.push_back("Input device interrupted before already-saved incoming audio; recording gap duration unknown.");
        }
        if (!inputRecovering && monotonicMillis() - lastCallback >= options.callbackTimeout.count())
        {
            auto expected = Failure::none;
            failure.compare_exchange_strong(expected, Failure::inputLost);
            beginRecovery(events, head, "Valid input callbacks disappeared", lastProgress);
        }
        if (recoverySince == 0) return;
        const auto duration = monotonicMillis() - recoverySince;
        if (duration >= options.noProgressTimeout.count())
        {
            events.push_back("Input recovery failed; attempts=" + juce::String(recoveryAttempts.load())
                + "; no recording progress for " + juce::String(duration) + "ms.");
            fail(Failure::inputLost);
        }
        else if (inputRecovering && !reopenInFlight && !reopenRequested
            && monotonicMillis() - recoveryAttemptStart >= static_cast<int64_t>(recoveryAttempts.load()) * options.recoveryInterval.count())
        {
            reopenRequested = true;
        }
    }
    void serviceRecovery()
    {
        logInputDiagnostics();
        std::vector<juce::String> events;
        {
            std::lock_guard lock(mutex);
            collectRecoveryEvents(events);
        }
        for (const auto& message : events) event(message);
    }
    void savedProgress(uint64_t through)
    {
        std::vector<juce::String> events;
        {
            std::lock_guard lock(mutex);
            collectRecoveryOutcome(events);
            lastProgress = monotonicMillis();
            if (recoverySince != 0 && through > recoveryThreshold)
            {
                events.push_back("Recovery succeeded; recording gap estimated duration="
                    + juce::String(monotonicMillis() - recoverySince) + "ms; missing audio omitted.");
                recoverySince = 0; recoveryAttempts = 0; reopenRequested = false;
                failedRecoveryAttempts = 0;
                ++recoveryInterruption;
                inputRecovering = false;
                auto expected = State::recovering;
                state.compare_exchange_strong(expected, State::recording);
            }
        }
        for (const auto& message : events) event(message);
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
            logCallbackErrors();
            serviceRecovery();
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
                    event("FLAC encoder could not save captured audio; storage recovery did not succeed.");
                    fail(Failure::writer);
                    break;
                }
                encoded += static_cast<uint64_t>(count);
                savedProgress(read + static_cast<uint64_t>(count));
                tail.store(read + static_cast<uint64_t>(count), std::memory_order_release);
                continue;
            }
            if ((!accepting.load() || stopping) && inFlight.load() == 0 && head.load(std::memory_order_acquire) == read) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        // A writer failure can stop the consumer while one callback is copying its block.
        // Keep the take locked until that producer is quiescent, before another start can resize/reset storage.
        while (inFlight.load() != 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        logCallbackErrors();
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
                event("FLAC finalization validation failed; saved format or frame count does not match captured audio.");
                fail(Failure::writer);
            }
        }
        bool complete = finalizationValid && failure.load() != Failure::writer && failure.load() != Failure::diskFlush;
        const bool promotionFailed = complete && !promoteWithoutReplacement(partial, final, *io);
        if (promotionFailed) { fail(Failure::promotion); complete = false; }
        logInputDiagnostics();
        logRecoveryOutcome();
        if (hasErrors) event(complete ? "Recording finalized: " + final.getFileName() : "Recording incomplete: " + partial.getFileName());
        const bool rolling = !stopping && !terminalFailure && rolloverPending;
        if (!rolling) finishLog(log);
        const auto reason = failure.load();
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
            publishLogFailure();
            uint64_t next = 1;
            if (nextCounter(config, next).wasOk()) view.nextFile = config.targetFolder.getChildFile(numberedName(config.recordingPrefix, next));
        }
        endedAt = rolling ? 0 : monotonicMillis();
        state.store(rolling ? State::recovering : complete && !terminalFailure ? State::succeeded : State::failed, std::memory_order_release);
    }
    void cancelRollover(const juce::String& message)
    {
        event(message);
        fail(Failure::inputLost);
        finishLog(log);
        std::lock_guard lock(mutex);
        view.failureReason = failure;
        publishLogFailure();
        endedAt = monotonicMillis();
        state = State::failed;
    }
    void run(FileContext file, double rate, int channels)
    {
        for (;;)
        {
            writeRecording(std::move(file.writer), file.output, file.io, file.partial, file.final, rate, channels);
            if (stopping || terminalFailure || !rolloverPending.exchange(false)) return;
            if (recoverySince != 0 && monotonicMillis() - recoverySince >= options.noProgressTimeout.count())
            { cancelRollover("Recording rollover canceled at the no-progress deadline."); return; }
            const auto nextRate = actualRate.load();
            const auto previous = file.final;
            const auto previousLog = log;
            if (!std::isfinite(nextRate) || nextRate < 1 || nextRate > 655350 || std::floor(nextRate) != nextRate)
            { cancelRollover("Cannot roll over to an invalid input stream rate."); return; }
            FileContext next;
            const auto opened = openFile(next, nextRate, true);
            if (!next.reserved && stopping)
            {
                // Cancellation before reservation keeps the prior file's outcome.
                finishLog(previousLog);
                std::lock_guard lock(mutex);
                view.failureReason = failure;
                publishLogFailure();
                endedAt = monotonicMillis();
                state = terminalFailure ? State::failed : State::succeeded;
                return;
            }
            if (opened.failed())
            {
                eventTo(previousLog, "Recording rollover could not start: " + next.partial.getFileName() + "; " + opened.detail);
                finishLog(previousLog);
                finishLog(log);
                fail(Failure::start);
                std::lock_guard lock(mutex);
                view.error = opened;
                view.failureReason = failure;
                if (next.reserved)
                {
                    view.recordingFile = next.partial; view.incomplete = true; view.logFile = log->file;
                    view.finalization = FinalizationOutcome::writerFailed;
                }
                publishLogFailure();
                endedAt = monotonicMillis();
                state = State::failed;
                return;
            }
            const auto link = "Recording rollover from " + previous.getFileName() + " to " + next.final.getFileName()
                + "; input stream rate " + juce::String(rate, 0) + " -> " + juce::String(nextRate, 0) + " Hz.";
            if (eventTo(previousLog, link)) event(link);
            finishLog(previousLog);
            // Once reserved, the next file must go through normal finalization
            // even when Stop or a required-log failure prevents accepting audio.
            file = std::move(next);
            rate = nextRate;
            writerRate = rate;
            startedAt = monotonicMillis(); endedAt = 0; lastCallback = startedAt.load();
            {
                std::lock_guard lock(mutex);
                config.inputStreamRate = rate;
                view.recordingFile = file.partial; view.incomplete = true;
                view.logFile = log->file; view.inputStreamRate = rate;
                view.inputDetail.clear(); view.error = {}; view.finalization = FinalizationOutcome::none;
            }
            accepting = !stopping && !terminalFailure && actualRate == rate && !rolloverPending;
            state = stopping || terminalFailure ? State::finalizing : State::recovering;
        }
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
    impl->startedAt = 0; impl->endedAt = 0;
    impl->view.inputStreamRate = config.inputStreamRate;
    impl->view.outputChannels = config.rightInputChannel < 0 ? 1 : 2;
    impl->view.leftInputChannel = config.leftInputChannel; impl->view.rightInputChannel = config.rightInputChannel;
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
    Impl::FileContext file;
    try
    {
        if (impl->locked()) return { qfr::ErrorCode::recordingBusy };
        waitForStop();
        std::lock_guard lock(impl->mutex);
        auto reject = [&](RecorderResult error)
        {
            file.writer.reset();
            impl->finishLog(impl->log);
            impl->view.error = error;
            impl->view.failureReason = FailureReason::start;
            if (file.reserved) impl->view.finalization = FinalizationOutcome::writerFailed;
            impl->publishLogFailure();
            impl->endedAt = monotonicMillis();
            impl->state = State::failed;
            return error;
        };
        impl->view.recordingFile = juce::File{}; impl->view.incomplete = false; impl->capturedFrames = 0;
        impl->head = 0; impl->tail = 0; impl->failure = Impl::Failure::none;
        impl->view.failureReason = FailureReason::none; impl->view.finalization = FinalizationOutcome::none;
        impl->view.inputDetail.clear();
        impl->view.logError = {};
        impl->log.reset(); impl->failedLog.reset();
        impl->hasErrors = false; impl->terminalFailure = false; impl->discontinuities = 0;
        impl->stopping = false; impl->inputFault = false; impl->reopenRequested = false;
        impl->rolloverPending = false;
        impl->recoverySince = 0; impl->recoveryAttempts = 0;
        impl->inputRecovering = false; impl->reopenOutcome = Impl::ReopenOutcome::none; impl->droppedFrames = 0;
        impl->reopenInFlight = false; impl->reopenStarted = false; impl->failedRecoveryAttempts = 0;
        impl->recoveryInterruption = 1; impl->reopenAttempt = {};
        impl->pendingInputDetails.clear(); impl->lastInputDetail.clear();
        impl->startedAt = monotonicMillis(); impl->endedAt = 0; impl->lastCallback = impl->startedAt.load();
        impl->lastProgress = impl->startedAt;
        impl->actualRate = impl->config.inputStreamRate;
        impl->writerRate = impl->config.inputStreamRate;
        auto valid = impl->updatePreview();
        if (valid.failed()) return reject(valid);
        if (impl->options.captureCapacityFrames == 0 || impl->options.captureCapacityFrames > 16777216)
            return reject({ ErrorCode::invalidCapacity });
        if (impl->options.diskFlushInterval.count() <= 0 || impl->options.storageRetryInterval.count() < 0
            || impl->options.noProgressTimeout.count() <= 0 || impl->options.callbackTimeout.count() <= 0
            || impl->options.recoveryInterval.count() <= 0)
            return reject({ ErrorCode::invalidFlushInterval });
        auto channels = impl->config.rightInputChannel < 0 ? 1 : 2;
        const auto opened = impl->openFile(file, impl->config.inputStreamRate);
        io = file.io;
        impl->view.recordingFile = file.reserved ? file.partial : juce::File();
        impl->view.incomplete = file.reserved;
        impl->view.logFile = impl->log ? impl->log->file : juce::File();
        if (opened.failed()) return reject(opened);
        impl->ring.resize(impl->options.captureCapacityFrames * 2);
        impl->view.error = {};
        impl->state = impl->recoverySince != 0 ? State::recovering : State::recording;
        impl->accepting = true;
        impl->worker = std::thread([p = impl.get(), file = std::move(file), io,
                                  rate = impl->config.inputStreamRate, channels]() mutable
        {
            try { p->run(std::move(file), rate, channels); }
            catch (...)
            {
                p->accepting = false;
                while (p->inFlight.load() != 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
                std::lock_guard workerLock(p->mutex);
                p->view.incomplete = true;
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
        if (file.reserved) { impl->view.recordingFile = file.partial; impl->view.incomplete = true; }
        if (!io) io = file.io;
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
    impl->stopping = true;
    impl->reopenRequested = false;
    impl->accepting = false;
    auto expected = State::recording;
    if (!impl->state.compare_exchange_strong(expected, State::finalizing))
    {
        expected = State::recovering;
        impl->state.compare_exchange_strong(expected, State::finalizing);
    }
}
void RecordingSession::waitForStop() { if (impl->worker.joinable()) impl->worker.join(); }
void RecordingSession::retainInputDiagnostic(const juce::String& detail, uint64_t occurrences)
{
    std::lock_guard lock(impl->mutex);
    if (detail.isNotEmpty() && (impl->locked() || impl->failure.load() == FailureReason::inputLost))
    {
        if (impl->view.inputDetail.isEmpty()) impl->view.inputDetail = detail;
        if (impl->locked() && (occurrences != 0 || detail != impl->lastInputDetail))
        {
            impl->pendingInputDetails.push_back(detail + (occurrences != 0 ? "; count=" + juce::String(occurrences) : juce::String()));
            impl->lastInputDetail = detail;
        }
    }
}
bool RecordingSession::takeInputRecoveryRequest()
{
    std::lock_guard lock(impl->mutex);
    if (impl->stopping || impl->terminalFailure || impl->reopenInFlight || !impl->reopenRequested.exchange(false)) return false;
    impl->reopenInFlight = true;
    impl->reopenStarted = true;
    impl->reopenAttempt = { impl->recoveryInterruption, ++impl->recoveryAttempts };
    return true;
}
void RecordingSession::inputRecoveryFinished(const RecorderResult& result)
{
    if (impl->stopping || impl->terminalFailure) return;
    retainInputDiagnostic(result.detail);
    std::lock_guard lock(impl->mutex);
    if (!impl->reopenInFlight) return;
    impl->reopenOutcome = result.wasOk() ? Impl::ReopenOutcome::reopened : Impl::ReopenOutcome::failed;
}
void RecordingSession::inputFormatChanged(double rate, int channels) noexcept
{
    if (!impl->locked() || impl->stopping || impl->terminalFailure) return;
    impl->availableChannels = channels;
    if (impl->actualRate.exchange(rate) != rate)
    {
        impl->rolloverPending = true;
        impl->faultHead = impl->head.load();
        impl->inputFault = true;
        impl->accepting = false;
    }
}
RecordingSession::Snapshot RecordingSession::snapshot() const
{
    std::lock_guard lock(impl->mutex);
    auto result = impl->view;
    result.hasRecordingErrors = impl->hasErrors;
    result.recoveryAttempts = impl->recoveryAttempts;
    result.state = impl->state.load(std::memory_order_acquire);
    switch (result.state)
    {
        case State::idle: break;
        case State::recording: result.resultCode = ResultCode::recording; break;
        case State::recovering: result.resultCode = ResultCode::recovering; break;
        case State::finalizing: result.resultCode = ResultCode::finalizing; break;
        case State::succeeded: result.resultCode = ResultCode::saved; break;
        case State::failed: result.resultCode = ResultCode::failed; break;
    }
    result.elapsedSeconds = impl->startedAt > 0 ? static_cast<double>((impl->endedAt > 0 ? impl->endedAt.load() : monotonicMillis()) - impl->startedAt) / 1000 : 0;
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
    bool capture = impl->accepting.load() && !impl->stopping && !impl->terminalFailure
        && impl->actualRate.load() == impl->writerRate.load();
    bool valid = inputs && left >= 0 && left < inputChannels && inputs[left]
                 && (channels == 1 || (right < inputChannels && inputs[right]));
    if (!valid)
    {
        if (capture) inputLost();
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
        impl->lastCallback = monotonicMillis();
        auto write = impl->head.load(std::memory_order_relaxed);
        auto read = impl->tail.load(std::memory_order_acquire);
        if (static_cast<uint64_t>(frames) > impl->options.captureCapacityFrames - (write - read))
        {
            auto expected = FailureReason::none;
            impl->failure.compare_exchange_strong(expected, FailureReason::overflow);
            impl->droppedFrames.fetch_add(static_cast<uint64_t>(frames));
        }
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
    if (impl->accepting.load() && !impl->stopping && !impl->terminalFailure)
    {
        auto expected = FailureReason::none;
        impl->failure.compare_exchange_strong(expected, FailureReason::inputLost);
        impl->faultHead = impl->head.load();
        impl->inputFault = true;
    }
    impl->inFlight.fetch_sub(1);
}
void RecordingSession::inputDiscontinuity(uint64_t count) noexcept
{
    impl->inFlight.fetch_add(1);
    if (impl->accepting.load())
    {
        auto expected = FailureReason::none;
        impl->failure.compare_exchange_strong(expected, FailureReason::discontinuity);
        impl->discontinuities.fetch_add(count);
    }
    impl->inFlight.fetch_sub(1);
}
}
