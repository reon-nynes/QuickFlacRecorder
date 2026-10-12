// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_core/juce_core.h>

namespace qfr
{
enum class DiskOperation { none, create, write, seek, flush, close, promotion, cleanup };

struct DiskDiagnostic
{
    DiskOperation operation = DiskOperation::none;
    uint64_t nativeCode = 0; // GetLastError on Windows, errno on macOS/Linux.
    juce::String detail; // Original platform message, never translated or parsed.
};

enum class ErrorCode
{
    none, prefixLength, prefixCharacters, prefixReserved, folderMissing, folderNotWritable,
    folderCreateDenied, folderCleanupFailed, invalidRate, invalidChannels, monoChannelMissing,
    stereoChannelsMissing, settingsLocked, filenameLocked, recordingBusy, invalidCapacity,
    invalidFlushInterval, counterTooLarge, counterExhausted, createFailed, reservationFailed,
    outputOpenFailed, writerStartFailed, startFailed, driverUnavailable, deviceUnavailable,
    deviceCreateFailed, noInputChannels, deviceOpenFailed, deviceStartFailed, permissionDenied,
    deviceLost
};

// Stable outcome plus verbatim native data. No translated or app-authored diagnostic text.
struct RecorderResult
{
    ErrorCode code = ErrorCode::none;
    juce::String detail;
    juce::String driver;
    juce::String device;
    DiskDiagnostic disk;
    bool wasOk() const noexcept { return code == ErrorCode::none; }
    bool failed() const noexcept { return ! wasOk(); }
};
}
