// SPDX-License-Identifier: AGPL-3.0-only
#include "RecorderComponent.h"
#include "localization/SystemLanguages.h"
#include "X11Startup.h"

#if defined(__linux__)
namespace
{
// START_JUCE_APPLICATION initializes JUCE's X11 backend before Application::initialise,
// so the atom must be registered during static startup.
const auto x11PreparedForJuce = []
{
    qfr::prepareX11ForJuce();
    return true;
}();
}
#endif

namespace qfr
{
class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow(juce::PropertiesFile& preferences, i18n::Translation translation, std::function<void()> quit)
        : DocumentWindow("QuickFlacRecorder", theme::background, DocumentWindow::closeButton
                          | DocumentWindow::minimiseButton), quitApplication(std::move(quit))
    {
        setUsingNativeTitleBar(true);
        recorder = new RecorderComponent(preferences, std::move(translation));
        setContentOwned(recorder, true);
        setResizable(false, false);
        centreWithSize(getWidth(), getHeight());
        if (preferences.containsKey("windowX") && preferences.containsKey("windowY"))
            setBoundsConstrained({ preferences.getIntValue("windowX"), preferences.getIntValue("windowY"),
                                   getWidth(), getHeight() });
        setVisible(true);
    }

    ~MainWindow() override { recorder->saveWindowPosition(getBounds()); }
    void closeButtonPressed() override { recorder->requestClose(quitApplication); }

private:
    RecorderComponent* recorder = nullptr;
    std::function<void()> quitApplication;
};

class Application final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "QuickFlacRecorder"; }
    const juce::String getApplicationVersion() override { return QFR_APPLICATION_VERSION; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String&) override
    {
        juce::PropertiesFile::Options options;
        options.applicationName = "QuickFlacRecorder";
        options.filenameSuffix = ".settings";
        options.folderName = "QuickFlacRecorder";
        options.osxLibrarySubFolder = "Application Support";
        options.storageFormat = juce::PropertiesFile::storeAsXML;
        preferences = std::make_unique<juce::PropertiesFile>(options);
        // Re-query the current user's interface preferences on every launch.
        // Recorder settings never persist or override this immutable selection.
        i18n::Translation translation(i18n::systemLanguagePreferences());
        window = std::make_unique<MainWindow>(*preferences, std::move(translation), [this] { quit(); });
    }

    void shutdown() override
    {
        window.reset();
        preferences->saveIfNeeded();
        preferences.reset();
    }

    void systemRequestedQuit() override { if (window) window->closeButtonPressed(); }
    void anotherInstanceStarted(const juce::String&) override {}

private:
    std::unique_ptr<juce::PropertiesFile> preferences;
    std::unique_ptr<MainWindow> window;
};
}

START_JUCE_APPLICATION(qfr::Application)
