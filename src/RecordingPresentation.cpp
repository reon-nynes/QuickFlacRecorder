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
juce::String recordingStatus(const RecordingSession::Snapshot& snapshot, const i18n::Translation& translation)
{
    using i18n::MessageId;
    switch (snapshot.resultCode)
    {
        case RecordingSession::ResultCode::ready: return translation.text(MessageId::Ready);
        case RecordingSession::ResultCode::recording: return translation.text(MessageId::Recording, { { "filename", snapshot.recordingFile.getFileName() } });
        case RecordingSession::ResultCode::finalizing: return translation.text(MessageId::Finalizing, { { "filename", snapshot.recordingFile.getFileName() } });
        case RecordingSession::ResultCode::saved: return translation.text(MessageId::Saved, { { "filename", snapshot.recordingFile.getFileName() } });
        case RecordingSession::ResultCode::failed:
        case RecordingSession::ResultCode::none: return snapshot.message;
    }
    return {};
}
}
