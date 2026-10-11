// SPDX-License-Identifier: AGPL-3.0-only
#include "RecordingPresentation.h"
#include <iostream>
#include <stdexcept>

namespace
{
void require(bool value, const char* message) { if (! value) throw std::runtime_error(message); }
void recordsStructuredSuccessAndFormatsInPresentation()
{
    const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("qfr-language-test", "", false);
    require(folder.createDirectory().wasOk(), "create isolated recording folder");
    struct Cleanup { juce::File folder; ~Cleanup() { folder.deleteRecursively(); } } cleanup { folder };
    qfr::RecordingSession session;
    require(session.configure({ folder, "original_prefix", 48000, 0, -1 }).wasOk(), "configure recording");
    require(session.snapshot().resultCode == qfr::RecordingSession::ResultCode::ready, "ready result code");
    require(session.start().wasOk(), "start recording");
    auto snapshot = session.snapshot();
    const qfr::i18n::Translation chinese({ "zh" });
    require(snapshot.resultCode == qfr::RecordingSession::ResultCode::recording
        && qfr::recordingStatus(snapshot, chinese) == juce::String::fromUTF8("正在录音：original_prefix_01.partial.flac"), "active result code and original filename");
    session.requestStop(); session.waitForStop();
    snapshot = session.snapshot();
    require(snapshot.resultCode == qfr::RecordingSession::ResultCode::saved && snapshot.message.isEmpty()
        && qfr::recordingStatus(snapshot, chinese) == juce::String::fromUTF8("已保存：original_prefix_01.flac"), "structured saved outcome has no session-formatted success message");
    require(qfr::recordingAction(qfr::RecordingSession::State::recording) == qfr::RecordingAction::stop
        && qfr::recordingAction(qfr::RecordingSession::State::finalizing) == qfr::RecordingAction::finalizing
        && qfr::recordingAction(qfr::RecordingSession::State::succeeded) == qfr::RecordingAction::record, "action state is independent of captions");
}
}
int main()
{
    try { recordsStructuredSuccessAndFormatsInPresentation(); std::cout << "PASS structured recording success and caption-independent action\n"; return 0; }
    catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
