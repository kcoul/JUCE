#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <unistd.h>
#include "GeneratedBuildVersion.h"

namespace
{
    juce::Rectangle<int> getInitialDisplayArea()
    {
        if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
            return display->totalArea;

        return { 0, 0, 1920, 1080 };
    }

    std::unique_ptr<juce::FileLogger> createAppLogger()
    {
        auto logFile = juce::File::getSpecialLocation (juce::File::currentApplicationFile)
                           .getParentDirectory()
                           .getChildFile ("JUCEQNXHelloWorld.log");

        return std::make_unique<juce::FileLogger> (logFile,
                                                   "JUCE QNX Hello World",
                                                   512 * 1024);
    }
}

class MainComponent final : public juce::Component
{
public:
    MainComponent()
    {
        setWantsKeyboardFocus (true);
        setMouseClickGrabsKeyboardFocus (true);
        tone.setFrequency (440.0);
        tone.setAmplitude (0.18f);
        player.setSource (&tone);

        const auto error = deviceManager.initialise (0, 2, nullptr, true, "*USB*");

        if (error.isEmpty())
        {
            audioReady = true;
            juce::Logger::writeToLog ("Audio device manager initialised successfully");
            logCurrentAudioDevice ("After initialise");
            preferUsbAudioOutput();
        }
        else
        {
            lastError = error;
            juce::Logger::writeToLog ("Audio device manager initialisation failed: " + error);
        }

        setOpaque (true);
        const auto displayArea = getInitialDisplayArea();
        setSize (displayArea.getWidth(), displayArea.getHeight());
        juce::Logger::writeToLog ("MainComponent created with size "
                                  + juce::String (getWidth())
                                  + "x"
                                  + juce::String (getHeight()));
        grabKeyboardFocus();
    }

    ~MainComponent() override
    {
        juce::Logger::writeToLog ("MainComponent shutting down");
        deviceManager.removeAudioCallback (&player);
        player.setSource (nullptr);
        deviceManager.closeAudioDevice();
    }

    void paint (juce::Graphics& g) override
    {
        static int paintCount = 0;
        ++paintCount;

        if (paintCount <= 5 || (paintCount % 60) == 0)
            juce::Logger::writeToLog ("MainComponent::paint #" + juce::String (paintCount));

        g.fillAll (juce::Colour::fromRGB (243, 239, 231));

        auto panel = getLocalBounds().toFloat().reduced (24.0f);
        g.setColour (juce::Colour::fromRGB (34, 57, 76));
        g.fillRoundedRectangle (panel, 24.0f);

        g.setColour (juce::Colour::fromRGB (233, 196, 106));
        g.fillEllipse (panel.getX() + 24.0f, panel.getY() + 24.0f, 18.0f, 18.0f);
        g.fillEllipse (panel.getX() + 48.0f, panel.getY() + 24.0f, 18.0f, 18.0f);
        g.fillEllipse (panel.getX() + 72.0f, panel.getY() + 24.0f, 18.0f, 18.0f);

        auto halo = getToggleBounds().toFloat().expanded (toneEnabled ? 14.0f : 8.0f);
        g.setColour ((audioReady && toneEnabled) ? juce::Colour::fromRGBA (42, 157, 143, 90)
                                                 : juce::Colour::fromRGBA (231, 111, 81, 70));
        g.fillEllipse (halo);

        g.setColour ((audioReady && toneEnabled) ? juce::Colour::fromRGB (42, 157, 143)
                                                 : juce::Colour::fromRGB (231, 111, 81));
        g.fillEllipse (getToggleBounds().toFloat());

        juce::Path symbol;

        if (audioReady && toneEnabled)
        {
            auto toggle = getToggleBounds().toFloat().reduced (18.0f);
            symbol.addRectangle (toggle.removeFromLeft (16.0f));
            toggle.removeFromLeft (10.0f);
            symbol.addRectangle (toggle.removeFromLeft (16.0f));
        }
        else
        {
            auto toggle = getToggleBounds().toFloat().reduced (18.0f);
            symbol.addTriangle (toggle.getX(), toggle.getY(),
                                toggle.getRight(), toggle.getCentreY(),
                                toggle.getX(), toggle.getBottom());
        }

        g.setColour (juce::Colours::white);
        g.fillPath (symbol);

        auto indicatorArea = juce::Rectangle<float> (panel.getX() + 40.0f,
                                                     panel.getBottom() - 80.0f,
                                                     panel.getWidth() - 80.0f,
                                                     18.0f);

        g.setColour (juce::Colour::fromRGBA (255, 255, 255, 40));
        g.fillRoundedRectangle (indicatorArea, 9.0f);

        if (audioReady)
        {
            auto fill = indicatorArea.withWidth (toneEnabled ? indicatorArea.getWidth() : indicatorArea.getWidth() * 0.35f);
            g.setColour (toneEnabled ? juce::Colour::fromRGB (42, 157, 143)
                                     : juce::Colour::fromRGB (233, 196, 106));
            g.fillRoundedRectangle (fill, 9.0f);
        }
        else
        {
            auto fill = indicatorArea.withWidth (indicatorArea.getWidth() * 0.25f);
            g.setColour (juce::Colour::fromRGB (231, 111, 81));
            g.fillRoundedRectangle (fill, 9.0f);
        }
    }

