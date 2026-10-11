// SPDX-License-Identifier: AGPL-3.0-only
#include "RecorderComponent.h"
#include <cmath>
#if JUCE_WINDOWS
 #include <windows.h>
 #include <shlobj.h>
#endif

namespace qfr
{
namespace
{
juce::File downloadsFolder()
{
   #if JUCE_WINDOWS
    PWSTR path = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, KF_FLAG_DONT_VERIFY, nullptr, &path)))
    {
        const juce::File result { juce::String(path) };
        CoTaskMemFree(path);
        return result;
    }
    return {}; // Require a folder choice if Windows cannot resolve Downloads.
   #elif JUCE_LINUX
    const auto home = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
    const auto config = juce::SystemStats::getEnvironmentVariable("XDG_CONFIG_HOME", home.getChildFile(".config").getFullPathName());
    const auto lines = juce::StringArray::fromLines(juce::File(config).getChildFile("user-dirs.dirs").loadFileAsString());
    for (const auto& line : lines)
    {
        if (! line.trimStart().startsWith("XDG_DOWNLOAD_DIR=")) continue;
        auto path = line.fromFirstOccurrenceOf("=", false, false).trim().unquoted();
        path = path.replace("$HOME", home.getFullPathName()).replace("\\\"", "\"").replace("\\\\", "\\");
        return juce::File::isAbsolutePath(path) ? juce::File(path) : juce::File();
    }
    return home.getChildFile("Downloads");
   #else
    return juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile("Downloads");
   #endif
}
}

void LevelMeter::update(const RecordingSession::Levels& levels)
{
    channels = levels.channels;
    const auto now = juce::Time::getMillisecondCounterHiRes();
    for (size_t channel = 0; channel < db.size(); ++channel)
    {
        const auto peakDb = juce::Decibels::gainToDecibels(levels.peak[channel], -60.0f);
        db[channel] = juce::jmax(peakDb, db[channel] - 2.4f);
        if (levels.clipped[channel]) clipUntil[channel] = now + 1200;
    }
    setTitle("Selected input levels");
    setDescription(juce::String(db[0], 1) + " dBFS"
                   + (channels == 2 ? ", right " + juce::String(db[1], 1) + " dBFS" : ""));
    repaint();
}

void LevelMeter::paint(juce::Graphics& g)
{
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const int barHeight = channels == 1 ? 22 : 18;
    const int top = channels == 1 ? 8 : 2;
    const int barX = 18;
    const int barWidth = getWidth() - 114;
    g.setFont(theme::data());
    for (int channel = 0; channel < channels; ++channel)
    {
        const auto y = top + channel * 24;
        const auto bounds = juce::Rectangle<float>(static_cast<float>(barX), static_cast<float>(y),
                                                   static_cast<float>(barWidth), static_cast<float>(barHeight));
        g.setColour(theme::muted);
        g.drawText(channels == 1 ? "M" : (channel == 0 ? "L" : "R"), 0, y, 14, barHeight,
                   juce::Justification::centred);
        g.setColour(theme::surface);
        g.fillRoundedRectangle(bounds, 2.0f);
        const auto fraction = juce::jlimit(0.0f, 1.0f, (db[static_cast<size_t>(channel)] + 60.0f) / 60.0f);
        g.setColour(db[static_cast<size_t>(channel)] > -6 ? theme::warning : theme::meter);
        g.fillRoundedRectangle(bounds.withWidth(bounds.getWidth() * fraction), 2.0f);
        for (int marker = 1; marker < 6; ++marker)
        {
            g.setColour(theme::background.withAlpha(0.6f));
            g.drawVerticalLine(barX + marker * barWidth / 6, bounds.getY(), bounds.getBottom());
        }
        g.setColour(theme::text);
        const auto value = db[static_cast<size_t>(channel)] <= -60 ? "-inf" : juce::String(db[static_cast<size_t>(channel)], 1);
        g.drawText(value, barX + barWidth + 4, y, 52, barHeight, juce::Justification::centredRight);
        if (now < clipUntil[static_cast<size_t>(channel)])
        {
            g.setColour(theme::danger);
            g.drawText("CLIP", getWidth() - 34, y, 34, barHeight, juce::Justification::centredRight);
        }
    }
    g.setColour(theme::muted);
    g.drawText("-60", barX, 48, 28, 16, juce::Justification::left);
    g.drawText("-30", barX + barWidth / 2 - 14, 48, 28, 16, juce::Justification::centred);
    g.drawText("0 dBFS", barX + barWidth - 50, 48, 50, 16, juce::Justification::right);
}

