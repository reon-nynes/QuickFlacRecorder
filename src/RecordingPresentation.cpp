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
    MessageId id = MessageId::StartFailed;
    switch (result.code)
    {
        case ErrorCode::none: return {};
        case ErrorCode::prefixLength: id = MessageId::PrefixLength; break;
        case ErrorCode::prefixCharacters: id = MessageId::PrefixCharacters; break;
        case ErrorCode::prefixReserved: id = MessageId::PrefixReserved; break;
        case ErrorCode::folderMissing: id = MessageId::FolderMissing; break;
        case ErrorCode::folderNotWritable: id = MessageId::FolderNotWritable; break;
        case ErrorCode::folderCreateDenied: id = MessageId::FolderCreateDenied; break;
        case ErrorCode::folderCleanupFailed: id = MessageId::FolderCleanupFailed; break;
        case ErrorCode::invalidRate: id = MessageId::InvalidRate; break;
        case ErrorCode::invalidChannels: id = MessageId::InvalidChannels; break;
        case ErrorCode::monoChannelMissing: id = MessageId::MonoChannelMissing; break;
        case ErrorCode::stereoChannelsMissing: id = MessageId::StereoChannelsMissing; break;
        case ErrorCode::settingsLocked: id = MessageId::SettingsLocked; break;
        case ErrorCode::filenameLocked: id = MessageId::FilenameLocked; break;
        case ErrorCode::recordingBusy: id = MessageId::RecordingBusy; break;
        case ErrorCode::invalidCapacity: id = MessageId::InvalidCapacity; break;
        case ErrorCode::invalidFlushInterval: id = MessageId::InvalidFlushInterval; break;
        case ErrorCode::counterTooLarge: id = MessageId::CounterTooLarge; break;
        case ErrorCode::counterExhausted: id = MessageId::CounterExhausted; break;
        case ErrorCode::createFailed: id = MessageId::CreateFailed; break;
        case ErrorCode::reservationFailed: id = MessageId::ReservationFailed; break;
        case ErrorCode::permissionDenied: id = MessageId::PermissionDenied; break;
        case ErrorCode::outputOpenFailed: id = MessageId::OutputOpenFailed; break;
        case ErrorCode::writerStartFailed: id = MessageId::WriterStartFailed; break;
        case ErrorCode::startFailed: id = MessageId::StartFailed; break;
        case ErrorCode::driverUnavailable: id = MessageId::DriverUnavailable; break;
        case ErrorCode::deviceUnavailable: id = MessageId::DeviceUnavailable; break;
        case ErrorCode::deviceCreateFailed: id = MessageId::DeviceCreateFailed; break;
        case ErrorCode::noInputChannels: id = MessageId::NoInputChannels; break;
        case ErrorCode::deviceStartFailed: id = MessageId::DeviceStartFailed; break;
        case ErrorCode::deviceLost: id = MessageId::DeviceLost; break;
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
    const juce::String diagnostic = result.detail.isNotEmpty() ? "yes" : "no";
    if (result.code == ErrorCode::outputOpenFailed || result.code == ErrorCode::writerStartFailed || result.code == ErrorCode::startFailed)
        return translation.text(id, { { "retained", juce::String(retained == juce::File() ? "no" : "yes") },
            { "path", retained.getFullPathName() }, { "diagnostic", diagnostic }, { "detail", result.detail } });
    if (result.code == ErrorCode::driverUnavailable || result.code == ErrorCode::deviceUnavailable
        || result.code == ErrorCode::deviceCreateFailed || result.code == ErrorCode::noInputChannels
        || result.code == ErrorCode::deviceStartFailed || result.code == ErrorCode::deviceLost)
        return translation.text(id, { { "driver", result.driver }, { "device", result.device },
            { "diagnostic", diagnostic }, { "detail", result.detail } });
    return translation.text(id);
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