    void mouseUp (const juce::MouseEvent& event) override
    {
        juce::Logger::writeToLog ("MainComponent::mouseUp at "
                                  + juce::String (event.getPosition().x)
                                  + ","
                                  + juce::String (event.getPosition().y));

        if (getToggleBounds().contains (event.getPosition()))
            toggleTone();
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        juce::Logger::writeToLog ("MainComponent::keyPressed keyCode=" + juce::String (key.getKeyCode()));

        if (key == juce::KeyPress::escapeKey)
        {
            juce::Logger::writeToLog ("Escape pressed, requesting quit");
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
            return true;
        }

        if (key == juce::KeyPress::spaceKey || key == juce::KeyPress::returnKey)
        {
            juce::Logger::writeToLog ("Keyboard toggle requested");
            toggleTone();
            return true;
        }

        return false;
    }

private:
    juce::Rectangle<int> getToggleBounds() const
    {
        return getLocalBounds().withSizeKeepingCentre (120, 120).translated (0, 12);
    }

    void logCurrentAudioDevice (const juce::String& context) const
    {
        if (auto* currentDevice = deviceManager.getCurrentAudioDevice())
            juce::Logger::writeToLog (context + ": current audio device '" + currentDevice->getName() + "' type=" + currentDevice->getTypeName());
        else
            juce::Logger::writeToLog (context + ": no current audio device");
    }

    void toggleTone()
    {
        if (! audioReady)
        {
            juce::Logger::writeToLog ("toggleTone ignored because audio is not ready");
            return;
        }

        if (! toneEnabled)
            deviceManager.addAudioCallback (&player);
        else
            deviceManager.removeAudioCallback (&player);

        toneEnabled = ! toneEnabled;
        juce::Logger::writeToLog ("Tone toggled " + juce::String (toneEnabled ? "on" : "off"));
        repaint();
    }

    void preferUsbAudioOutput()
    {
        const auto& deviceTypes = deviceManager.getAvailableDeviceTypes();

        for (auto* type : deviceTypes)
        {
            if (type == nullptr)
                continue;

            type->scanForDevices();
            const auto outputNames = type->getDeviceNames (false);

            juce::Logger::writeToLog ("Audio device type: " + type->getTypeName());

            for (const auto& outputName : outputNames)
                juce::Logger::writeToLog ("  Output device: " + outputName);
        }

        auto setup = deviceManager.getAudioDeviceSetup();
        juce::String usbDeviceName;

        for (auto* type : deviceTypes)
        {
            if (type == nullptr)
                continue;

            const auto outputNames = type->getDeviceNames (false);

            for (const auto& outputName : outputNames)
            {
                if (outputName.containsIgnoreCase ("USB"))
                {
                    usbDeviceName = outputName;
                    break;
                }
            }

            if (usbDeviceName.isNotEmpty())
                break;
        }

        if (usbDeviceName.isEmpty())
        {
            juce::Logger::writeToLog ("No USB audio output device found; leaving current output unchanged");
            return;
        }

        setup.outputDeviceName = usbDeviceName;
        setup.useDefaultOutputChannels = true;
        setup.useDefaultInputChannels = true;

        const auto setupError = deviceManager.setAudioDeviceSetup (setup, true);

        if (setupError.isEmpty())
        {
            juce::Logger::writeToLog ("Selected USB audio output device: " + usbDeviceName);
            logCurrentAudioDevice ("After USB device selection");
        }
        else
            juce::Logger::writeToLog ("Failed to select USB audio output device '" + usbDeviceName + "': " + setupError);
    }

    juce::AudioDeviceManager deviceManager;
    juce::AudioSourcePlayer player;
    juce::ToneGeneratorAudioSource tone;
    juce::String lastError;
    bool audioReady = false;
    bool toneEnabled = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

class QnxHelloWorldApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override       { return "JUCE QNX Hello World"; }
    const juce::String getApplicationVersion() override    { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override             { return true; }

    void initialise (const juce::String&) override
    {
        logger = createAppLogger();
        juce::Logger::setCurrentLogger (logger.get());
        juce::Logger::writeToLog ("Build version: " + juce::String (JUCE_QNX_HELLO_WORLD_BUILD_VERSION));
        juce::Logger::writeToLog ("Process PID: " + juce::String ((int) getpid()));
        juce::Logger::writeToLog ("Application initialise()");
        mainWindow = std::make_unique<MainWindow> (getApplicationName());
    }

    void shutdown() override
    {
        juce::Logger::writeToLog ("Application shutdown()");
        mainWindow.reset();
        juce::Logger::setCurrentLogger (nullptr);
        logger.reset();
    }

    void systemRequestedQuit() override
    {
        juce::Logger::writeToLog ("systemRequestedQuit()");
        quit();
    }

    void anotherInstanceStarted (const juce::String&) override {}

    class MainWindow final : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (juce::String name)
            : DocumentWindow (std::move (name),
                              juce::Colours::lightgrey,
                              juce::DocumentWindow::allButtons)
        {
            juce::Logger::writeToLog ("MainWindow constructed");
            setUsingNativeTitleBar (false);
            setResizable (false, false);
            setContentOwned (new MainComponent(), true);
            setBounds (getInitialDisplayArea());
            setFullScreen (true);
            setVisible (true);
            toFront (true);
            juce::Logger::writeToLog ("MainWindow made visible");
        }

        void closeButtonPressed() override
        {
            juce::Logger::writeToLog ("MainWindow::closeButtonPressed()");
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

private:
    std::unique_ptr<MainWindow> mainWindow;
    std::unique_ptr<juce::FileLogger> logger;
};

START_JUCE_APPLICATION (QnxHelloWorldApplication)