RecorderComponent::RecorderComponent(juce::PropertiesFile& settings) : preferences(settings)
{
    setLookAndFeel(&lookAndFeel);
    setWantsKeyboardFocus(true);
    addKeyListener(this);
    const auto label = [this](juce::Label& item, const juce::String& text, juce::Component* control)
    {
        item.setText(text, juce::dontSendNotification);
        item.setFont(theme::body(theme::labelSize));
        item.setColour(juce::Label::textColourId, theme::muted);
        addAndMakeVisible(item);
        if (control) control->setTitle(text);
    };
    label(folderLabel, "Target folder", &targetFolder);
    label(prefixLabel, "Prefix", &recordingPrefix);
    label(driverLabel, "Audio driver", &audioDriver);
    label(deviceLabel, "Input device", &inputDevice);
    label(modeLabel, "Channels", &mode);
    label(leftLabel, "Mono input", &leftChannel);
    label(rightLabel, "Right input", &rightChannel);
    for (auto* editor : { &targetFolder, &recordingPrefix })
    {
        addAndMakeVisible(editor);
        editor->setFont(theme::body());
        editor->setSelectAllWhenFocused(true);
        editor->onTextChange = [this] { configureSession(); savePreferences(); };
    }
    for (auto* selector : { &audioDriver, &inputDevice, &mode, &leftChannel, &rightChannel })
    {
        addAndMakeVisible(selector);
        selector->setTextWhenNothingSelected("Choose...");
        selector->setScrollWheelEnabled(false);
    }
    for (auto* button : { &browse, &recordButton, &refreshInputs }) addAndMakeVisible(button);
    for (auto* item : { &filename, &format, &elapsed, &status })
    {
        addAndMakeVisible(item);
        item->setFont(theme::body());
        item->setMinimumHorizontalScale(1.0f);
    }
    recordButton.setComponentID("recordingAction");
    filename.setFont(theme::data());
    filename.setTitle("Next filename");
    format.setColour(juce::Label::textColourId, theme::muted);
    format.setFont(theme::body(theme::secondarySize));
    elapsed.setFont(theme::data(theme::timerSize));
    elapsed.setJustificationType(juce::Justification::centred);
    elapsed.setTitle("Elapsed recording time");
    status.setJustificationType(juce::Justification::topLeft);
    status.setTitle("Recording status");
    addAndMakeVisible(meter);

    targetFolder.setText(preferences.getValue("targetFolder", downloadsFolder().getFullPathName()), false);
    recordingPrefix.setText(juce::Time::getCurrentTime().formatted("%Y-%m-%d_%H-%M-%S"), false);
    targetFolder.setTooltip("Choose an existing writable folder. Recordings will only be saved here.");
    recordingPrefix.setTooltip("The app appends _01.flac, _02.flac, and later counters. The prefix stays across takes.");
    audioDriver.setTooltip("Shared audio is the normal system path. Exclusive or ASIO requires an available device.");
    inputDevice.setTooltip("Choose the microphone or audio interface used for both the meter and recording.");
    recordButton.setTooltip("Start or stop a recording. Space works when a field or selector does not have focus.");
    refreshInputs.setTooltip("Look for connected inputs again, then choose a replacement if needed.");
    browse.onClick = [this] { chooseFolder(); };
    recordButton.onClick = [this] { toggleRecording(); };
    refreshInputs.onClick = [this]
    {
        input.rescan();
        populateDrivers();
        populateDevices(inputDevice.getText());
        openInput();
    };
    audioDriver.onChange = [this]
    {
        populateDevices(input.defaultDevice(audioDriver.getText()));
        openInput();
    };
    inputDevice.onChange = [this] { openInput(); };
    mode.addItem("Mono", 1);
    mode.addItem("Stereo", 2);
    mode.setSelectedId(preferences.getBoolValue("stereo", false) ? 2 : 1, juce::dontSendNotification);
    mode.onChange = [this] { configureSession(); resized(); savePreferences(); };
    leftChannel.onChange = rightChannel.onChange = [this] { configureSession(); savePreferences(); };
    populateDrivers();
    populateDevices(preferences.getValue("inputDevice", input.defaultDevice(audioDriver.getText())));
    input.onDevicesChanged = [this]
    {
        populateDevices(inputDevice.getText());
        if (! input.isAvailable()) inputError = input.pollError();
        updateStatus();
    };
    setSize(theme::contentWidth, theme::contentHeight);
    requestMicrophonePermission();
    startTimerHz(25);
}

