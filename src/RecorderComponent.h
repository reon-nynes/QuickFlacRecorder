// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "InputController.h"
#include "RecordingPresentation.h"
#include "RecorderTheme.h"
#include <juce_gui_extra/juce_gui_extra.h>

namespace qfr
{
class LevelMeter final : public juce::Component
{
public:
    explicit LevelMeter(const i18n::Translation& selected) : translation(selected) {}
    void update(const RecordingSession::Levels&);
    void paint(juce::Graphics&) override;
private:
    const i18n::Translation& translation;
    std::array<float, 2> db { -60.0f, -60.0f };
    std::array<double, 2> clipUntil { 0, 0 };
    int channels = 1;
};

class RecorderComponent final : public juce::Component,
                                private juce::Timer,
                                private juce::KeyListener
{
public:
    RecorderComponent(juce::PropertiesFile&, i18n::Translation, RecordingSession::Options = {});
    ~RecorderComponent() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void requestClose(std::function<void()> quit);
    void saveWindowPosition(const juce::Rectangle<int>&);

private:
    void timerCallback() override;
    bool keyPressed(const juce::KeyPress&, juce::Component*) override;
    void populateDrivers();
    void populateDevices(const juce::String& selected);
    void populateChannels();
    void openInput();
    void configureSession();
    void toggleRecording();
    void chooseFolder();
    void savePreferences();
    void updateStatus();
    void requestMicrophonePermission();

    juce::PropertiesFile& preferences;
    const i18n::Translation translation;
    RecordingSession session;
    InputController input { session };
    theme::LookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips { this, 600 };
    juce::TextEditor targetFolder, recordingPrefix;
    juce::ComboBox audioDriver, inputDevice, mode, leftChannel, rightChannel;
    juce::TextButton browse, refreshInputs;
    theme::RecordingButton recordButton;
    juce::Label folderLabel, prefixLabel, driverLabel, deviceLabel, modeLabel, leftLabel, rightLabel;
    juce::Label filename, format, elapsed, status;
    LevelMeter meter { translation };
    std::unique_ptr<juce::FileChooser> folderChooser;
    std::function<void()> quitAfterStop;
    RecorderResult inputError, configurationError;
    juce::String selectedDriver, selectedDevice;
    juce::StringArray driverNames, deviceNames;
    bool microphoneAllowed = false, closeDialogOpen = false;
    int incompleteRecordings = 0;
    double lastPreviewRefresh = 0;
};
}
