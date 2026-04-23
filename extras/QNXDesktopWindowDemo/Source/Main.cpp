#include <juce_gui_basics/juce_gui_basics.h>
#include <unistd.h>

#include "GeneratedBuildVersion.h"

namespace
{
    constexpr int fallbackWidth = 1280;
    constexpr int fallbackHeight = 1024;

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

            titleLabel.setText ("Desktop Window Integration Demo", juce::dontSendNotification);
            titleLabel.setJustificationType (juce::Justification::centredLeft);
            titleLabel.setFont (juce::FontOptions (28.0f));
            titleLabel.setColour (juce::Label::textColourId, juce::Colours::white);
            addAndMakeVisible (titleLabel);

            infoLabel.setText ("Target this at the Raspberry Pi 5 path. The goal here is correct JUCE desktop peers, title bars, focus, menus, and layered windows.", juce::dontSendNotification);
            infoLabel.setJustificationType (juce::Justification::topLeft);
            infoLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.86f));
            infoLabel.setFont (juce::FontOptions (17.0f));
            addAndMakeVisible (infoLabel);

            closeButton.setButtonText ("Close");
            closeButton.onClick = [] { juce::JUCEApplication::getInstance()->systemRequestedQuit(); };
            addAndMakeVisible (closeButton);

            childWindowButton.setButtonText ("Open Utility Window");
            childWindowButton.onClick = [this] { toggleUtilityWindow(); };
            addAndMakeVisible (childWindowButton);
        }

        ~DesktopContentComponent() override
        {
            utilityWindow.reset();
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colour::fromRGB (238, 232, 221));

            auto bounds = getLocalBounds().toFloat().reduced (20.0f);
            g.setColour (juce::Colour::fromRGB (34, 49, 63));
            g.fillRoundedRectangle (bounds, 22.0f);

            auto panel = bounds.reduced (24.0f, 20.0f);
            auto status = panel.removeFromBottom (118.0f);

            g.setColour (juce::Colours::white.withAlpha (0.08f));
            g.fillRoundedRectangle (status, 18.0f);

            g.setColour (juce::Colours::white.withAlpha (0.88f));
            g.setFont (juce::FontOptions (18.0f));
            g.drawFittedText ("Current desktop checkpoints: single top-level peer works; multi-peer/window layering is still being validated on QNX Screen.", status.toNearestInt().reduced (18, 16), juce::Justification::topLeft, 3);
        }

        void resized() override
        {
            auto bounds = getLocalBounds().reduced (48, 40);
            auto topRow = bounds.removeFromTop (40);
            titleLabel.setBounds (topRow.removeFromLeft (bounds.getWidth() - 260));
            closeButton.setBounds (topRow.removeFromRight (110));
            topRow.removeFromRight (12);
            childWindowButton.setBounds (topRow.removeFromRight (170));

            bounds.removeFromTop (20);
            infoLabel.setBounds (bounds.removeFromTop (70));
        }

    private:
        class UtilityWindow final : public juce::DocumentWindow
        {
        public:
            explicit UtilityWindow (std::function<void()> onCloseIn)
                : juce::DocumentWindow ("Utility Window",
                                        juce::Colour::fromRGB (44, 62, 80),
                                        juce::DocumentWindow::closeButton,
                                        true),
                  onClose (std::move (onCloseIn))
            {
                setUsingNativeTitleBar (false);
                setResizable (true, false);

                auto* content = new juce::Component();
                content->setSize (320, 180);
                setContentOwned (content, true);
                centreWithSize (320, 180);
            }

            void closeButtonPressed() override
            {
                if (onClose != nullptr)
                    onClose();
            }

        private:
            std::function<void()> onClose;
        };

        void toggleUtilityWindow()
        {
            if (utilityWindow != nullptr)
            {
                utilityWindow->setVisible (false);
                utilityWindow.reset();
                return;
            }

            utilityWindow = std::make_unique<UtilityWindow> ([this]
            {
                utilityWindow->setVisible (false);
                utilityWindow.reset();
            });

            utilityWindow->setVisible (true);
            utilityWindow->toFront (true);
        }

        juce::Label titleLabel;
        juce::Label infoLabel;
        juce::TextButton closeButton;
        juce::TextButton childWindowButton;
        std::unique_ptr<UtilityWindow> utilityWindow;
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