RecorderComponent::~RecorderComponent()
{
    stopTimer();
    input.onDevicesChanged = {};
    session.requestStop();
    input.close();
    session.waitForStop();
    savePreferences();
    removeKeyListener(this);
    setLookAndFeel(nullptr);
}

void RecorderComponent::requestMicrophonePermission()
{
    const juce::Component::SafePointer<RecorderComponent> safeThis(this);
    juce::RuntimePermissions::request(juce::RuntimePermissions::recordAudio, [safeThis](bool granted)
    {
        if (safeThis == nullptr) return;
        safeThis->microphoneAllowed = granted;
        if (granted) safeThis->openInput();
        else
        {
            safeThis->inputError = "Microphone permission denied. Allow microphone access in system privacy settings and reopen the app.";
            safeThis->configureSession();
        }
    });
}

void RecorderComponent::populateDrivers()
{
    const auto selected = audioDriver.getText().isNotEmpty() ? audioDriver.getText().upToFirstOccurrenceOf(" (unavailable)", false, false)
        : preferences.getValue("audioDriver", input.defaultDriver());
    audioDriver.clear(juce::dontSendNotification);
    const auto drivers = input.drivers();
    audioDriver.addItemList(drivers, 1);
    if (drivers.contains(selected)) audioDriver.setSelectedItemIndex(drivers.indexOf(selected), juce::dontSendNotification);
    else audioDriver.setText(selected + " (unavailable)", juce::dontSendNotification);
}

void RecorderComponent::populateDevices(const juce::String& selected)
{
    inputDevice.clear(juce::dontSendNotification);
    const auto devices = input.devices(audioDriver.getText());
    inputDevice.addItemList(devices, 1);
    if (devices.contains(selected)) inputDevice.setSelectedItemIndex(devices.indexOf(selected), juce::dontSendNotification);
    else if (selected.isNotEmpty()) inputDevice.setText(selected, juce::dontSendNotification);
    else inputDevice.setText("No input available", juce::dontSendNotification);
    inputDevice.setTooltip(devices.contains(selected) ? selected
        : selected + " is unavailable. Choose an available input device.");
}

