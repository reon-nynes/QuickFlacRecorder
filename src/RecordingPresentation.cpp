// SPDX-License-Identifier: AGPL-3.0-only
#include "RecordingPresentation.h"

namespace qfr
{
RecordingAction recordingAction(RecordingSession::State state) noexcept
{
    if (state == RecordingSession::State::recording) return RecordingAction::stop;
    if (state == RecordingSession::State::finalizing) return RecordingAction::finalizing;
    return RecordingAction::record;
}

juce::String resultMessage(const RecorderResult& result, const i18n::Translation& translation, const juce::File& retained)
{
    using i18n::MessageId;
    const juce::String diagnostic = result.detail.isNotEmpty() ? "yes" : "no";
    const auto startFailure = [&](MessageId id)
    {
        return translation.text(id, { { "retained", juce::String(retained == juce::File() ? "no" : "yes") },
            { "path", retained.getFullPathName() }, { "diagnostic", diagnostic }, { "detail", result.detail } });
    };
    const auto inputFailure = [&](MessageId id)
    {
        return translation.text(id, { { "driver", result.driver }, { "device", result.device },
            { "diagnostic", diagnostic }, { "detail", result.detail } });
    };
    switch (result.code)
    {
        case ErrorCode::none: return {};
        case ErrorCode::prefixLength: return translation.text(MessageId::PrefixLength);
        case ErrorCode::prefixCharacters: return translation.text(MessageId::PrefixCharacters);
        case ErrorCode::prefixReserved: return translation.text(MessageId::PrefixReserved);
        case ErrorCode::folderMissing: return translation.text(MessageId::FolderMissing);
        case ErrorCode::folderNotWritable: return translation.text(MessageId::FolderNotWritable);
        case ErrorCode::folderCreateDenied: return translation.text(MessageId::FolderCreateDenied);
        case ErrorCode::folderCleanupFailed: return translation.text(MessageId::FolderCleanupFailed);
        case ErrorCode::invalidRate: return translation.text(MessageId::InvalidRate);
        case ErrorCode::invalidChannels: return translation.text(MessageId::InvalidChannels);
        case ErrorCode::monoChannelMissing: return translation.text(MessageId::MonoChannelMissing);
        case ErrorCode::stereoChannelsMissing: return translation.text(MessageId::StereoChannelsMissing);
        case ErrorCode::settingsLocked: return translation.text(MessageId::SettingsLocked);
        case ErrorCode::filenameLocked: return translation.text(MessageId::FilenameLocked);
        case ErrorCode::recordingBusy: return translation.text(MessageId::RecordingBusy);
        case ErrorCode::invalidCapacity: return translation.text(MessageId::InvalidCapacity);
        case ErrorCode::invalidFlushInterval: return translation.text(MessageId::InvalidFlushInterval);
        case ErrorCode::counterTooLarge: return translation.text(MessageId::CounterTooLarge);
        case ErrorCode::counterExhausted: return translation.text(MessageId::CounterExhausted);
        case ErrorCode::createFailed: return translation.text(MessageId::CreateFailed);
        case ErrorCode::reservationFailed: return translation.text(MessageId::ReservationFailed);
        case ErrorCode::permissionDenied: return translation.text(MessageId::PermissionDenied);
        case ErrorCode::outputOpenFailed: return startFailure(MessageId::OutputOpenFailed);
        case ErrorCode::writerStartFailed: return startFailure(MessageId::WriterStartFailed);
        case ErrorCode::startFailed: return startFailure(MessageId::StartFailed);
        case ErrorCode::driverUnavailable: return inputFailure(MessageId::DriverUnavailable);
        case ErrorCode::deviceUnavailable: return inputFailure(MessageId::DeviceUnavailable);
        case ErrorCode::deviceCreateFailed: return inputFailure(MessageId::DeviceCreateFailed);
        case ErrorCode::noInputChannels: return inputFailure(MessageId::NoInputChannels);
        case ErrorCode::deviceStartFailed: return inputFailure(MessageId::DeviceStartFailed);
        case ErrorCode::deviceLost: return inputFailure(MessageId::DeviceLost);
        case ErrorCode::deviceOpenFailed:
        {
           #if JUCE_WINDOWS
            const juce::String platform = "windows";
           #elif JUCE_MAC
            const juce::String platform = "mac";
           #else
            const juce::String platform = "other";
           #endif
            return translation.text(MessageId::DeviceOpenFailed, { { "driver", result.driver }, { "device", result.device },
                { "platform", platform }, { "detail", result.detail } });
        }
    }
    return {};

}
juce::String incompleteRecordingWarning(int count, const juce::String& path, const i18n::Translation& translation)
{
    return translation.text(i18n::MessageId::IncompleteRecordings, { { "count", static_cast<double>(count) }, { "path", path } });
}

juce::String recordingStatus(const RecordingSession::Snapshot& snapshot, const i18n::Translation& translation)
{
    using i18n::MessageId;
    switch (snapshot.resultCode)
    {
        case RecordingSession::ResultCode::ready: return translation.text(MessageId::Ready);
        case RecordingSession::ResultCode::recording: return translation.text(MessageId::Recording, { { "filename", snapshot.recordingFile.getFileName() } });
        case RecordingSession::ResultCode::finalizing: return translation.text(MessageId::Finalizing, { { "filename", snapshot.recordingFile.getFileName() } });
        case RecordingSession::ResultCode::saved: return translation.text(MessageId::Saved, { { "filename", snapshot.recordingFile.getFileName() } });
        case RecordingSession::ResultCode::none: return resultMessage(snapshot.error, translation);
        case RecordingSession::ResultCode::failed:
        {
            if (snapshot.error.failed()) return resultMessage(snapshot.error, translation, snapshot.incomplete ? snapshot.recordingFile : juce::File());
            juce::String reason = "writer", outcome = "writerFailed";
            switch (snapshot.failureReason)
            {
                case RecordingSession::FailureReason::inputLost: reason = "inputLost"; break;
                case RecordingSession::FailureReason::discontinuity: reason = "discontinuity"; break;
                case RecordingSession::FailureReason::overflow: reason = "overflow"; break;
                case RecordingSession::FailureReason::diskFlush: reason = "diskFlush"; break;
                case RecordingSession::FailureReason::promotion: reason = "promotion"; break;
                default: break;
            }
            switch (snapshot.finalization)
            {
                case RecordingSession::FinalizationOutcome::finalized: outcome = "finalized"; break;
                case RecordingSession::FinalizationOutcome::diskFlushFailed: outcome = "diskFlushFailed"; break;
                case RecordingSession::FinalizationOutcome::promotionFailed: outcome = "promotionFailed"; break;
                default: break;
            }
            return translation.text(MessageId::RecordingFailure, { { "reason", reason }, { "outcome", outcome },
                { "path", snapshot.recordingFile.getFullPathName() }, { "detail", snapshot.error.detail },
                { "inputDetail", snapshot.inputDetail },
                { "inputDiagnostic", juce::String(snapshot.inputDetail.isNotEmpty() ? "yes" : "no") },
                { "diagnostic", juce::String(snapshot.error.detail.isNotEmpty() ? "yes" : "no") } });
        }
    }
    return {};
}
}
