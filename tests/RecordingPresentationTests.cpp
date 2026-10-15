// SPDX-License-Identifier: AGPL-3.0-only
#include "RecordingPresentation.h"
#include <iostream>
#include <stdexcept>

namespace
{
void require(bool value, const char* message) { if (! value) throw std::runtime_error(message); }
bool containsExact(const juce::String& text, const juce::String& value)
{
    return text.toStdString().find(value.toStdString()) != std::string::npos;
}
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
    require(snapshot.resultCode == qfr::RecordingSession::ResultCode::saved && snapshot.error.wasOk()
        && qfr::recordingStatus(snapshot, chinese) == juce::String::fromUTF8("已保存：original_prefix_01.flac"), "structured saved outcome has no session-formatted success message");
    require(qfr::recordingAction(qfr::RecordingSession::State::recording) == qfr::RecordingAction::stop
        && qfr::recordingAction(qfr::RecordingSession::State::finalizing) == qfr::RecordingAction::finalizing
        && qfr::recordingAction(qfr::RecordingSession::State::succeeded) == qfr::RecordingAction::record, "action state is independent of captions");
}
void translatesValidationAndPreservesUserData()
{
    const qfr::i18n::Translation chinese({ "zh-Hans" });
    const auto detail = juce::String::fromUTF8("驱动 {name} # <error>");
    require(qfr::resultMessage({ qfr::ErrorCode::folderMissing }, chinese)
        == juce::String::fromUTF8("目标文件夹不存在。请选择现有且可写的文件夹。"), "translated folder correction guidance");
    require(qfr::resultMessage({ qfr::ErrorCode::deviceOpenFailed, detail, "ASIO", "{input}" }, chinese).contains(detail),
        "native diagnostics are substituted verbatim, never parsed as template syntax");
}
void formatsCompoundFailuresCountsAndEnglishFallback()
{
    const qfr::i18n::Translation english({ "en" }), chinese({ "zh" });
    auto catalogs = qfr::i18n::productionCatalogs();
    catalogs[1].json = R"({"recording_failure":"{unknown}"})";
    const qfr::i18n::Translation fallback({ "zh" }, catalogs);
    qfr::RecordingSession::Snapshot snapshot;
    snapshot.state = qfr::RecordingSession::State::failed;
    snapshot.resultCode = qfr::RecordingSession::ResultCode::failed;
    snapshot.failureReason = qfr::RecordingSession::FailureReason::inputLost;
    snapshot.finalization = qfr::RecordingSession::FinalizationOutcome::diskFlushFailed;
    snapshot.incomplete = true;
    snapshot.recordingFile = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(juce::String::fromUTF8("录音{path}#_01.partial.flac"));
    snapshot.error.detail = juce::String::fromUTF8("原始{diagnostic}");
    snapshot.inputDetail = juce::String::fromUTF8("输入{driver}");
    const auto en = qfr::recordingStatus(snapshot, english), zh = qfr::recordingStatus(snapshot, chinese);
    require(en.contains("Input device was lost") && en.contains("Disk flush also failed") && !en.contains("Saved")
        && en.contains(snapshot.recordingFile.getFullPathName()) && en.contains(snapshot.error.detail) && en.contains(snapshot.inputDetail), "English reports both causes and retained path");
    require(zh.contains(juce::String::fromUTF8("输入设备已断开")) && zh.contains(juce::String::fromUTF8("磁盘写入也失败"))
        && zh.contains(snapshot.recordingFile.getFullPathName()) && zh.contains(snapshot.error.detail), "Chinese reports both causes and retained path");
    require(qfr::recordingStatus(snapshot, fallback) == en, "invalid translated failure falls back as one complete English message");
    require(qfr::incompleteRecordingWarning(0, "{folder}", english) == "No incomplete recordings in {folder}.", "zero incomplete recordings");
    require(qfr::incompleteRecordingWarning(1, "{folder}", english) == "1 incomplete recording. Inspect .partial.flac files in {folder}.", "English singular count");
    require(qfr::incompleteRecordingWarning(2, "{folder}", english) == "2 incomplete recordings. Inspect .partial.flac files in {folder}.", "English plural count");
    require(qfr::incompleteRecordingWarning(0, "{folder}", chinese) == juce::String::fromUTF8("{folder} 中没有未完成的录音。"), "Chinese zero count");
    require(qfr::incompleteRecordingWarning(1, "{folder}", chinese) == juce::String::fromUTF8("有 1 个未完成的录音。请检查 {folder} 中的 .partial.flac 文件。"), "Chinese single count");
    require(qfr::incompleteRecordingWarning(2, "{folder}", chinese) == juce::String::fromUTF8("有 2 个未完成的录音。请检查 {folder} 中的 .partial.flac 文件。"), "Chinese multiple count");
    snapshot.finalization = qfr::RecordingSession::FinalizationOutcome::finalized;
    snapshot.incomplete = false;
    require(qfr::recordingStatus(snapshot, english).contains("Captured audio was finalized")
        && !qfr::recordingStatus(snapshot, english).contains("Saved"), "interrupted finalized audio never claims successful capture");
}
void nativeDiskMessagesPreserveOperationDiagnosticsAndActualRecordingPath()
{
    const qfr::i18n::Translation english({ "en" }), chinese({ "zh-Hans" });
    const auto raw = juce::String::fromUTF8("原始 {disk} # error\r\n");
    const auto retained = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("actual_01.partial.flac");
    for (const auto* translation : { &english, &chinese })
    {
        qfr::RecorderResult create { qfr::ErrorCode::createFailed, raw, {}, {}, { qfr::DiskOperation::create, 112, raw } };
        const auto beforeCreation = qfr::resultMessage(create, *translation);
        require(containsExact(beforeCreation, raw) && beforeCreation.contains("112") && !beforeCreation.contains("partial.flac"),
            "native create error retains raw data and has no fictional retained recording");
        create.code = qfr::ErrorCode::writerStartFailed;
        const auto startup = qfr::resultMessage(create, *translation, retained);
        require(containsExact(startup, raw) && containsExact(startup, retained.getFullPathName()), "native startup failure shows actual retained file");
        qfr::RecordingSession::Snapshot snapshot;
        snapshot.state = qfr::RecordingSession::State::failed;
        snapshot.resultCode = qfr::RecordingSession::ResultCode::failed;
        snapshot.failureReason = qfr::RecordingSession::FailureReason::inputLost;
        snapshot.finalization = qfr::RecordingSession::FinalizationOutcome::diskFlushFailed;
        snapshot.incomplete = true; snapshot.recordingFile = retained;
        snapshot.inputDetail = "original input {diagnostic}";
        snapshot.error = { qfr::ErrorCode::none, raw, {}, {}, { qfr::DiskOperation::flush, 112, raw } };
        const auto compound = qfr::recordingStatus(snapshot, *translation);
        require(containsExact(compound, snapshot.inputDetail) && containsExact(compound, raw) && compound.contains("112")
            && containsExact(compound, retained.getFullPathName()) && !compound.contains("Saved") && !containsExact(compound, juce::String::fromUTF8("已保存")),
            "compound native failure preserves both diagnostics and path without a successful-save claim");
        require(containsExact(compound, translation == &english ? "flush recording to disk" : juce::String::fromUTF8("将录音刷新到磁盘")),
            "disk operation is translated independently of original native diagnostic");
    }
}
void recoveryAndLoggedOutcomesKeepTheirLocalizedMeaning()
{
    const qfr::i18n::Translation english({ "en" }), chinese({ "zh-Hans" });
    qfr::RecordingSession::Snapshot snapshot;
    snapshot.state = qfr::RecordingSession::State::recovering;
    snapshot.resultCode = qfr::RecordingSession::ResultCode::recovering;
    snapshot.recordingFile = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("take_01.flac");
    snapshot.logFile = snapshot.recordingFile.withFileExtension(".log");
    require(snapshot.controlsLocked() && qfr::recordingAction(snapshot.state) == qfr::RecordingAction::stop
        && qfr::recordingStatus(snapshot, english).contains("Recovering")
        && containsExact(qfr::recordingStatus(snapshot, chinese), juce::String::fromUTF8("正在恢复录音")),
        "recovery keeps Stop and translates status in both interface languages");
    snapshot.state = qfr::RecordingSession::State::succeeded;
    snapshot.resultCode = qfr::RecordingSession::ResultCode::saved;
    snapshot.hasRecordingErrors = true;
    require(qfr::recordingStatus(snapshot, english).contains("Saved with recording errors")
        && containsExact(qfr::recordingStatus(snapshot, chinese), juce::String::fromUTF8("录音期间发生错误")),
        "finalized audio with gaps uses a distinct localized saved-with-errors outcome");
    for (const auto* translation : { &english, &chinese })
        require(containsExact(qfr::recordingStatus(snapshot, *translation), snapshot.logFile.getFullPathName()),
            "saved-with-errors status identifies the stable accompanying log path");
    snapshot.state = qfr::RecordingSession::State::failed;
    snapshot.resultCode = qfr::RecordingSession::ResultCode::failed;
    snapshot.failureReason = qfr::RecordingSession::FailureReason::log;
    for (const auto* translation : { &english, &chinese })
        require(containsExact(qfr::recordingStatus(snapshot, *translation), snapshot.logFile.getFullPathName())
            && containsExact(qfr::recordingStatus(snapshot, *translation), snapshot.recordingFile.getFullPathName()),
            "failed required log reports both the attempted log and retained audio paths");
}
}
int main()
{
    try { recordsStructuredSuccessAndFormatsInPresentation(); translatesValidationAndPreservesUserData(); formatsCompoundFailuresCountsAndEnglishFallback(); nativeDiskMessagesPreserveOperationDiagnosticsAndActualRecordingPath(); recoveryAndLoggedOutcomesKeepTheirLocalizedMeaning(); std::cout << "PASS recording presentation\n"; return 0; }
    catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