void RecorderComponent::populateChannels()
{
    const auto channelNames = input.channels();
    const auto left = leftChannel.getSelectedId() > 0 ? leftChannel.getSelectedId()
        : preferences.getIntValue("leftChannel", 1);
    const auto right = rightChannel.getSelectedId() > 0 ? rightChannel.getSelectedId()
        : preferences.getIntValue("rightChannel", 2);
    leftChannel.clear(juce::dontSendNotification);
    rightChannel.clear(juce::dontSendNotification);
    for (int i = 0; i < channelNames.size(); ++i)
    {
        const auto name = juce::String(i + 1) + ": " + channelNames[i];
        leftChannel.addItem(name, i + 1);
        rightChannel.addItem(name, i + 1);
    }
    if (left <= channelNames.size()) leftChannel.setSelectedId(left, juce::dontSendNotification);
    if (right <= channelNames.size()) rightChannel.setSelectedId(right, juce::dontSendNotification);
}

void RecorderComponent::openInput()
{
    if (session.snapshot().controlsLocked()) return;
    if (! microphoneAllowed) { configureSession(); return; }
    const auto result = input.open(audioDriver.getText(), inputDevice.getText());
    inputError = result.getErrorMessage();
    populateChannels();
    configureSession();
    savePreferences();
}

void RecorderComponent::configureSession()
{
    if (session.snapshot().controlsLocked()) return;
    RecordingSession::Configuration config;
    config.targetFolder = juce::File(targetFolder.getText());
    config.recordingPrefix = recordingPrefix.getText();
    config.inputStreamRate = input.sampleRate();
    config.leftInputChannel = leftChannel.getSelectedId() - 1;
    config.rightInputChannel = mode.getSelectedId() == 2 ? rightChannel.getSelectedId() - 1 : -1;
    if (mode.getSelectedId() == 2 && rightChannel.getSelectedId() <= 0)
        configurationError = "Choose two available input channels for stereo.";
    else if (leftChannel.getSelectedId() <= 0)
        configurationError = "Choose an available mono input or stereo left input channel.";
    else configurationError = session.configure(config).getErrorMessage();
    incompleteRecordings = config.targetFolder.isDirectory()
        ? config.targetFolder.findChildFiles(juce::File::findFiles, false, "*.partial.flac").size() : 0;
    updateStatus();
}

void RecorderComponent::toggleRecording()
{
    const auto state = session.snapshot().state;
    if (state == RecordingSession::State::recording)
    {
        session.requestStop();
        updateStatus();
        return;
    }
    if (state == RecordingSession::State::finalizing) return;
    configureSession();
    if (inputError.isNotEmpty() || configurationError.isNotEmpty() || ! input.isAvailable()) return;
    configurationError = session.start().getErrorMessage();
    updateStatus();
}

void RecorderComponent::timerCallback()
{
    const auto error = input.pollError();
    if (error.isNotEmpty()) inputError = error;
    meter.update(session.readLevels());
    const auto snapshot = session.snapshot();
    const auto now = juce::Time::getMillisecondCounterHiRes();
    if (snapshot.state == RecordingSession::State::idle && now - lastPreviewRefresh > 1000
        && configurationError.isEmpty())
    {
        session.refreshPreview();
        lastPreviewRefresh = now;
    }
    updateStatus();
    if (quitAfterStop && ! session.snapshot().controlsLocked())
    {
        auto quit = std::move(quitAfterStop);
        quit();
    }
}

