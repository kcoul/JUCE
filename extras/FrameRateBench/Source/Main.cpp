#include <juce_gui_basics/juce_gui_basics.h>
#include <unistd.h>

#include <cstdlib>

#include "FrameRateMonitor.h"
#include "GeneratedBuildVersion.h"

//==============================================================================
// Portable GUI frame-rate benchmark.
//
// Renders a deterministic, CPU-render-bound scene and drives the platform
// present path as hard as it will go, logging FrameRateMonitor reports once a
// second. The SAME binary/workload runs on QNX, Ubuntu and Windows, so numbers
// compare apples-to-apples on identical RPi hardware.
//
// Configuration (environment variables, so runs are scriptable):
//   BENCH_TAG=<string>    log tag identifying the run     (default: "SW")
//   BENCH_SECONDS=<int>   auto-quit after N seconds, 0=run until closed (default 0)
//   BENCH_MODE=full|partial  invalidate whole window vs a small moving rect (default full)
//   BENCH_COMPLEXITY=<int>   number of draw ops per frame; sweep to find the
//                            point where each OS drops below 60 FPS  (default 1500)
//   BENCH_WIDTH / BENCH_HEIGHT  window size (default: full primary display)
//
// "full" stresses raw fill-rate/throughput. "partial" isolates the dirty-region
// optimisation: on a backend that ignores the dirty rect, partial costs the same
// as full; on one that honours it, partial collapses to near-zero render cost.
//==============================================================================

namespace
{
    int envInt (const char* name, int fallback)
    {
        const auto v = juce::SystemStats::getEnvironmentVariable (name, {});
        return v.isNotEmpty() ? v.getIntValue() : fallback;
    }

    juce::String envStr (const char* name, const juce::String& fallback)
    {
        const auto v = juce::SystemStats::getEnvironmentVariable (name, {});
        return v.isNotEmpty() ? v : fallback;
    }

    juce::Rectangle<int> getInitialDisplayArea (int fallbackW, int fallbackH)
    {
        if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        {
            const auto logical = display->logicalBounds.getSmallestIntegerContainer();

            if (! logical.isEmpty())
                return logical;

            if (! display->totalArea.isEmpty())
                return display->totalArea;
        }

        return { 0, 0, fallbackW, fallbackH };
    }

    std::unique_ptr<juce::FileLogger> createAppLogger()
    {
        auto logFile = juce::File::getSpecialLocation (juce::File::currentApplicationFile)
                           .getParentDirectory()
                           .getChildFile ("FrameRateBench.log");

        return std::make_unique<juce::FileLogger> (logFile, "JUCE Frame Rate Bench", 512 * 1024);
    }

