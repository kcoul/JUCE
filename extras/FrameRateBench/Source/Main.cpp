#include <juce_gui_basics/juce_gui_basics.h>

#if JUCE_MODULE_AVAILABLE_juce_opengl
 #include <juce_opengl/juce_opengl.h>
#endif

#include <unistd.h>

#include <atomic>
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
//   BENCH_TAG=<string>    log tag identifying the run  (default: "SW" / "GL")
//   BENCH_SECONDS=<int>   auto-quit after N seconds, 0=run until closed (default 0)
//   BENCH_MODE=full|partial  invalidate whole window vs a small moving rect (default full)
//   BENCH_COMPLEXITY=<int>   number of draw ops per frame; sweep to find the
//                            point where each OS drops below 60 FPS  (default 1500)
//   BENCH_WIDTH / BENCH_HEIGHT  window size (default: full primary display)
//   BENCH_RENDERER=software|opengl   which present path to exercise (default software)
//
// "full" stresses raw fill-rate/throughput. "partial" isolates the dirty-region
// optimisation: on a backend that ignores the dirty rect, partial costs the same
// as full; on one that honours it, partial collapses to near-zero render cost.
//
// BENCH_RENDERER picks the *presentation* path, which is the axis this harness
// exists to compare:
//   software - JUCE renders into a CPU image and the platform blits/posts it.
//              On QNX that is the Screen path (see JUCE_QNX_FAST_PRESENT).
//   opengl   - an OpenGLContext with component painting + continuous repainting
//              is attached, so the component is drawn into a texture and shown
//              via a GL swap (eglSwapBuffers on QNX). This mirrors exactly what
//              SurgeXT does on QNX by default, so bench numbers transfer to it.
// The same binary and workload serve both, on QNX and on Linux, which is what
// keeps the cross-OS comparison honest.
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

    // Set by the app on its top-level component to tell the QNX peer to stop
    // creating/blitting software Screen buffers and let EGL own the window
    // surface. Same property SurgeXT sets in SurgeStandaloneAppQnx.
    constexpr const char* qnxOpenGLPresentationProperty = "juce_qnx_use_opengl_presentation";

    bool openGLRendererRequested()
    {
       #if JUCE_MODULE_AVAILABLE_juce_opengl
        return envStr ("BENCH_RENDERER", "software").trim().equalsIgnoreCase ("opengl");
       #else
        return false;
       #endif
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

        explicit BenchContentComponent (bool useOpenGLIn)
            : openGLMode (useOpenGLIn),
              monitor (envStr ("BENCH_TAG", useOpenGLIn ? "GL" : "SW")),
              // partial mode applies to BOTH renderers. Under GL it is the probe for
              // whether the backend honours JUCE's valid-region tracking and repaints
              // only the dirty part of the component into the cached texture — which
              // is what a real GUI does when one control changes, and what the
              // JUCE_QNX branch in juce_OpenGLContext.cpp defeats via validArea.clear().
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
            //
            // This timer drives BOTH renderers, including OpenGL, on purpose.
            // setContinuousRepainting(true) is NOT equivalent across platforms:
            // juce_OpenGLContext.cpp has a JUCE_QNX branch where continuous
            // repainting also re-paints the *component* every frame, while stock
            // JUCE (Linux) only re-draws the cached texture and re-paints the
            // component when something invalidates it. Relying on it therefore
            // measured different work on each OS — QNX repainted ~70x/sec while
            // Linux painted 3 times in 8 seconds and stopped. Invalidating from the
            // app keeps the workload identical on both.
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
            const auto f = (float) frameIndex.load();

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
                                              + juce::String (frameIndex.load()) + " frames");

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
                const int  x = (int) std::fmod ((double) frameIndex.load() * 7.0, (double) juce::jmax (1, w - s));
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
                          + "   [" + (openGLMode ? "opengl" : "software")
                          + " " + (partialMode ? "partial" : "full")
                          + " x" + juce::String (complexity) + "]",
                        box.reduced (12.0f, 6.0f).removeFromBottom (24.0f),
                        juce::Justification::centredLeft);
        }

        const bool openGLMode;
        FrameRateMonitor monitor;
        const bool partialMode;
        const int  complexity;
        const int  autoQuitSeconds;
        double startTimeMs = 0.0;
        // Written by whichever thread paints (the GL render thread in opengl
        // mode, the message thread otherwise) and read by the auto-quit timer.
        std::atomic<int> frameIndex { 0 };
        juce::uint32 lastOverlaySeq = 0;
    };

    //==========================================================================
    class BenchWindow final : public juce::DocumentWindow
                             #if JUCE_MODULE_AVAILABLE_juce_opengl
                              , private juce::OpenGLRenderer
                              , private juce::Timer
                             #endif
    {
    public:
        explicit BenchWindow (bool useOpenGLIn)
            : juce::DocumentWindow ("JUCE Frame Rate Bench",
                                    juce::Colours::black,
                                    juce::DocumentWindow::closeButton,
                                    true),
              useOpenGL (useOpenGLIn)
        {
            // Renderer in the title bar: which mode a window is running is then
            // obvious on-screen, not just in the log.
            setName ("JUCE Frame Rate Bench - " + juce::String (useOpenGLIn ? "opengl" : "software"));
            setUsingNativeTitleBar (false);
            setResizable (true, true);
            setContentOwned (new BenchContentComponent (useOpenGLIn), true);

            const auto fallback = getInitialDisplayArea (envInt ("BENCH_WIDTH", 1280),
                                                         envInt ("BENCH_HEIGHT", 1024));
            const auto w = envInt ("BENCH_WIDTH", fallback.getWidth());
            const auto h = envInt ("BENCH_HEIGHT", fallback.getHeight());
            setBounds (fallback.withSize (w, h));

           #if JUCE_MODULE_AVAILABLE_juce_opengl
            if (useOpenGL)
            {
                // Tell the QNX peer up front, so it never allocates software
                // Screen buffers for a window EGL is about to take over.
                getProperties().set (qnxOpenGLPresentationProperty, true);
                startTimer (16);
            }
           #endif
        }

       #if JUCE_MODULE_AVAILABLE_juce_opengl
        ~BenchWindow() override
        {
            if (useOpenGL)
            {
                stopTimer();
                openGLContext.detach();
                openGLContext.setRenderer (nullptr);
            }
        }

        void visibilityChanged() override        { DocumentWindow::visibilityChanged();       attachOpenGLIfReady(); }
        void parentHierarchyChanged() override   { DocumentWindow::parentHierarchyChanged();  attachOpenGLIfReady(); }
       #endif

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
       #if JUCE_MODULE_AVAILABLE_juce_opengl
        void timerCallback() override
        {
            if (glActive.load())
            {
                stopTimer();
                return;
            }

            attachOpenGLIfReady();

            // If the context never comes up, fall back rather than sit at zero
            // FPS in front of a blank window — and say so in the log, because a
            // silent fallback would corrupt the comparison.
            if (attachAttempted
                && juce::Time::getMillisecondCounterHiRes() - attachStartMs > 1500.0)
            {
                juce::Logger::writeToLog ("BENCH_WARN OpenGL context was not created within 1500ms; "
                                          "falling back to the software present path");
                openGLContext.detach();
                openGLContext.setRenderer (nullptr);
                getProperties().set (qnxOpenGLPresentationProperty, false);
                stopTimer();

                if (auto* content = getContentComponent())
                    content->repaint();
            }
        }

        void attachOpenGLIfReady()
        {
            if (! useOpenGL || attachAttempted || glActive.load())
                return;

            if (getPeer() == nullptr || ! isShowing() || getWidth() <= 0 || getHeight() <= 0)
                return;

            attachAttempted = true;
            attachStartMs = juce::Time::getMillisecondCounterHiRes();

            openGLContext.setRenderer (this);
            // Component painting routes the bench's paint() through the GL
            // context; continuous repainting makes it swap every vsync, which is
            // exactly the SurgeXT-on-QNX configuration.
            openGLContext.setComponentPaintingEnabled (true);
            openGLContext.setContinuousRepainting (true);
            openGLContext.attachTo (*this);

            juce::Logger::writeToLog ("BENCH_NOTE requested OpenGL context attachment");
        }

        void newOpenGLContextCreated() override
        {
            glActive = true;

            // Called on the render thread, which is where the swap interval can be
            // set. JUCE defaults it to 1 (sync to vblank); we default to 0 so the
            // bench measures uncapped throughput rather than the panel's refresh
            // rate — and, more importantly, so both platforms are explicitly on the
            // same setting instead of inheriting different per-platform defaults.
            // On X11 the vsync default also stalls the render thread outright once
            // the swap chain fills (a fixed 3 frames, then nothing).
            const auto interval = envInt ("BENCH_SWAP_INTERVAL", 0);
            const auto applied = openGLContext.setSwapInterval (interval);

            juce::Logger::writeToLog ("BENCH_NOTE OpenGL context created, swapInterval="
                                      + juce::String (interval)
                                      + (applied ? "" : " (NOT SUPPORTED - platform default in use)"));
        }

        void renderOpenGL() override
        {
            // Diagnostic: the render thread ticking is NOT the same as the component
            // being repainted. If this count climbs while FRAMEBENCH stays flat, the
            // GL thread is alive but JUCE is not re-painting the component into it.
            const auto n = ++glRenderCount;

            if (n <= 3 || (n % 120) == 0)
                juce::Logger::writeToLog ("BENCH_NOTE renderOpenGL #" + juce::String (n));
        }

        void openGLContextClosing() override
        {
            glActive = false;
            juce::Logger::writeToLog ("BENCH_NOTE OpenGL context closing");
        }

        juce::OpenGLContext openGLContext;
        std::atomic<bool> glActive { false };
        std::atomic<int> glRenderCount { 0 };
        bool attachAttempted = false;
        double attachStartMs = 0.0;
       #endif

        const bool useOpenGL;
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

        const auto useOpenGL = openGLRendererRequested();

       #if ! JUCE_MODULE_AVAILABLE_juce_opengl
        if (envStr ("BENCH_RENDERER", "software").equalsIgnoreCase ("opengl"))
            juce::Logger::writeToLog ("BENCH_WARN BENCH_RENDERER=opengl ignored: this binary was built "
                                      "without juce_opengl");
       #endif

        // One machine-parseable line describing the run, so a log can always be
        // attributed to a configuration without trusting the shell history.
        juce::Logger::writeToLog ("BENCH_CONFIG renderer=" + juce::String (useOpenGL ? "opengl" : "software")
                                  + " mode=" + envStr ("BENCH_MODE", "full")
                                  + " complexity=" + juce::String (juce::jmax (1, envInt ("BENCH_COMPLEXITY", 1500)))
                                  + " seconds=" + juce::String (juce::jmax (0, envInt ("BENCH_SECONDS", 0)))
                                  + " os=" + juce::SystemStats::getOperatingSystemName()
                                  + " cpus=" + juce::String (juce::SystemStats::getNumCpus()));

        mainWindow = std::make_unique<BenchWindow> (useOpenGL);
        mainWindow->setVisible (true);
        mainWindow->toFront (true);

        // Logged after the window exists, so it reports the size actually
        // obtained rather than the size requested. A cross-OS comparison is only
        // valid at equal pixel counts, so this has to be in the log to be checked.
        const auto b = mainWindow->getBounds();
        juce::Logger::writeToLog ("BENCH_WINDOW width=" + juce::String (b.getWidth())
                                  + " height=" + juce::String (b.getHeight())
                                  + " pixels=" + juce::String (b.getWidth() * b.getHeight()));
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
