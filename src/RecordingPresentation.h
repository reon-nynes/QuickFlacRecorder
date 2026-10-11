// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "RecordingSession.h"
#include "localization/Translation.h"

namespace qfr
{
enum class RecordingAction { record, stop, finalizing };
RecordingAction recordingAction(RecordingSession::State) noexcept;
juce::String recordingStatus(const RecordingSession::Snapshot&, const i18n::Translation&);
}