    //==========================================================================
    class BenchContentComponent final : public juce::Component,
                                        private juce::Timer
    {
    public:
        // The on-screen FPS overlay lives top-left. In partial mode the present
        // path only blits the invalidated region, so this rect must be dirtied
        // explicitly or the counter never refreshes — see timerCallback().
        static juce::Rectangle<int> overlayBounds() { return { 12, 12, 320, 64 }; }

        BenchContentComponent()
            : monitor (envStr ("BENCH_TAG", "SW")),
              partialMode (envStr ("BENCH_MODE", "full").equalsIgnoreCase ("partial")),
              complexity (juce::jmax (1, envInt ("BENCH_COMPLEXITY", 1500))),
              autoQuitSeconds (juce::jmax (0, envInt ("BENCH_SECONDS", 0)))
        {
            setOpaque (true);

            monitor.onReport = [] (const FrameRateMonitor::Report& r)
            {
                juce::Logger::writeToLog (r.toLogLine());
            };

            startTimeMs = juce::Time::getMillisecondCounterHiRes();

            // Drive repaints as fast as JUCE allows; the platform present path is
            // the real throttle, and the monitor records what it actually sustains.
            startTimer (1);
        }

        void paint (juce::Graphics& g) override
        {
            monitor.tick();

            // Background. In partial mode JUCE clips drawing to the invalidated
            // region, so a dirty-rect-honouring backend pays almost nothing here.
            g.fillAll (juce::Colour::fromRGB (18, 22, 28));

            const auto w = (float) getWidth();
            const auto h = (float) getHeight();
            const auto f = (float) frameIndex;

            // Deterministic scene: position/colour derived from element index and
            // frame index only (no RNG), so every frame on every OS does identical
            // work — that is what makes the comparison fair.
            for (int i = 0; i < complexity; ++i)
            {
                const auto fi = (float) i;
                const auto px = std::fmod (fi * 97.0f + f * 1.7f, w);
                const auto py = std::fmod (fi * 57.0f + f * 1.1f, h);
                const auto sz = 6.0f + std::fmod (fi * 13.0f, 26.0f);
                const auto hue = std::fmod (fi * 0.013f + f * 0.004f, 1.0f);

                g.setColour (juce::Colour::fromHSV (hue, 0.65f, 0.9f, 0.85f));
                g.fillRoundedRectangle (px, py, sz, sz, 3.0f);
            }

            drawOverlay (g);
            ++frameIndex;
        }

    private:
        void timerCallback() override
        {
            if (autoQuitSeconds > 0)
            {
                const auto elapsed = (juce::Time::getMillisecondCounterHiRes() - startTimeMs) / 1000.0;

                if (elapsed >= (double) autoQuitSeconds)
                {
                    juce::Logger::writeToLog ("BENCH_DONE after " + juce::String (elapsed, 1) + "s, "
                                              + juce::String (frameIndex) + " frames");

                    if (auto* app = juce::JUCEApplicationBase::getInstance())
                        app->systemRequestedQuit();

                    return;
                }
            }

            if (partialMode)
            {
                // Small moving rect: the dirty-region A/B probe.
                const auto w = getWidth();
                const auto h = getHeight();
                const int  s = 80;
                const int  x = (int) std::fmod ((double) frameIndex * 7.0, (double) juce::jmax (1, w - s));
                const int  y = h / 2 - s / 2;
                repaint ({ x, y, s, s });

                // The probe rect never touches the top-left overlay, so the FPS
                // counter would otherwise stay frozen on a dirty-rect-honouring
                // backend. Dirty the overlay only when a new report lands (~1 Hz)
                // — that keeps it visible without inflating the per-frame probe.
                const auto seq = monitor.getLastReport().sequence;

                if (seq != lastOverlaySeq)
                {
                    lastOverlaySeq = seq;
                    repaint (overlayBounds());
                }
            }
            else
            {
                repaint();
            }
        }

        void drawOverlay (juce::Graphics& g) const
        {
            const auto r = monitor.getLastReport();
            const auto box = overlayBounds().toFloat();

            g.setColour (juce::Colour::fromRGBA (0, 0, 0, 180));
            g.fillRoundedRectangle (box, 8.0f);

            g.setColour (juce::Colours::white.withAlpha (0.95f));
            g.setFont (juce::FontOptions (18.0f));
            g.drawText (monitor.getLastReport().tag + "  " + juce::String (r.fps, 1) + " FPS",
                        box.reduced (12.0f, 6.0f).removeFromTop (24.0f),
                        juce::Justification::centredLeft);

            g.setFont (juce::FontOptions (12.0f));
            g.setColour (juce::Colours::white.withAlpha (0.75f));
            g.drawText ("mean " + juce::String (r.meanFrameMs, 1) + "ms  "
                          + "p95 " + juce::String (r.p95FrameMs, 1) + "ms  "
                          + "jank " + juce::String (r.jankFrames)
                          + "   [" + (partialMode ? "partial" : "full")
                          + " x" + juce::String (complexity) + "]",
                        box.reduced (12.0f, 6.0f).removeFromBottom (24.0f),
                        juce::Justification::centredLeft);
        }

        FrameRateMonitor monitor;
        const bool partialMode;
        const int  complexity;
        const int  autoQuitSeconds;
        double startTimeMs = 0.0;
        int    frameIndex  = 0;
        juce::uint32 lastOverlaySeq = 0;
    };

    //==========================================================================
    class BenchWindow final : public juce::DocumentWindow
    {
    public:
        BenchWindow()
            : juce::DocumentWindow ("JUCE Frame Rate Bench",
                                    juce::Colours::black,
                                    juce::DocumentWindow::closeButton,
                                    true)
        {
            setUsingNativeTitleBar (false);
            setResizable (true, true);
            setContentOwned (new BenchContentComponent(), true);

            const auto fallback = getInitialDisplayArea (envInt ("BENCH_WIDTH", 1280),
                                                         envInt ("BENCH_HEIGHT", 1024));
            const auto w = envInt ("BENCH_WIDTH", fallback.getWidth());
            const auto h = envInt ("BENCH_HEIGHT", fallback.getHeight());
            setBounds (fallback.withSize (w, h));
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };
}

//==============================================================================
class FrameRateBenchApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "JUCE Frame Rate Bench"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override          { return true; }

    void initialise (const juce::String&) override
    {
        logger = createAppLogger();
        juce::Logger::setCurrentLogger (logger.get());
        juce::Logger::writeToLog ("Build version: " + juce::String (JUCE_FRAME_RATE_BENCH_BUILD_VERSION));
        juce::Logger::writeToLog ("Process PID: " + juce::String ((int) getpid()));

        mainWindow = std::make_unique<BenchWindow>();
        mainWindow->setVisible (true);
        mainWindow->toFront (true);
    }

    void shutdown() override
    {
        mainWindow.reset();
        juce::Logger::setCurrentLogger (nullptr);
        logger.reset();
    }

    void systemRequestedQuit() override { quit(); }
    void anotherInstanceStarted (const juce::String&) override {}

private:
    std::unique_ptr<BenchWindow> mainWindow;
    std::unique_ptr<juce::FileLogger> logger;
};

START_JUCE_APPLICATION (FrameRateBenchApplication)