void RecorderComponent::updateStatus()
{
    const auto snapshot = session.snapshot();
    const auto locked = snapshot.controlsLocked();
    for (auto* component : std::initializer_list<juce::Component*> {
        &targetFolder, &recordingPrefix, &browse, &audioDriver, &inputDevice, &mode,
        &leftChannel, &rightChannel, &refreshInputs }) component->setEnabled(! locked);
    recordButton.setButtonText(snapshot.state == RecordingSession::State::recording ? "Stop"
        : (snapshot.state == RecordingSession::State::finalizing ? "Finishing..." : "Record"));
    recordButton.setColour(juce::TextButton::buttonColourId,
        snapshot.state == RecordingSession::State::recording ? theme::record.interpolatedWith(theme::surface, 0.65f)
        : snapshot.state == RecordingSession::State::finalizing ? theme::surface
        : theme::record.interpolatedWith(theme::surface, 0.85f));
    recordButton.setEnabled(snapshot.state == RecordingSession::State::recording
        || (! locked && input.isAvailable() && inputError.isEmpty() && configurationError.isEmpty()));
    leftLabel.setText(mode.getSelectedId() == 2 ? "Left input" : "Mono input", juce::dontSendNotification);
    leftChannel.setTitle(mode.getSelectedId() == 2 ? "Left input" : "Mono input");
    const auto seconds = static_cast<int>(snapshot.elapsedSeconds);
    elapsed.setText(juce::String(seconds / 3600).paddedLeft('0', 2) + ":"
        + juce::String(seconds / 60 % 60).paddedLeft('0', 2) + ":"
        + juce::String(seconds % 60).paddedLeft('0', 2), juce::dontSendNotification);
    format.setText((input.sampleRate() > 0 ? juce::String(input.sampleRate(), 0) + " Hz" : "Input unavailable")
        + "  /  16-bit FLAC", juce::dontSendNotification);
    filename.setText("Next  " + snapshot.nextFile.getFileName(), juce::dontSendNotification);
    filename.setTooltip(snapshot.nextFile.getFullPathName());
    audioDriver.setTooltip(audioDriver.getText() + "\nShared audio is the normal system path. Exclusive or ASIO requires an available device.");
    targetFolder.setTooltip(targetFolder.getText() + "\nChoose an existing writable target folder.");
    recordingPrefix.setTooltip(recordingPrefix.getText() + "\nThe app appends _01.flac, _02.flac, and later counters.");
    leftChannel.setTooltip(leftChannel.getText());
    rightChannel.setTooltip(rightChannel.getText());

    auto message = snapshot.message;
    auto colour = theme::muted;
    if (snapshot.state == RecordingSession::State::failed)
    {
        colour = theme::danger;
    }
    else if (! locked && inputError.isNotEmpty()) { message = inputError; colour = theme::danger; }
    else if (! locked && configurationError.isNotEmpty()) { message = configurationError; colour = theme::danger; }
    else if (snapshot.state == RecordingSession::State::succeeded)
    {
        colour = theme::meter;
    }
    else if (! locked && incompleteRecordings > 0)
    {
        message = juce::String(incompleteRecordings) + " incomplete recording(s). Inspect .partial.flac files in the target folder.";
        colour = theme::warning;
    }
    else if (! locked && message.isEmpty()) message = "Ready. Check the input level, then press Record.";
    status.setColour(juce::Label::textColourId, colour);
    status.setText(message, juce::dontSendNotification);
    status.setTooltip(message);
}

bool RecorderComponent::keyPressed(const juce::KeyPress& key, juce::Component*)
{
    if (key.getKeyCode() != juce::KeyPress::spaceKey) return false;
    auto* focused = juce::Component::getCurrentlyFocusedComponent();
    for (auto* ancestor = focused; ancestor != nullptr; ancestor = ancestor->getParentComponent())
        if (dynamic_cast<juce::TextEditor*>(ancestor) || dynamic_cast<juce::ComboBox*>(ancestor)) return false;
    if (juce::ModalComponentManager::getInstance()->getNumModalComponents() > 0 || closeDialogOpen) return false;
    toggleRecording();
    return true;
}

void RecorderComponent::chooseFolder()
{
    folderChooser = std::make_unique<juce::FileChooser>("Choose an existing target folder", juce::File(targetFolder.getText()));
    const juce::Component::SafePointer<RecorderComponent> safeThis(this);
    folderChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [safeThis](const juce::FileChooser& chooser)
        {
            if (safeThis != nullptr && chooser.getResult().isDirectory())
                safeThis->targetFolder.setText(chooser.getResult().getFullPathName());
        });
}

