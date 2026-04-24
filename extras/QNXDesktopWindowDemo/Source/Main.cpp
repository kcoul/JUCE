#include <juce_gui_basics/juce_gui_basics.h>
#include <unistd.h>

#include "GeneratedBuildVersion.h"

namespace
{
    constexpr int fallbackWidth = 1280;
    constexpr int fallbackHeight = 1024;

    class FpsCounter
    {
    public:
        void frameRendered()
        {
            const auto nowMs = juce::Time::getMillisecondCounterHiRes();

            if (lastFrameMs > 0.0)
            {
                const auto frameMs = juce::jmax (0.001, nowMs - lastFrameMs);
                averageFrameMs += (frameMs - averageFrameMs) * smoothingFactor;
            }

            lastFrameMs = nowMs;
            ++frameCount;
        }

        juce::String getSummaryText (const juce::String& rendererTag) const
        {
            if (frameCount < 2 || averageFrameMs <= 0.0)
                return rendererTag + " FPS --";

            return rendererTag + " FPS " + juce::String (1000.0 / averageFrameMs, 1);
        }

        double getFramesPerSecond() const
        {
            if (frameCount < 2 || averageFrameMs <= 0.0)
                return 0.0;

            return 1000.0 / averageFrameMs;
        }

    private:
        static constexpr double smoothingFactor = 0.12;

        double lastFrameMs = 0.0;
        double averageFrameMs = 0.0;
        int frameCount = 0;
    };

    juce::Rectangle<int> getInitialDisplayArea()
    {
        if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        {
            const auto logicalBounds = display->logicalBounds.getSmallestIntegerContainer();

            if (! logicalBounds.isEmpty())
                return logicalBounds;

            if (! display->totalArea.isEmpty())
                return display->totalArea;
        }

        return { 0, 0, fallbackWidth, fallbackHeight };
    }

    std::unique_ptr<juce::FileLogger> createAppLogger()
    {
        auto logFile = juce::File::getSpecialLocation (juce::File::currentApplicationFile)
                           .getParentDirectory()
                           .getChildFile ("JUCEQNXDesktopWindowDemo.log");

        return std::make_unique<juce::FileLogger> (logFile,
                                                   "JUCE QNX Desktop Window Demo",
                                                   512 * 1024);
    }

    class DesktopContentComponent final : public juce::Component
    {
    public:
        DesktopContentComponent()
        {
            setOpaque (true);

            closeButton.setButtonText ("Close");
            closeButton.onClick = [] { juce::JUCEApplication::getInstance()->systemRequestedQuit(); };
            addAndMakeVisible (closeButton);
        }

        ~DesktopContentComponent() override
        {
        }

        void paint (juce::Graphics& g) override
        {
            fpsCounter.frameRendered();
            g.fillAll (juce::Colour::fromRGB (238, 232, 221));

            auto bounds = getLocalBounds().toFloat().reduced (20.0f);
            g.setColour (juce::Colour::fromRGB (34, 49, 63));
            g.fillRoundedRectangle (bounds, 22.0f);

            drawFpsOverlay (g);
        }

        void resized() override
        {
            auto bounds = getLocalBounds().reduced (48, 40);
            auto topRow = bounds.removeFromTop (40);
            closeButton.setBounds (topRow.removeFromRight (110));
        }

    private:
        juce::Rectangle<int> getFpsOverlayBounds() const
        {
            auto bounds = getLocalBounds().reduced (20);
            return { bounds.getX() + 12, bounds.getY() + 12, 104, 40 };
        }

        void drawFpsOverlay (juce::Graphics& g) const
        {
            const auto overlay = getFpsOverlayBounds().toFloat();

            g.setColour (juce::Colour::fromRGBA (11, 16, 20, 170));
            g.fillRoundedRectangle (overlay, 11.0f);
            g.setColour (juce::Colour::fromRGB (239, 196, 76).withAlpha (0.70f));
            g.drawRoundedRectangle (overlay, 11.0f, 1.0f);
            g.setColour (juce::Colour::fromRGB (239, 196, 76).withAlpha (0.16f));
            g.fillRoundedRectangle (overlay.reduced (6.0f), 8.0f);

            auto textBounds = overlay.reduced (12.0f, 7.0f);
            auto titleBounds = textBounds.removeFromTop (13.0f);

            g.setColour (juce::Colour::fromRGB (239, 196, 76).withAlpha (0.88f));
            g.setFont (juce::FontOptions (11.0f));
            g.drawText ("Renderer", titleBounds, juce::Justification::centredLeft);

            g.setColour (juce::Colours::white.withAlpha (0.95f));
            g.setFont (juce::FontOptions (17.0f));
            g.drawText (fpsCounter.getSummaryText ("SW"), textBounds, juce::Justification::centredLeft);
        }

        juce::TextButton closeButton;
        FpsCounter fpsCounter;
    };

    class MainWindow final : public juce::DocumentWindow
    {
    public:
        MainWindow()
            : juce::DocumentWindow ("JUCE QNX Desktop Window Demo",
                                    juce::Colour::fromRGB (26, 39, 51),
                                    juce::DocumentWindow::allButtons,
                                    true)
        {
            setUsingNativeTitleBar (false);
            setResizable (true, true);
            setContentOwned (new DesktopContentComponent(), true);

            auto area = getInitialDisplayArea();
            setBounds (area.withTrimmedLeft (90).withTrimmedTop (72).withWidth (area.getWidth() - 180).withHeight (area.getHeight() - 144));
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };
}

class QnxDesktopWindowApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override       { return "JUCE QNX Desktop Window Demo"; }
    const juce::String getApplicationVersion() override    { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override             { return true; }

    void initialise (const juce::String&) override
    {
        logger = createAppLogger();
        juce::Logger::setCurrentLogger (logger.get());
        juce::Logger::writeToLog ("Build version: " + juce::String (JUCE_QNX_DESKTOP_WINDOW_DEMO_BUILD_VERSION));
        juce::Logger::writeToLog ("Process PID: " + juce::String ((int) getpid()));
        juce::Logger::writeToLog ("Application initialise()");

        mainWindow = std::make_unique<MainWindow>();
        mainWindow->setVisible (true);
        mainWindow->toFront (true);
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

private:
    std::unique_ptr<MainWindow> mainWindow;
    std::unique_ptr<juce::FileLogger> logger;
};

START_JUCE_APPLICATION (QnxDesktopWindowApplication)
