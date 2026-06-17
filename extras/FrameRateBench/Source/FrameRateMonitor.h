#pragma once

#include <juce_core/juce_core.h>

#include <algorithm>
#include <functional>
#include <utility>
#include <vector>

//==============================================================================
/**
    Portable frame-rate / frame-time monitor for apples-to-apples GUI
    responsiveness benchmarking across operating systems (QNX, Linux, Windows).

    Call tick() exactly once per *presented* frame. Every reportIntervalMs the
    monitor emits ONE machine-parseable line (via onReport, defaulting to the
    JUCE Logger) carrying throughput (FPS) and the frame-time distribution
    (mean/min/max/p50/p95/p99 plus a jank count). Because the same code runs on
    every platform, QNX and Ubuntu logs diff directly.

    Threading: tick()/flush() are NOT internally synchronised. Drive it from a
    single thread (the message thread / the platform present path). Keeping it
    lock-free avoids the monitor itself perturbing the measurement.

    Log line format (stable, greppable, parseable):
        FRAMEBENCH tag=SW-QNX fps=58.3 frames=58 windowMs=994.8 meanMs=17.1
                   minMs=15.9 maxMs=33.2 p50Ms=16.8 p95Ms=22.4 p99Ms=31.0 jank=2
*/
class FrameRateMonitor
{
public:
    struct Report
    {
        juce::String tag;
        juce::uint32 sequence = 0;   // increments once per emitted report; key an overlay-dirty check off this
        int    frames        = 0;
        double windowMs      = 0.0;
        double fps           = 0.0;
        double meanFrameMs   = 0.0;
        double minFrameMs    = 0.0;
        double maxFrameMs    = 0.0;
        double p50FrameMs    = 0.0;
        double p95FrameMs    = 0.0;
        double p99FrameMs    = 0.0;
        int    jankFrames    = 0;

        juce::String toLogLine() const
        {
            auto n = [] (double v) { return juce::String (v, 1); };

            return "FRAMEBENCH tag=" + tag
                 + " fps="      + n (fps)
                 + " frames="   + juce::String (frames)
                 + " windowMs=" + n (windowMs)
                 + " meanMs="   + n (meanFrameMs)
                 + " minMs="    + n (minFrameMs)
                 + " maxMs="    + n (maxFrameMs)
                 + " p50Ms="    + n (p50FrameMs)
                 + " p95Ms="    + n (p95FrameMs)
                 + " p99Ms="    + n (p99FrameMs)
                 + " jank="     + juce::String (jankFrames);
        }
    };

    /** @param tagIn            identifies this run in the logs, e.g. "SW-QNX" / "SW-UBUNTU".
        @param reportIntervalMsIn  how often to emit a report (a fixed-time window).
        @param jankThresholdMsIn   frame-times slower than this count as "jank"
                                   (default ~55 FPS, i.e. a missed 60 Hz frame).
    */
    explicit FrameRateMonitor (juce::String tagIn,
                               double reportIntervalMsIn = 1000.0,
                               double jankThresholdMsIn   = 1000.0 / 55.0)
        : tag (std::move (tagIn)),
          reportIntervalMs (reportIntervalMsIn),
          jankThresholdMs (jankThresholdMsIn)
    {
        frameTimesMs.reserve (256);
    }

    /** Called with each completed Report. If left null, reports go to the JUCE Logger. */
    std::function<void (const Report&)> onReport;

    /** The most recent completed report (useful for an on-screen overlay). */
    Report getLastReport() const { return lastReport; }

    /** Smoothed instantaneous FPS for a live overlay, independent of the report window. */
    double getInstantaneousFps() const
    {
        return smoothedFrameMs > 0.0 ? 1000.0 / smoothedFrameMs : 0.0;
    }

    void reset()
    {
        frameTimesMs.clear();
        windowAccumMs = 0.0;
        lastFrameMs   = 0.0;
        smoothedFrameMs = 0.0;
    }

    /** Record one presented frame. Emits a report once the time window fills. */
    void tick()
    {
        const auto now = juce::Time::getMillisecondCounterHiRes();

        if (lastFrameMs > 0.0)
        {
            const auto dt = juce::jmax (0.0, now - lastFrameMs);
            frameTimesMs.push_back (dt);
            windowAccumMs += dt;

            smoothedFrameMs = smoothedFrameMs > 0.0
                                ? smoothedFrameMs + (dt - smoothedFrameMs) * smoothingFactor
                                : dt;
        }

        lastFrameMs = now;

        if (windowAccumMs >= reportIntervalMs && frameTimesMs.size() >= 2)
            flush();
    }

private:
    void flush()
    {
        std::vector<double> sorted (frameTimesMs);
        std::sort (sorted.begin(), sorted.end());

        Report r;
        r.tag       = tag;
        r.sequence  = ++reportSequence;
        r.frames    = (int) sorted.size();
        r.windowMs  = windowAccumMs;
        r.fps       = windowAccumMs > 0.0 ? (1000.0 * (double) sorted.size() / windowAccumMs) : 0.0;
        r.minFrameMs = sorted.front();
        r.maxFrameMs = sorted.back();
        r.meanFrameMs = windowAccumMs / (double) sorted.size();
        r.p50FrameMs = percentile (sorted, 0.50);
        r.p95FrameMs = percentile (sorted, 0.95);
        r.p99FrameMs = percentile (sorted, 0.99);

        for (auto ms : sorted)
            if (ms > jankThresholdMs)
                ++r.jankFrames;

        lastReport = r;

        if (onReport != nullptr)
            onReport (r);
        else
            juce::Logger::writeToLog (r.toLogLine());

        frameTimesMs.clear();
        windowAccumMs = 0.0;
    }

    static double percentile (const std::vector<double>& sortedAsc, double q)
    {
        if (sortedAsc.empty())
            return 0.0;

        const auto idx = (size_t) juce::jlimit (0.0,
                                                (double) (sortedAsc.size() - 1),
                                                q * (double) (sortedAsc.size() - 1) + 0.5);
        return sortedAsc[idx];
    }

    static constexpr double smoothingFactor = 0.12;

    juce::String tag;
    double reportIntervalMs;
    double jankThresholdMs;

    std::vector<double> frameTimesMs;
    double windowAccumMs   = 0.0;
    double lastFrameMs     = 0.0;
    double smoothedFrameMs = 0.0;
    juce::uint32 reportSequence = 0;
    Report lastReport;
};