void RecorderComponent::savePreferences()
{
    preferences.setValue("targetFolder", targetFolder.getText());
    // Keep unavailable remembered selections verbatim until the user replaces them.
    if (audioDriver.getSelectedId() > 0) preferences.setValue("audioDriver", audioDriver.getText());
    preferences.setValue("inputDevice", inputDevice.getText());
    preferences.setValue("stereo", mode.getSelectedId() == 2);
    if (leftChannel.getSelectedId() > 0) preferences.setValue("leftChannel", leftChannel.getSelectedId());
    if (rightChannel.getSelectedId() > 0) preferences.setValue("rightChannel", rightChannel.getSelectedId());
    preferences.saveIfNeeded();
}

void RecorderComponent::saveWindowPosition(const juce::Rectangle<int>& bounds)
{
    preferences.setValue("windowX", bounds.getX());
    preferences.setValue("windowY", bounds.getY());
    preferences.saveIfNeeded();
}

void RecorderComponent::requestClose(std::function<void()> quit)
{
    const auto snapshot = session.snapshot();
    if (! snapshot.controlsLocked()) { quit(); return; }
    if (snapshot.state == RecordingSession::State::finalizing)
    {
        quitAfterStop = std::move(quit);
        return;
    }
    if (closeDialogOpen) return;
    closeDialogOpen = true;
    const juce::Component::SafePointer<RecorderComponent> safeThis(this);
    auto* dialog = new juce::AlertWindow("Recording is still running",
        "Keep recording, or stop and finish saving before closing QuickFlacRecorder.",
        juce::MessageBoxIconType::QuestionIcon, this);
    dialog->addButton("Continue recording", 1, juce::KeyPress(juce::KeyPress::returnKey),
                      juce::KeyPress(juce::KeyPress::escapeKey));
    dialog->addButton("Stop and quit", 2);
    dialog->enterModalState(true, juce::ModalCallbackFunction::create(
        [safeThis, quit = std::move(quit)](int result) mutable
        {
            if (safeThis == nullptr) return;
            safeThis->closeDialogOpen = false;
            if (result == 2)
            {
                safeThis->quitAfterStop = std::move(quit);
                safeThis->session.requestStop();
                safeThis->updateStatus();
            }
        }), true);
}

void RecorderComponent::paint(juce::Graphics& g) { g.fillAll(theme::background); }

void RecorderComponent::resized()
{
    folderLabel.setBounds(10, 14, 98, 28);
    targetFolder.setBounds(112, 14, 392, 28);
    browse.setBounds(510, 14, 76, 28);
    prefixLabel.setBounds(10, 48, 98, 28);
    recordingPrefix.setBounds(112, 48, 474, 28);
    driverLabel.setBounds(10, 80, 176, 18);
    deviceLabel.setBounds(196, 80, 300, 18);
    audioDriver.setBounds(14, 98, 174, 28);
    inputDevice.setBounds(200, 98, 304, 28);
    refreshInputs.setBounds(510, 98, 76, 28);
    modeLabel.setBounds(10, 128, 100, 18);
    leftLabel.setBounds(122, 128, 220, 18);
    rightLabel.setBounds(358, 128, 226, 18);
    mode.setBounds(14, 146, 100, 28);
    leftChannel.setBounds(126, 146, mode.getSelectedId() == 2 ? 224 : 460, 28);
    rightChannel.setBounds(362, 146, 224, 28);
    rightLabel.setVisible(mode.getSelectedId() == 2);
    rightChannel.setVisible(mode.getSelectedId() == 2);
    elapsed.setBounds(theme::margin, 178, getWidth() - 2 * theme::margin, theme::timerAreaHeight);
    meter.setBounds(14, 244, 416, 64);
    recordButton.setBounds(446, 244, theme::recordWidth, theme::recordHeight);
    format.setBounds(10, 310, 580, 18);
    filename.setBounds(10, 330, 580, 18);
    status.setBounds(10, 350, 580, 36);
}
}
