#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_devices/juce_audio_devices.h>

class MainComponent final : public juce::Component
{
public:
    MainComponent()
    {
        tone.setFrequency (440.0);
        tone.setAmplitude (0.18f);
        player.setSource (&tone);

        const auto error = deviceManager.initialise (0, 2, nullptr, true);

        if (error.isEmpty())
            audioReady = true;
        else
            lastError = error;

        setOpaque (true);
        setSize (720, 360);
    }

    ~MainComponent() override
    {
        deviceManager.removeAudioCallback (&player);
        player.setSource (nullptr);
        deviceManager.closeAudioDevice();
    }

    void paint (juce::Graphics& g) override
    {
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
        if (getToggleBounds().contains (event.getPosition()))
            toggleTone();
    }

private:
    juce::Rectangle<int> getToggleBounds() const
    {
        return getLocalBounds().withSizeKeepingCentre (120, 120).translated (0, 12);
    }

    void toggleTone()
    {
        if (! audioReady)
            return;

        if (! toneEnabled)
            deviceManager.addAudioCallback (&player);
        else
            deviceManager.removeAudioCallback (&player);

        toneEnabled = ! toneEnabled;
        repaint();
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
        mainWindow = std::make_unique<MainWindow> (getApplicationName());
    }

    void shutdown() override
    {
        mainWindow.reset();
    }

    void systemRequestedQuit() override
    {
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
            setUsingNativeTitleBar (true);
            setResizable (true, true);
            setContentOwned (new MainComponent(), true);
            centreWithSize (720, 360);
            setVisible (true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

private:
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (QnxHelloWorldApplication)
