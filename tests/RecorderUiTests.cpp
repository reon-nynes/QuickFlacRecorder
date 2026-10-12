// SPDX-License-Identifier: AGPL-3.0-only
#include "RecorderComponent.h"
#include "localization/SystemLanguages.h"
#include "FolderWritePermission.h"
#include <iostream>
#include <stdexcept>

namespace
{
void require(bool value, const char* message) { if (! value) throw std::runtime_error(message); }
template<class T> T& childWithTitle(juce::Component& parent, const juce::String& title)
{
    for (auto* child : parent.getChildren())
        if (auto* typed = dynamic_cast<T*>(child); typed && typed->getTitle() == title) return *typed;
    throw std::runtime_error("Missing control: " + title.toStdString());
}
qfr::theme::RecordingButton& actionButton(juce::Component& parent)
{
    for (auto* child : parent.getChildren()) if (auto* button = dynamic_cast<qfr::theme::RecordingButton*>(child)) return *button;
    throw std::runtime_error("Missing recording action");
}
void writeImage(juce::Component& component, const juce::File& folder, const juce::String& name, float scale = 1.0f)
{
    if (folder == juce::File()) return;
    juce::PNGImageFormat png;
    auto stream = folder.getChildFile(name + ".png").createOutputStream();
    require(stream != nullptr && png.writeImageToStream(component.createComponentSnapshot(component.getLocalBounds(), true, scale), *stream), "write Windows layout evidence");
}
void controlsPreserveNativeIdentities(const juce::File& root, const juce::File& evidence, const qfr::i18n::Translation& translation)
{
    using qfr::i18n::MessageId;
    const auto tag = translation.language().tag;
    juce::PropertiesFile::Options options;
    options.applicationName = "qfr-ui-test"; options.filenameSuffix = ".settings";
    juce::PropertiesFile settings(root.getChildFile(tag + ".settings"), options);
    settings.setValue("targetFolder", root.getFullPathName());
    const auto nativeDriver = juce::String::fromUTF8("Missing driver (unavailable) （不可用）");
    const auto nativeDevice = juce::String::fromUTF8("Missing input （不可用）");
    settings.setValue("audioDriver", nativeDriver); settings.setValue("inputDevice", nativeDevice);
    {
        qfr::RecorderComponent recorder(settings, translation);
        auto& driver = childWithTitle<juce::ComboBox>(recorder, translation.text(MessageId::AudioDriver));
        auto& device = childWithTitle<juce::ComboBox>(recorder, translation.text(MessageId::InputDevice));
        require(driver.getText() == translation.text(MessageId::Unavailable, { { "name", nativeDriver } })
            && device.getText() == translation.text(MessageId::Unavailable, { { "name", nativeDevice } }), "native identities receive translated presentation annotations");
        auto& action = actionButton(recorder);
        auto& status = childWithTitle<juce::Label>(recorder, translation.text(MessageId::StatusTitle));
        require(status.getText() == qfr::resultMessage({ qfr::ErrorCode::driverUnavailable, {}, nativeDriver, nativeDevice }, translation),
            "missing remembered driver has translated guidance with unchanged native identities");
        require(status.getTooltip() == status.getText() && status.getDescription() == status.getText(), "full error remains available in tooltip and accessible description");
        require(action.getAction() == qfr::RecordingAction::record && action.getButtonText() == translation.text(MessageId::Record)
            && ! action.isEnabled(), "missing input disables the caption-independent Record action");
        for (float scale : { 1.0f, 1.5f, 2.0f })
            writeImage(recorder, evidence, tag + "-unavailable-" + juce::String(scale, 1), scale);
        auto& mode = childWithTitle<juce::ComboBox>(recorder, translation.text(MessageId::Channels));
        mode.setSelectedId(2, juce::sendNotificationSync);
        require(mode.getSelectedId() == 2 && childWithTitle<juce::ComboBox>(recorder, translation.text(MessageId::RightInput)).isVisible(),
            "translated Stereo selection reveals the right channel");
    }
    require(settings.getValue("audioDriver") == nativeDriver && settings.getValue("inputDevice") == nativeDevice,
        "saving preferences preserves exact native identities including annotation-like text");
}
void inputResultsDoNotDependOnLanguage()
{
    qfr::RecordingSession session;
    qfr::InputController input(session);
    const auto nativeDriver = juce::String::fromUTF8("不存在{driver}"), nativeDevice = juce::String::fromUTF8("设备{input}");
    const auto result = input.open(nativeDriver, nativeDevice);
    require(result.code == qfr::ErrorCode::driverUnavailable && result.driver == nativeDriver && result.device == nativeDevice
        && input.driver() == nativeDriver && input.inputDevice() == nativeDevice && !input.isAvailable(),
        "unavailable driver retains original identities in structured result");
    const auto firstDetail = juce::String::fromUTF8("第一个驱动错误 {detail}");
    input.audioDeviceError(firstDetail);
    input.audioDeviceError("later notification before UI poll");
    require(input.pollError().detail == firstDetail, "native input boundary retains first diagnostic before polling");
    if (input.defaultDriver().isNotEmpty())
    {
        const auto missing = input.open(input.defaultDriver(), nativeDevice);
        require(missing.code == qfr::ErrorCode::deviceUnavailable && missing.device == nativeDevice,
            "missing remembered input retains identity instead of selecting a fallback");
    }
}
void nativeFailureAndRecoveryEvidence(const juce::File& root, const juce::File& evidence, const qfr::i18n::Translation& translation)
{
    using qfr::i18n::MessageId;
    const auto tag = translation.language().tag;
    const auto recordingFolder = root.getChildFile(tag + "-recovery-audio");
    require(recordingFolder.createDirectory().wasOk(), "isolate recovery recording fixtures by language");
    juce::PropertiesFile::Options options;
    options.applicationName = "qfr-recovery-test"; options.filenameSuffix = ".settings";
    juce::PropertiesFile settings(root.getChildFile(tag + "-recovery.settings"), options);
    settings.setValue("targetFolder", recordingFolder.getFullPathName());
    qfr::RecorderComponent recorder(settings, translation);
    recorder.addToDesktop(juce::ComponentPeer::windowIsTemporary); recorder.setVisible(true);
    auto& action = actionButton(recorder);
    auto& status = childWithTitle<juce::Label>(recorder, translation.text(MessageId::StatusTitle));
    auto& folder = childWithTitle<juce::TextEditor>(recorder, translation.text(MessageId::TargetFolder));
    auto& prefix = childWithTitle<juce::TextEditor>(recorder, translation.text(MessageId::Prefix));
    const auto advance = [] { juce::MessageManager::getInstance()->runDispatchLoopUntil(100); };
    const auto capture = [&](const juce::String& state)
    {
        for (float scale : { 1.0f, 1.5f, 2.0f }) writeImage(recorder, evidence, tag + "-" + state + "-" + juce::String(scale, 1), scale);
        require(status.getTooltip() == status.getText() && status.getDescription() == status.getText(), "full recovery status remains accessible");
        juce::TooltipWindow tooltip;
        tooltip.setLookAndFeel(&recorder.getLookAndFeel());
        tooltip.displayTip(recorder.getScreenPosition(), status.getTooltip());
        writeImage(tooltip, evidence, tag + "-" + state + "-tooltip-1.5", 1.5f);
        tooltip.hideTip(); tooltip.setLookAndFeel(nullptr);
    };
    advance(); require(action.isEnabled(), "native input ready for recovery tests");
    require(recordingFolder.deleteFile(), "remove empty currently selected folder");
    juce::MessageManager::getInstance()->runDispatchLoopUntil(1200);
    const bool blockedMissing = !action.isEnabled() && status.getText() == qfr::resultMessage({ qfr::ErrorCode::folderMissing }, translation);
    require(recordingFolder.createDirectory().wasOk(), "externally restore selected folder");
    juce::MessageManager::getInstance()->runDispatchLoopUntil(1200);
    require(blockedMissing && action.isEnabled() && folder.getText() == recordingFolder.getFullPathName(),
        "passive folder validation disables Record and recovers when the original folder returns");
    const auto missing = recordingFolder.getChildFile(juce::String::fromUTF8("不存在{folder}"));
    folder.setText(missing.getFullPathName()); advance();
    require(!action.isEnabled() && folder.getText() == missing.getFullPathName() && !missing.exists()
        && status.getText() == qfr::resultMessage({ qfr::ErrorCode::folderMissing }, translation), "missing target folder preserves value and blocks recording");
    capture("folder-missing");
    folder.setText(recordingFolder.getFullPathName()); advance(); require(action.isEnabled(), "explicit target-folder correction restores recording");
    prefix.setText("CON"); advance();
    require(!action.isEnabled() && prefix.getText() == "CON" && status.getText() == qfr::resultMessage({ qfr::ErrorCode::prefixReserved }, translation), "invalid prefix preserves value and blocks recording");
    capture("prefix-invalid");
    prefix.setText(juce::String::fromUTF8("录音{filename}")); advance(); require(action.isEnabled(), "prefix correction recovers unchanged selected language");
    const auto partial = recordingFolder.getChildFile("test_01.partial.flac");
    require(partial.replaceWithText("incomplete"), "seed incomplete warning");
    folder.setText(recordingFolder.getFullPathName() + juce::File::getSeparatorString()); advance();
    require(status.getText() == qfr::incompleteRecordingWarning(1, folder.getText(), translation), "one incomplete recording uses localized grammar and actual folder");
    capture("incomplete-one");
    require(partial.deleteFile(), "remove isolated warning fixture");
    recorder.removeFromDesktop();
    // A real Windows input and the production writer use a controlled disk boundary.
    qfr::RecordingSession::Options diskOptions;
    diskOptions.diskFlushInterval = std::chrono::milliseconds(20);
    class RefusedFlush final : public juce::OutputStream
    {
    public:
        explicit RefusedFlush(std::unique_ptr<juce::OutputStream> stream) : output(std::move(stream)) {}
        void flush() override { throw std::runtime_error("native test {flush} refused"); }
        bool setPosition(juce::int64 position) override { return output->setPosition(position); }
        juce::int64 getPosition() override { return output->getPosition(); }
        bool write(const void* data, size_t size) override { return output->write(data, size); }
    private:
        std::unique_ptr<juce::OutputStream> output;
    };
    diskOptions.outputAdapter = [](std::unique_ptr<juce::OutputStream> stream) { return std::make_unique<RefusedFlush>(std::move(stream)); };
    qfr::RecorderComponent failed(settings, translation, std::move(diskOptions));
    failed.addToDesktop(juce::ComponentPeer::windowIsTemporary); failed.setVisible(true); advance();
    auto& failedAction = actionButton(failed);
    require(failedAction.isEnabled(), "real input ready for disk failure");
    failedAction.triggerClick(); advance(); advance();
    auto& failedStatus = childWithTitle<juce::Label>(failed, translation.text(MessageId::StatusTitle));
    require(failedAction.getAction() == qfr::RecordingAction::record && failedStatus.getText().contains(".partial.flac")
        && failedStatus.getText().contains("native test {flush} refused"), "native writer failure exposes retained recording and raw diagnostic");
    for (float scale : { 1.0f, 1.5f, 2.0f }) writeImage(failed, evidence, tag + "-flush-failure-" + juce::String(scale, 1), scale);
    juce::TooltipWindow tooltip;
    tooltip.setLookAndFeel(&failed.getLookAndFeel());
    tooltip.displayTip(failed.getScreenPosition(), failedStatus.getTooltip());
    writeImage(tooltip, evidence, tag + "-flush-failure-tooltip-1.5", 1.5f);
    tooltip.hideTip(); tooltip.setLookAndFeel(nullptr);
    failed.removeFromDesktop();
}
void nativeMissingInputRecovery(const juce::File& root, const juce::File& evidence, const qfr::i18n::Translation& translation)
{
    using qfr::i18n::MessageId;
    juce::PropertiesFile::Options options;
    options.applicationName = "qfr-input-recovery"; options.filenameSuffix = ".settings";
    juce::PropertiesFile settings(root.getChildFile(translation.language().tag + "-input-recovery.settings"), options);
    settings.setValue("targetFolder", root.getFullPathName());
    const auto missing = juce::String::fromUTF8("已断开的设备 {input}");
    settings.setValue("inputDevice", missing);
    qfr::RecorderComponent recorder(settings, translation);
    recorder.addToDesktop(juce::ComponentPeer::windowIsTemporary); recorder.setVisible(true);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
    auto& input = childWithTitle<juce::ComboBox>(recorder, translation.text(MessageId::InputDevice));
    auto& action = actionButton(recorder);
    auto& status = childWithTitle<juce::Label>(recorder, translation.text(MessageId::StatusTitle));
    require(!action.isEnabled() && status.getText().contains(missing) && settings.getValue("inputDevice") == missing,
        "missing remembered input remains selected with translated guidance");
    for (float scale : { 1.0f, 1.5f, 2.0f }) writeImage(recorder, evidence, translation.language().tag + "-input-missing-" + juce::String(scale, 1), scale);
    require(input.getNumItems() > 0, "an available replacement input exists for native recovery");
    input.setSelectedId(input.getItemId(0), juce::sendNotificationSync);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
    require(action.isEnabled() && settings.getValue("inputDevice") != missing, "explicitly selecting available input recovers recording and saves its native identity");
    action.triggerClick(); juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
    require(action.getAction() == qfr::RecordingAction::stop, "replacement input records again");
    action.triggerClick(); juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
    require(action.getAction() == qfr::RecordingAction::record, "replacement recording finishes normally");
    recorder.removeFromDesktop();
}
void iconsDoNotUseCaptions(const juce::File& evidence)
{
    qfr::theme::LookAndFeel look;
    qfr::theme::RecordingButton button;
    button.setLookAndFeel(&look); button.setSize(140, 64); button.setButtonText(juce::String::fromUTF8("任意文字"));
    const auto width = juce::GlyphArrangement::getStringWidthInt(qfr::theme::body(qfr::theme::actionSize), button.getButtonText());
    const auto x = (button.getWidth() - width - 24) / 2;
    button.setAction(qfr::RecordingAction::record);
    const auto record = button.createComponentSnapshot(button.getLocalBounds());
    require(record.getPixelAt(x + 7, 32) == qfr::theme::record, "arbitrary caption still draws Record dot");
    button.setAction(qfr::RecordingAction::stop);
    const auto stop = button.createComponentSnapshot(button.getLocalBounds());
    require(stop.getPixelAt(x + 7, 32) == qfr::theme::text, "arbitrary caption still draws Stop square");
    writeImage(button, evidence, "caption-independent-stop");
    button.setAction(qfr::RecordingAction::finalizing); button.setEnabled(false);
    writeImage(button, evidence, "caption-independent-finalizing");
    button.setLookAndFeel(nullptr);
}
void terminalReadinessRecoversWithoutEditing(qfr::RecorderComponent& recorder, const juce::File& recordingFolder,
    const qfr::i18n::Translation& translation, const juce::File& evidence = {})
{
    using qfr::i18n::MessageId;
    auto& action = actionButton(recorder);
    auto& status = childWithTitle<juce::Label>(recorder, translation.text(MessageId::StatusTitle));
    auto& elapsed = childWithTitle<juce::Label>(recorder, translation.text(MessageId::ElapsedTitle));
    auto& folder = childWithTitle<juce::TextEditor>(recorder, translation.text(MessageId::TargetFolder));
    auto& prefix = childWithTitle<juce::TextEditor>(recorder, translation.text(MessageId::Prefix));
    auto& driver = childWithTitle<juce::ComboBox>(recorder, translation.text(MessageId::AudioDriver));
    auto& device = childWithTitle<juce::ComboBox>(recorder, translation.text(MessageId::InputDevice));
    const auto selectedDriver = driver.getText(), selectedDevice = device.getText();
    const auto outcome = status.getText(), time = elapsed.getText(), enteredFolder = folder.getText(), enteredPrefix = prefix.getText();
    const auto poll = [] { juce::MessageManager::getInstance()->runDispatchLoopUntil(1200); };
    const auto externalPartial = recordingFolder.getChildFile("external.partial.flac");
    require(externalPartial.replaceWithText("incomplete"), "add incomplete recording after terminal take");
    poll();
    require(status.getText() == outcome && elapsed.getText() == time && action.isEnabled()
        && status.getTooltip() == outcome && status.getDescription() == outcome,
        "passive count addition preserves terminal path, incomplete status, diagnostics, and elapsed time");
    require(externalPartial.deleteFile(), "remove external incomplete recording after terminal take");
    poll();
    require(status.getText() == outcome && elapsed.getText() == time && action.isEnabled(),
        "passive count removal preserves terminal outcome");
    const auto moved = recordingFolder.getSiblingFile(recordingFolder.getFileName() + "-moved");
    require(recordingFolder.moveFileTo(moved), "externally rename folder after terminal take");
    poll();
    const bool missingBlocked = !action.isEnabled() && status.getText().toStdString().find(outcome.toStdString()) != std::string::npos
        && status.getText().toStdString().find(qfr::resultMessage({ qfr::ErrorCode::folderMissing }, translation).toStdString()) != std::string::npos;
    writeImage(recorder, evidence, recordingFolder.getFileName() + "-missing-1.5", 1.5f);
    require(moved.moveFileTo(recordingFolder), "restore same folder after terminal take");
    poll();
    require(missingBlocked && action.isEnabled() && status.getText() == outcome && elapsed.getText() == time,
        "folder removal/restoration updates Record while preserving terminal message and elapsed time");
    {
        FolderWritePermission permission(recordingFolder);
        poll();
        const bool blocked = !action.isEnabled() && status.getText().toStdString().find(outcome.toStdString()) != std::string::npos
            && status.getTooltip() == status.getText() && status.getDescription() == status.getText();
        writeImage(recorder, evidence, recordingFolder.getFileName() + "-unwritable-1.5", 1.5f);
        juce::TooltipWindow tooltip;
        tooltip.setLookAndFeel(&recorder.getLookAndFeel());
        tooltip.displayTip(recorder.getScreenPosition(), status.getTooltip());
        writeImage(tooltip, evidence, recordingFolder.getFileName() + "-unwritable-tooltip-1.5", 1.5f);
        tooltip.hideTip(); tooltip.setLookAndFeel(nullptr);
        permission.restore();
        poll();
        require(blocked && action.isEnabled() && status.getText() == outcome, "permission change/restoration updates Record without replacing terminal result");
    }
    require(folder.getText() == enteredFolder && prefix.getText() == enteredPrefix && elapsed.getText() == time
        && driver.getText() == selectedDriver && device.getText() == selectedDevice,
        "passive readiness retains original folder, prefix, selected input, and elapsed time");
}
void nativeIncompleteWarnings(const juce::File& root, const qfr::i18n::Translation& translation)
{
    using qfr::i18n::MessageId;
    const auto recordingFolder = root.getChildFile(translation.language().tag + "-incomplete-{folder}");
    require(recordingFolder.createDirectory().wasOk(), "create isolated incomplete-recording folder");
    juce::PropertiesFile::Options options;
    options.applicationName = "qfr-incomplete-test"; options.filenameSuffix = ".settings";
    juce::PropertiesFile settings(root.getChildFile(translation.language().tag + "-incomplete.settings"), options);
    settings.setValue("targetFolder", recordingFolder.getFullPathName() + juce::File::getSeparatorString());
    qfr::RecorderComponent recorder(settings, translation);
    auto& action = actionButton(recorder);
    auto& status = childWithTitle<juce::Label>(recorder, translation.text(MessageId::StatusTitle));
    auto& folder = childWithTitle<juce::TextEditor>(recorder, translation.text(MessageId::TargetFolder));
    auto& prefix = childWithTitle<juce::TextEditor>(recorder, translation.text(MessageId::Prefix));
    auto& driver = childWithTitle<juce::ComboBox>(recorder, translation.text(MessageId::AudioDriver));
    auto& device = childWithTitle<juce::ComboBox>(recorder, translation.text(MessageId::InputDevice));
    const auto enteredFolder = folder.getText(), enteredPrefix = prefix.getText();
    const auto selectedDriver = driver.getText(), selectedDevice = device.getText();
    const auto poll = [] { juce::MessageManager::getInstance()->runDispatchLoopUntil(1200); };
    const auto expectCount = [&](int count)
    {
        poll();
        require(status.getText() == (count == 0 ? translation.text(MessageId::Ready)
            : qfr::incompleteRecordingWarning(count, enteredFolder, translation)), "passive warning matches current count and entered folder");
        require(action.isEnabled() && status.getTooltip() == status.getText() && status.getDescription() == status.getText(),
            "incomplete warning preserves Record readiness and full accessible message");
        require(folder.getText() == enteredFolder && prefix.getText() == enteredPrefix
            && driver.getText() == selectedDriver && device.getText() == selectedDevice,
            "passive count refresh preserves entered configuration and selected input");
    };
    expectCount(0);
    std::cout << "Passive incomplete warnings: " << translation.language().tag << ", "
        << selectedDriver << ", " << selectedDevice << '\n';
    const auto first = recordingFolder.getChildFile("other-prefix_01.partial.flac");
    const auto second = recordingFolder.getChildFile("external.partial.flac");
    require(first.replaceWithText("incomplete"), "externally add first incomplete recording");
    expectCount(1);
    require(second.replaceWithText("incomplete"), "externally add second incomplete recording");
    require(recordingFolder.getChildFile("complete.flac").replaceWithText("complete")
        && recordingFolder.getChildFile("ignore.partial.flac.bak").replaceWithText("backup")
        && recordingFolder.getChildFile("directory.partial.flac").createDirectory().wasOk()
        && recordingFolder.getChildFile("nested").createDirectory().wasOk()
        && recordingFolder.getChildFile("nested/hidden.partial.flac").replaceWithText("nested")
        && root.getChildFile("outside.partial.flac").replaceWithText("outside"), "seed excluded files and directories");
    expectCount(2);
    require(first.deleteFile(), "externally remove one of multiple incomplete recordings");
    expectCount(1);
    require(second.deleteFile(), "externally clear incomplete recordings");
    expectCount(0);
    require(first.replaceWithText("incomplete") && second.replaceWithText("incomplete"), "restore incomplete fixtures for folder recovery");
    expectCount(2);
    const auto moved = recordingFolder.getSiblingFile(recordingFolder.getFileName() + "-moved");
    require(recordingFolder.moveFileTo(moved), "externally remove selected folder with incomplete recordings");
    poll();
    require(!action.isEnabled() && !recordingFolder.exists() && folder.getText() == enteredFolder
        && status.getText() == qfr::resultMessage({ qfr::ErrorCode::folderMissing }, translation),
        "missing folder takes priority without recreation or redirection");
    require(moved.getChildFile(first.getFileName()).deleteFile(), "remove incomplete recording while folder is missing");
    require(moved.moveFileTo(recordingFolder), "restore selected folder externally");
    expectCount(1);
    require(second.deleteFile(), "externally remove last incomplete recording");
    expectCount(0);
    const auto otherFolder = root.getChildFile(translation.language().tag + "-other-incomplete");
    require(otherFolder.createDirectory().wasOk() && otherFolder.getChildFile("other.partial.flac").replaceWithText("incomplete"),
        "create another target folder with a different count");
    folder.setText(otherFolder.getFullPathName());
    poll();
    require(status.getText() == qfr::incompleteRecordingWarning(1, folder.getText(), translation), "warning counts currently entered target folder");
    folder.setText(enteredFolder);
    expectCount(0);
}
void nativeRecordingEvidence(const juce::File& root, const juce::File& evidence, const qfr::i18n::Translation& translation)
{
    using qfr::i18n::MessageId;
    const auto tag = translation.language().tag;
    juce::PropertiesFile::Options options;
    options.applicationName = "qfr-native-language-test"; options.filenameSuffix = ".settings";
    juce::PropertiesFile settings(root.getChildFile(tag + "-native.settings"), options);
    settings.setValue("targetFolder", root.getFullPathName());
    qfr::RecorderComponent recorder(settings, translation);
    recorder.addToDesktop(juce::ComponentPeer::windowIsTemporary);
    recorder.setVisible(true);
    auto& action = actionButton(recorder);
    auto& status = childWithTitle<juce::Label>(recorder, translation.text(MessageId::StatusTitle));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
    require(action.isEnabled(), "real default Windows input is ready");
    require(status.getText() == translation.text(MessageId::Ready), "ready status uses selected language");
    for (float scale : { 1.0f, 1.5f, 2.0f })
        writeImage(recorder, evidence, tag + "-ready-" + juce::String(scale, 1), scale);
    auto& prefix = childWithTitle<juce::TextEditor>(recorder, translation.text(MessageId::Prefix));
    const auto sendSpace = [&recorder]
    {
        recorder.getPeer()->handleKeyPress(juce::KeyPress(juce::KeyPress::spaceKey));
    };
    prefix.grabKeyboardFocus();
    sendSpace();
    require(action.getAction() == qfr::RecordingAction::record, "Space while editing does not start capture");
    auto& mode = childWithTitle<juce::ComboBox>(recorder, translation.text(MessageId::Channels));
    mode.grabKeyboardFocus();
    sendSpace();
    require(action.getAction() == qfr::RecordingAction::record, "Space with a selector focused does not start capture");
    mode.showPopup();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    require(juce::Component::getCurrentlyModalComponent() != nullptr, "native selector popup opens");
    writeImage(*juce::Component::getCurrentlyModalComponent(), evidence, tag + "-popup-1.5", 1.5f);
    juce::PopupMenu::dismissAllActiveMenus();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    {
        juce::TooltipWindow tooltip;
        tooltip.setLookAndFeel(&recorder.getLookAndFeel());
        tooltip.displayTip(recorder.getScreenPosition(), translation.text(MessageId::RecordTip));
        writeImage(tooltip, evidence, tag + "-tooltip-1.5", 1.5f);
        tooltip.hideTip(); tooltip.setLookAndFeel(nullptr);
    }
    recorder.grabKeyboardFocus();
    sendSpace();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(300);
    require(action.getAction() == qfr::RecordingAction::stop && status.getText().contains(".partial.flac"), "native recording shows Stop and original filename");
    require(! mode.isEnabled() && ! prefix.isEnabled() && action.isEnabled(), "capture locks setup and keeps Stop enabled");
    writeImage(recorder, evidence, tag + "-recording-1.5", 1.5f);
    bool quit = false;
    recorder.requestClose([&quit] { quit = true; });
    auto* modal = juce::Component::getCurrentlyModalComponent();
    auto* dialog = dynamic_cast<juce::AlertWindow*>(modal);
    require(dialog != nullptr && dialog->getName() == translation.text(MessageId::CloseTitle), "close confirmation is translated");
    writeImage(*dialog, evidence, tag + "-close-1.5", 1.5f);
    dialog->getPeer()->handleKeyPress(juce::KeyPress(juce::KeyPress::escapeKey));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    require(! quit && action.getAction() == qfr::RecordingAction::stop, "Continue recording keeps capture running");
    recorder.grabKeyboardFocus();
    sendSpace();
    require(action.getAction() == qfr::RecordingAction::finalizing && ! action.isEnabled(), "finalization disables the action");
    writeImage(recorder, evidence, tag + "-finalizing-1.5", 1.5f);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(300);
    require(action.getAction() == qfr::RecordingAction::record && status.getText().contains(".flac")
        && ! status.getText().contains(".partial.flac"), "native Stop finishes saving and restores Record");
    writeImage(recorder, evidence, tag + "-saved-1.5", 1.5f);
    terminalReadinessRecoversWithoutEditing(recorder, root, translation);
    mode.setSelectedId(2, juce::sendNotificationSync);
    writeImage(recorder, evidence, tag + "-stereo-1.5", 1.5f);
    recorder.grabKeyboardFocus(); sendSpace();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
    recorder.requestClose([&quit] { quit = true; });
    modal = juce::Component::getCurrentlyModalComponent();
    require(modal != nullptr, "second close confirmation opens during stereo capture");
    modal->getPeer()->handleKeyPress(juce::KeyPress(juce::KeyPress::returnKey));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    require(! quit && action.getAction() == qfr::RecordingAction::stop, "Return keeps recording");
    recorder.requestClose([&quit] { quit = true; });
    dialog = dynamic_cast<juce::AlertWindow*>(juce::Component::getCurrentlyModalComponent());
    require(dialog != nullptr, "Stop and quit confirmation opens");
    juce::TextButton* quitButton = nullptr;
    for (auto* child : dialog->getChildren())
        if (auto* button = dynamic_cast<juce::TextButton*>(child); button && button->getButtonText() == translation.text(MessageId::StopAndQuit)) quitButton = button;
    require(quitButton != nullptr, "translated Stop and quit action is available");
    quitButton->triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(300);
    require(quit && action.getAction() == qfr::RecordingAction::record, "Stop and quit waits for successful finalization");
    recorder.removeFromDesktop();
}
void nativeTerminalReadiness(const juce::File& root, const juce::File& evidence, const qfr::i18n::Translation& translation)
{
    using qfr::i18n::MessageId;
    for (const bool failDisk : { false, true })
    {
        const auto name = translation.language().tag + (failDisk ? "-failed-readiness" : "-saved-readiness");
        const auto folder = root.getChildFile(name);
        require(folder.createDirectory().wasOk(), "create readiness target folder");
        juce::PropertiesFile::Options propertyOptions;
        propertyOptions.applicationName = "qfr-readiness-test"; propertyOptions.filenameSuffix = ".settings";
        juce::PropertiesFile settings(root.getChildFile(name + ".settings"), propertyOptions);
        settings.setValue("targetFolder", folder.getFullPathName());
        qfr::RecordingSession::Options options;
        options.diskFlushInterval = std::chrono::milliseconds(100);
        if (failDisk) options.nativeError = [](qfr::DiskOperation operation, const juce::File& file) -> uint64_t
        {
            if (operation != qfr::DiskOperation::flush || !file.getFileName().endsWith(".partial.flac")) return 0;
           #if JUCE_WINDOWS
            return ERROR_DISK_FULL;
           #else
            return ENOSPC;
           #endif
        };
        qfr::RecorderComponent recorder(settings, translation, options);
        recorder.addToDesktop(juce::ComponentPeer::windowIsTemporary); recorder.setVisible(true);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
        auto& action = actionButton(recorder);
        require(action.isEnabled(), "real native input ready for terminal readiness regression");
        action.triggerClick();
        juce::MessageManager::getInstance()->runDispatchLoopUntil(1100);
        if (!failDisk) { action.triggerClick(); juce::MessageManager::getInstance()->runDispatchLoopUntil(300); }
        auto& status = childWithTitle<juce::Label>(recorder, translation.text(MessageId::StatusTitle));
        require(action.getAction() == qfr::RecordingAction::record && action.isEnabled()
            && status.getText().contains(failDisk ? ".partial.flac" : ".flac"), "terminal take has retained recording outcome");
        writeImage(recorder, evidence, name + "-1.5", 1.5f);
        terminalReadinessRecoversWithoutEditing(recorder, folder, translation, evidence);
        recorder.removeFromDesktop();
    }
}
}
int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    try
    {
        juce::StringArray args;
        for (int i = 1; i < argc; ++i) args.add(juce::String::fromUTF8(argv[i]));
        juce::File evidence;
        if (args.contains("--evidence")) evidence = juce::File(args[args.indexOf("--evidence") + 1]);
        if (evidence != juce::File()) require(evidence.createDirectory().wasOk(), "create evidence folder");
        const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("qfr-ui", "", false);
        require(root.createDirectory().wasOk(), "create isolated UI test folder");
        struct Cleanup { juce::File folder; ~Cleanup() { folder.deleteRecursively(); } } cleanup { root };
        controlsPreserveNativeIdentities(root, evidence, qfr::i18n::Translation({ "en" }));
        controlsPreserveNativeIdentities(root, evidence, qfr::i18n::Translation({ "zh-Hans" }));
        auto catalogs = qfr::i18n::productionCatalogs();
        catalogs.push_back({ { "en-XA", "Expanded test language", {}, "test-only", qfr::i18n::PluralRule::oneOther },
            R"({"record":"Capture", "stop":"End take", "ready":"Ready to capture the selected input. Check the live input levels and the original recording filename before you begin.","format":"{channels, plural, one {# recorded channel} other {# recorded channels}} at the original stream rate {rate}","unavailable":"Unavailable input: {name}"})" });
        controlsPreserveNativeIdentities(root, evidence, qfr::i18n::Translation({ "en-XA" }, catalogs));
        iconsDoNotUseCaptions(evidence);
        inputResultsDoNotDependOnLanguage();
        std::cout << "PASS bilingual controls, native identities, and caption-independent icons\n";
        if (args.contains("--native-incomplete"))
        {
            nativeIncompleteWarnings(root, qfr::i18n::Translation({ "en" }));
            nativeIncompleteWarnings(root, qfr::i18n::Translation({ "zh-Hans" }));
            std::cout << "PASS native bilingual passive incomplete-recording additions, removals, matching, and folder recovery\n";
        }
        if (args.contains("--native-readiness"))
        {
            nativeTerminalReadiness(root, evidence, qfr::i18n::Translation({ "en" }));
            nativeTerminalReadiness(root, evidence, qfr::i18n::Translation({ "zh-Hans" }));
            std::cout << "PASS native bilingual readiness after successful/failed takes and folder/permission recovery\n";
        }
        if (args.contains("--native-recording"))
        {
            nativeRecordingEvidence(root, evidence, qfr::i18n::Translation({ "en" }));
            nativeRecordingEvidence(root, evidence, qfr::i18n::Translation({ "zh-Hans" }));
            nativeFailureAndRecoveryEvidence(root, evidence, qfr::i18n::Translation({ "en" }));
            nativeFailureAndRecoveryEvidence(root, evidence, qfr::i18n::Translation({ "zh-Hans" }));
            nativeMissingInputRecovery(root, evidence, qfr::i18n::Translation({ "en" }));
            nativeMissingInputRecovery(root, evidence, qfr::i18n::Translation({ "zh-Hans" }));
            nativeRecordingEvidence(root, evidence, qfr::i18n::Translation({ "en-XA" }, catalogs));
            std::cout << "PASS Windows default-input recording, translated close confirmation, and saved status\n";
        }
        const auto detected = qfr::i18n::systemLanguagePreferences();
        std::cout << "Detected interface preferences: " << detected.joinIntoString(", ") << '\n';
        return 0;
    }
    catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
