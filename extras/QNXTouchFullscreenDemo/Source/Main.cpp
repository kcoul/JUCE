#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_opengl/juce_opengl.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_osc/juce_osc.h>
#include <unistd.h>

#include <atomic>
#include <cstdlib>
#include <cmath>
#include <map>
#include <set>

#include "GeneratedBuildVersion.h"

namespace
{
    constexpr int oscListenPort = 9001;
    constexpr int gridColumns = 8;
    constexpr int gridRows = 5;
    constexpr int keyboardVoiceId = -1;
    constexpr int oscVoiceBase = 1000;
    constexpr float maxVoiceAmplitude = 0.16f;
    constexpr int fallbackWidth = 1280;
    constexpr int fallbackHeight = 1024;
    constexpr double voiceRampTimeSeconds = 0.008;

    bool shouldEnableOpenGLRenderer()
    {
        return juce::SystemStats::getEnvironmentVariable ("JUCE_QNX_ENABLE_OPENGL", "1") != "0";
    }

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
                           .getChildFile ("JUCEQNXTouchFullscreenDemo.log");

        return std::make_unique<juce::FileLogger> (logFile,
                                                   "JUCE QNX Touch Fullscreen Demo",
                                                   512 * 1024);
    }

    juce::Colour colourForSourceId (int sourceId)
    {
        const auto hue = std::fmod (0.13f * (float) juce::jmax (1, sourceId + 3), 1.0f);
        return juce::Colour::fromHSV (hue, 0.72f, 0.94f, 1.0f);
    }

    class FpsCounter
    {
    public:
        void frameRendered()
        {
            const juce::ScopedLock lock (stateLock);
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
            const juce::ScopedLock lock (stateLock);

            if (frameCount < 2 || averageFrameMs <= 0.0)
                return rendererTag + " FPS --";

            return rendererTag + " FPS " + juce::String (1000.0 / averageFrameMs, 1);
        }

        double getFramesPerSecond() const
        {
            const juce::ScopedLock lock (stateLock);

            if (frameCount < 2 || averageFrameMs <= 0.0)
                return 0.0;

            return 1000.0 / averageFrameMs;
        }

    private:
        static constexpr double smoothingFactor = 0.12;

        mutable juce::CriticalSection stateLock;
        double lastFrameMs = 0.0;
        double averageFrameMs = 0.0;
        int frameCount = 0;
    };

    struct VoiceState
    {
        double frequency = 440.0;
        float currentAmplitude = 0.0f;
        float targetAmplitude = 0.0f;
        double phase = 0.0;
    };

    class TouchSynthAudioSource final : public juce::AudioSource
    {
    public:
        void prepareToPlay (int, double newSampleRate) override
        {
            const juce::ScopedLock lock (voiceLock);
            sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
            rampSamples = juce::jmax (1, (int) std::round (sampleRate * voiceRampTimeSeconds));
        }

        void releaseResources() override {}

        void getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill) override
        {
            bufferToFill.clearActiveBufferRegion();

            const juce::ScopedLock lock (voiceLock);

            if (voices.empty())
                return;

            const auto numChannels = bufferToFill.buffer->getNumChannels();
            const auto startSample = bufferToFill.startSample;
            const auto numSamples = bufferToFill.numSamples;
            const auto twoPi = juce::MathConstants<double>::twoPi;

            for (auto& [sourceId, voice] : voices)
            {
                juce::ignoreUnused (sourceId);

                const auto phaseDelta = twoPi * voice.frequency / sampleRate;
                auto phase = voice.phase;
                auto amplitude = voice.currentAmplitude;
                const auto targetAmplitude = voice.targetAmplitude;

                for (int sample = 0; sample < numSamples; ++sample)
                {
                    if (std::abs (targetAmplitude - amplitude) > 0.00001f)
                    {
                        const auto delta = (targetAmplitude - amplitude) / (float) rampSamples;
                        amplitude += delta;

                        if ((delta > 0.0f && amplitude > targetAmplitude)
                            || (delta < 0.0f && amplitude < targetAmplitude))
                        {
                            amplitude = targetAmplitude;
                        }
                    }

                    const auto value = std::sin (phase) * (double) amplitude;
                    phase += phaseDelta;

                    if (phase >= twoPi)
                        phase -= twoPi;

                    for (int channel = 0; channel < numChannels; ++channel)
                        bufferToFill.buffer->addSample (channel, startSample + sample, (float) value);
                }

                voice.phase = phase;
                voice.currentAmplitude = amplitude;
            }

            for (auto it = voices.begin(); it != voices.end();)
            {
                if (it->second.targetAmplitude <= 0.0001f && it->second.currentAmplitude <= 0.0001f)
                    it = voices.erase (it);
                else
                    ++it;
            }
        }

        void setVoice (int sourceId, double frequency, float amplitude)
        {
            const juce::ScopedLock lock (voiceLock);
            auto& voice = voices[sourceId];
            voice.frequency = frequency;
            voice.targetAmplitude = amplitude;
        }

        void removeVoice (int sourceId)
        {
            const juce::ScopedLock lock (voiceLock);
            if (auto it = voices.find (sourceId); it != voices.end())
                it->second.targetAmplitude = 0.0f;
        }

        void clearAllVoices()
        {
            const juce::ScopedLock lock (voiceLock);
            voices.clear();
        }

    private:
        juce::CriticalSection voiceLock;
        std::map<int, VoiceState> voices;
        double sampleRate = 44100.0;
        int rampSamples = 1;
    };
}

class MainComponent final : public juce::Component,
                            private juce::OSCReceiver,
                            private juce::OSCReceiver::Listener<juce::OSCReceiver::MessageLoopCallback>
{
public:
    MainComponent (FpsCounter& counterIn, std::atomic<bool>& isOpenGLActiveIn)
        : fpsCounter (counterIn),
          isOpenGLActive (isOpenGLActiveIn)
    {
        setWantsKeyboardFocus (true);
        setMouseClickGrabsKeyboardFocus (true);
        setOpaque (true);

        player.setSource (&synthSource);

        const auto error = deviceManager.initialise (0, 2, nullptr, true, "*USB*");

        if (error.isEmpty())
        {
            audioReady = true;
            juce::Logger::writeToLog ("Audio device manager initialised successfully");
            logCurrentAudioDevice ("After initialise");
            preferUsbAudioOutput();
            deviceManager.addAudioCallback (&player);
        }
        else
        {
            lastError = error;
            juce::Logger::writeToLog ("Audio device manager initialisation failed: " + error);
        }

        const auto displayArea = getInitialDisplayArea();
        setSize (displayArea.getWidth(), displayArea.getHeight());
        juce::Logger::writeToLog ("MainComponent created with size "
                                  + juce::String (getWidth())
                                  + "x"
                                  + juce::String (getHeight()));

        initialiseOscControl();
        updateStatusText ("Ready");
        grabKeyboardFocus();
    }

    ~MainComponent() override
    {
        juce::Logger::writeToLog ("MainComponent shutting down");
        removeListener (this);
        disconnect();
        synthSource.clearAllVoices();
        deviceManager.removeAudioCallback (&player);
        player.setSource (nullptr);
        deviceManager.closeAudioDevice();
    }

    void paint (juce::Graphics& g) override
    {
        if (! isOpenGLActive.load())
            fpsCounter.frameRendered();

        g.fillAll (juce::Colour::fromRGB (242, 236, 225));

        auto panel = getLocalBounds().toFloat().reduced (24.0f);
        g.setColour (juce::Colour::fromRGB (28, 50, 66));
        g.fillRoundedRectangle (panel, 26.0f);

        const auto grid = getGridBounds().toFloat();
        const auto status = getStatusBounds().toFloat();

        g.setColour (juce::Colour::fromRGBA (255, 255, 255, 28));
        g.fillRoundedRectangle (grid, 24.0f);

        const auto cellWidth = grid.getWidth() / (float) gridColumns;
        const auto cellHeight = grid.getHeight() / (float) gridRows;

        for (int row = 0; row < gridRows; ++row)
        {
            for (int column = 0; column < gridColumns; ++column)
            {
                auto cell = juce::Rectangle<float> (grid.getX() + (float) column * cellWidth,
                                                    grid.getY() + (float) row * cellHeight,
                                                    cellWidth,
                                                    cellHeight).reduced (3.0f);

                const auto note = noteForCell (column, row);
                const auto colour = ((row + column) % 2 == 0) ? juce::Colour::fromRGBA (255, 255, 255, 24)
                                                               : juce::Colour::fromRGBA (233, 196, 106, 28);

                g.setColour (colour);
                g.fillRoundedRectangle (cell, 14.0f);

                g.setColour (juce::Colour::fromRGBA (255, 255, 255, 46));
                g.drawRoundedRectangle (cell, 14.0f, 1.0f);

                g.setColour (juce::Colour::fromRGBA (255, 255, 255, 160));
                g.setFont (juce::FontOptions (15.0f));
                g.drawText ("N" + juce::String (note),
                            cell.removeFromTop (24.0f),
                            juce::Justification::centred);
            }
        }

        auto closeBounds = getCloseButtonBounds().toFloat();
        g.setColour (juce::Colour::fromRGBA (255, 255, 255, 24));
        g.fillRoundedRectangle (closeBounds, 12.0f);
        g.setColour (juce::Colour::fromRGBA (255, 255, 255, 90));
        g.drawRoundedRectangle (closeBounds, 12.0f, 1.0f);
        g.setColour (juce::Colour::fromRGBA (255, 255, 255, 225));
        g.setFont (juce::FontOptions (17.0f));
        g.drawText ("Close",
                    closeBounds,
                    juce::Justification::centred);

        for (const auto& [sourceId, touch] : activePointers)
        {
            const auto point = touch.position;
            const auto markerBounds = juce::Rectangle<float> (point.x - 26.0f, point.y - 26.0f, 52.0f, 52.0f);

            g.setColour (touch.colour.withAlpha (0.24f));
            g.fillEllipse (markerBounds.expanded (10.0f));
            g.setColour (touch.colour);
            g.fillEllipse (markerBounds);

                if (touch.isTouch)
                {
                    g.setColour (juce::Colours::black.withAlpha (0.78f));
                    g.setFont (juce::FontOptions (17.0f));
                    g.drawFittedText (juce::String (sourceId - 1),
                                      markerBounds.toNearestInt().reduced (10, 10),
                                      juce::Justification::centred,
                                      1);
                }
        }

        g.setColour (juce::Colour::fromRGBA (255, 255, 255, 42));
        g.fillRoundedRectangle (status, 20.0f);

        g.setColour (juce::Colour::fromRGBA (255, 255, 255, 225));
        g.setFont (juce::FontOptions (20.0f));
        g.drawText (statusText,
                    status.reduced (18.0f, 12.0f),
                    juce::Justification::centredLeft,
                    true);

        g.setFont (juce::FontOptions (16.0f));
        g.drawText ("OSC: /juce/noteOn <note:int> <velocity:float?>    /juce/noteOff <note:int?>",
                    status.getX() + 18.0f,
                    status.getBottom() - 30.0f,
                    status.getWidth() - 36.0f,
                    20.0f,
                    juce::Justification::centredLeft);

        if (! audioReady && lastError.isNotEmpty())
        {
            g.setColour (juce::Colour::fromRGB (231, 111, 81));
            g.setFont (juce::FontOptions (18.0f));
            g.drawText ("Audio init failed: " + lastError,
                        panel.getX() + 24.0f,
                        panel.getBottom() - 120.0f,
                        panel.getWidth() - 48.0f,
                        24.0f,
                        juce::Justification::left);
        }

        drawFpsOverlay (g);
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        if (getCloseButtonBounds().contains (event.getPosition()))
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
            return;
        }

        handlePointerPressOrDrag (event, true);
    }

    void mouseDrag (const juce::MouseEvent& event) override
    {
        handlePointerPressOrDrag (event, false);
    }

    void mouseUp (const juce::MouseEvent& event) override
    {
        releasePointer (event);
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        juce::Logger::writeToLog ("MainComponent::keyPressed keyCode=" + juce::String (key.getKeyCode()));

        if (key.getKeyCode() == juce::KeyPress::escapeKey)
        {
            juce::Logger::writeToLog ("Escape pressed, requesting quit");
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
            return true;
        }

        if (key.getKeyCode() == juce::KeyPress::spaceKey || key.getKeyCode() == juce::KeyPress::returnKey)
        {
            keyboardHeld = ! keyboardHeld;

            if (keyboardHeld)
            {
                const auto frequency = juce::MidiMessage::getMidiNoteInHertz (69);
                synthSource.setVoice (keyboardVoiceId, frequency, maxVoiceAmplitude * 0.85f);
                updateStatusText ("Keyboard reference note on");
            }
            else
            {
                synthSource.removeVoice (keyboardVoiceId);
                updateStatusText ("Keyboard reference note off");
            }

            requestVisualRefresh();
            return true;
        }

        return false;
    }

    void visibilityChanged() override
    {
    }

    void parentHierarchyChanged() override
    {
    }

    void resized() override
    {
    }

private:
    struct ActivePointer
    {
        juce::Point<float> position;
        int note = 60;
        int column = 0;
        int row = 0;
        float amplitude = 0.0f;
        juce::Colour colour;
        bool isTouch = false;
    };

    juce::Rectangle<int> getGridBounds() const
    {
        auto bounds = getLocalBounds().reduced (48, 44);
        bounds.removeFromTop (56);
        bounds.removeFromBottom (110);
        return bounds;
    }

    juce::Rectangle<int> getStatusBounds() const
    {
        auto bounds = getLocalBounds().reduced (48, 44);
        return bounds.removeFromBottom (86);
    }

    juce::Rectangle<int> getCloseButtonBounds() const
    {
        auto bounds = getLocalBounds().reduced (48, 44);
        return { bounds.getRight() - 108, bounds.getY() + 10, 96, 34 };
    }

    juce::Rectangle<int> getFpsOverlayBounds() const
    {
        auto bounds = getLocalBounds().reduced (48, 44);
        return { bounds.getX() + 12, bounds.getY() + 10, 104, 40 };
    }

    juce::Rectangle<int> getTouchCountOverlayBounds() const
    {
        auto bounds = getLocalBounds().reduced (48, 44);
        return { bounds.getX() + 124, bounds.getY() + 10, 104, 40 };
    }

    void drawDiagnosticCard (juce::Graphics& g,
                             juce::Rectangle<int> bounds,
                             const juce::String& title,
                             const juce::String& value,
                             juce::Colour accent) const
    {
        const auto overlay = bounds.toFloat();

        g.setColour (juce::Colour::fromRGBA (9, 18, 28, 170));
        g.fillRoundedRectangle (overlay, 11.0f);
        g.setColour (accent.withAlpha (0.70f));
        g.drawRoundedRectangle (overlay, 11.0f, 1.0f);
        g.setColour (accent.withAlpha (0.16f));
        g.fillRoundedRectangle (overlay.reduced (6.0f), 8.0f);

        auto textBounds = overlay.reduced (12.0f, 7.0f);
        auto titleBounds = textBounds.removeFromTop (13.0f);

        g.setColour (accent.withAlpha (0.88f));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (title, titleBounds, juce::Justification::centredLeft);

        g.setColour (juce::Colours::white.withAlpha (0.95f));
        g.setFont (juce::FontOptions (17.0f));
        g.drawText (value, textBounds, juce::Justification::centredLeft);
    }

    void drawFpsOverlay (juce::Graphics& g) const
    {
        drawDiagnosticCard (g,
                            getFpsOverlayBounds(),
                            "Renderer",
                            fpsCounter.getSummaryText (isOpenGLActive.load() ? "GL" : "SW"),
                            juce::Colour::fromRGB (239, 196, 76));

        drawDiagnosticCard (g,
                            getTouchCountOverlayBounds(),
                            "Touches",
                            juce::String (countActiveTouches()),
                            juce::Colour::fromRGB (118, 200, 255));
    }

    static int noteForCell (int column, int row)
    {
        return 36 + column + ((gridRows - 1 - row) * gridColumns);
    }

    int sourceIdForEvent (const juce::MouseEvent& event) const
    {
        return event.source.isTouch() ? event.source.getIndex() + 1 : 0;
    }

    float amplitudeForPosition (juce::Point<float> position) const
    {
        const auto grid = getGridBounds().toFloat();
        const auto normalisedY = juce::jlimit (0.0f, 1.0f, (position.y - grid.getY()) / juce::jmax (1.0f, grid.getHeight()));
        return juce::jmap (1.0f - normalisedY, 0.20f, 1.0f) * maxVoiceAmplitude;
    }

    bool getCellForPosition (juce::Point<float> position, int& column, int& row) const
    {
        const auto grid = getGridBounds().toFloat();

        if (! grid.contains (position))
            return false;

        column = juce::jlimit (0, gridColumns - 1, (int) ((position.x - grid.getX()) / juce::jmax (1.0f, grid.getWidth()) * (float) gridColumns));
        row = juce::jlimit (0, gridRows - 1, (int) ((position.y - grid.getY()) / juce::jmax (1.0f, grid.getHeight()) * (float) gridRows));
        return true;
    }

    int countActiveTouches() const
    {
        int count = 0;

        for (const auto& [sourceId, pointer] : activePointers)
        {
            juce::ignoreUnused (sourceId);

            if (pointer.isTouch)
                ++count;
        }

        return count;
    }

    juce::String getActiveTouchIdList() const
    {
        juce::StringArray ids;

        for (const auto& [sourceId, pointer] : activePointers)
            if (pointer.isTouch)
                ids.add (juce::String (sourceId - 1));

        return ids.isEmpty() ? juce::String ("none") : ids.joinIntoString (", ");
    }

    void updateStatusText (const juce::String& lastEvent)
    {
        statusText = "Touch points: "
                   + juce::String (countActiveTouches())
                   + " | Active touch IDs: "
                   + getActiveTouchIdList()
                   + " | Last event: "
                   + lastEvent;
    }

    void handlePointerPressOrDrag (const juce::MouseEvent& event, bool isPress)
    {
        if (! audioReady)
            return;

        const auto position = event.position;
        int column = 0;
        int row = 0;

        if (! getCellForPosition (position, column, row))
            return;

        const auto sourceId = sourceIdForEvent (event);
        const auto note = noteForCell (column, row);
        const auto frequency = juce::MidiMessage::getMidiNoteInHertz (note);
        const auto amplitude = amplitudeForPosition (position);
        const auto existingPointer = activePointers.find (sourceId);
        const bool shouldLogMove = isPress
                                || existingPointer == activePointers.end()
                                || existingPointer->second.note != note
                                || existingPointer->second.column != column
                                || existingPointer->second.row != row;
        const bool shouldUpdateStatus = isPress || shouldLogMove;
        auto& pointer = activePointers[sourceId];

        pointer.position = position;
        pointer.note = note;
        pointer.column = column;
        pointer.row = row;
        pointer.amplitude = amplitude;
        pointer.colour = colourForSourceId (sourceId);
        pointer.isTouch = event.source.isTouch();

        synthSource.setVoice (sourceId, frequency, amplitude);

        const auto sourceLabel = pointer.isTouch ? "touch#" + juce::String (sourceId - 1)
                                                 : "mouse";

        if (shouldUpdateStatus)
        {
            updateStatusText (sourceLabel
                              + (isPress ? " down " : " move ")
                              + "cell "
                              + juce::String (column)
                              + ","
                              + juce::String (row)
                              + " note "
                              + juce::String (note));
        }

        if (isPress)
        {
            juce::Logger::writeToLog ("Grid " + sourceLabel
                                      + " note=" + juce::String (note)
                                      + " amplitude=" + juce::String (amplitude, 3)
                                      + " position=" + juce::String ((int) position.x)
                                      + "," + juce::String ((int) position.y));
        }

        requestVisualRefresh();
    }

    void releasePointer (const juce::MouseEvent& event)
    {
        const auto sourceId = sourceIdForEvent (event);
        const auto wasTouch = event.source.isTouch();
        synthSource.removeVoice (sourceId);
        activePointers.erase (sourceId);

        updateStatusText ((wasTouch ? "touch#" + juce::String (sourceId - 1) : "mouse") + " up");
        requestVisualRefresh();
    }

    void logCurrentAudioDevice (const juce::String& context) const
    {
        if (auto* currentDevice = deviceManager.getCurrentAudioDevice())
            juce::Logger::writeToLog (context + ": current audio device '" + currentDevice->getName() + "' type=" + currentDevice->getTypeName());
        else
            juce::Logger::writeToLog (context + ": no current audio device");
    }

    void initialiseOscControl()
    {
        addListener (this);
        registerFormatErrorHandler ([] (const char*, int dataSize)
        {
            juce::Logger::writeToLog ("OSC format error while parsing packet of size " + juce::String (dataSize));
        });

        if (connect (oscListenPort))
        {
            juce::Logger::writeToLog ("OSC receiver listening on UDP port " + juce::String (oscListenPort));
            juce::Logger::writeToLog ("OSC schema: /juce/noteOn <note:int> <velocity:float?>, /juce/noteOff <note:int?>");
        }
        else
        {
            juce::Logger::writeToLog ("Failed to start OSC receiver on UDP port " + juce::String (oscListenPort));
        }
    }

    void oscMessageReceived (const juce::OSCMessage& message) override
    {
        const auto address = message.getAddressPattern().toString();
        juce::Logger::writeToLog ("OSC message received: " + address + " args=" + juce::String (message.size()));

        if (address == "/juce/noteOn")
        {
            handleOscNoteOn (message);
            return;
        }

        if (address == "/juce/noteOff")
        {
            handleOscNoteOff (message);
            return;
        }

        juce::Logger::writeToLog ("Unhandled OSC address: " + address);
    }

    void handleOscNoteOn (const juce::OSCMessage& message)
    {
        const auto noteNumber = parseOscNoteNumber (message);

        if (! juce::isPositiveAndBelow (noteNumber, 128))
        {
            juce::Logger::writeToLog ("OSC noteOn ignored: expected MIDI note 0-127 in argument 0");
            return;
        }

        const auto velocity = parseOscVelocity (message);
        const auto amplitude = juce::jlimit (0.0f, maxVoiceAmplitude, velocity * maxVoiceAmplitude);
        const auto frequency = juce::MidiMessage::getMidiNoteInHertz (noteNumber);
        const auto voiceId = oscVoiceBase + noteNumber;

        synthSource.setVoice (voiceId, frequency, amplitude);
        activeOscNotes.insert (noteNumber);

        juce::Logger::writeToLog ("OSC noteOn note=" + juce::String (noteNumber)
                                  + " frequency=" + juce::String (frequency, 2)
                                  + " velocity=" + juce::String (velocity, 2));
        updateStatusText ("OSC noteOn " + juce::String (noteNumber));
        requestVisualRefresh();
    }

    void handleOscNoteOff (const juce::OSCMessage& message)
    {
        const auto noteNumber = parseOscNoteNumber (message);

        if (juce::isPositiveAndBelow (noteNumber, 128))
        {
            synthSource.removeVoice (oscVoiceBase + noteNumber);
            activeOscNotes.erase (noteNumber);
            juce::Logger::writeToLog ("OSC noteOff note=" + juce::String (noteNumber));
            updateStatusText ("OSC noteOff " + juce::String (noteNumber));
        }
        else
        {
            for (auto note : activeOscNotes)
                synthSource.removeVoice (oscVoiceBase + note);

            activeOscNotes.clear();
            juce::Logger::writeToLog ("OSC noteOff all");
            updateStatusText ("OSC noteOff all");
        }

        requestVisualRefresh();
    }

    static int parseOscNoteNumber (const juce::OSCMessage& message)
    {
        if (message.size() == 0)
            return -1;

        const auto& argument = message[0];

        if (argument.isInt32())
            return argument.getInt32();

        if (argument.isFloat32())
            return juce::roundToInt (argument.getFloat32());

        return -1;
    }

    static float parseOscVelocity (const juce::OSCMessage& message)
    {
        if (message.size() < 2)
            return 1.0f;

        const auto& argument = message[1];

        if (argument.isFloat32())
            return juce::jlimit (0.0f, 1.0f, argument.getFloat32());

        if (argument.isInt32())
            return juce::jlimit (0.0f, 1.0f, (float) argument.getInt32() / 127.0f);

        return 1.0f;
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

    void requestVisualRefresh()
    {
        if (auto* content = getContentComponent())
            content->repaint();

        repaint();
    }

    juce::AudioDeviceManager deviceManager;
    juce::AudioSourcePlayer player;
    TouchSynthAudioSource synthSource;
    std::map<int, ActivePointer> activePointers;
    std::set<int> activeOscNotes;
    juce::String lastError;
    juce::String statusText;
    FpsCounter& fpsCounter;
    std::atomic<bool>& isOpenGLActive;
    bool audioReady = false;
    bool keyboardHeld = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

class MainWindow final : public juce::DocumentWindow,
                         private juce::OpenGLRenderer,
                         private juce::Timer
{
public:
    MainWindow()
        : juce::DocumentWindow ("JUCE QNX Touch Fullscreen Demo",
                                juce::Colours::black,
                                juce::DocumentWindow::allButtons,
                                true)
    {
        setUsingNativeTitleBar (false);
        setResizable (false, false);
        setTitleBarHeight (0);
        setContentOwned (new MainComponent (fpsCounter, isOpenGLActive), true);
        setBounds (getInitialDisplayArea());

        if (shouldEnableOpenGLRenderer())
        {
            openGLRequested = true;
            startTimerHz (30);
            tryAttachOpenGLIfReady();
        }
        else
        {
            juce::Logger::writeToLog ("OpenGL renderer disabled; set JUCE_QNX_ENABLE_OPENGL=0 to force software rendering");
        }
    }

    ~MainWindow() override
    {
        shutdownOpenGL();
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

    void resized() override
    {
        DocumentWindow::resized();
        tryAttachOpenGLIfReady();
    }

    void visibilityChanged() override
    {
        DocumentWindow::visibilityChanged();
        tryAttachOpenGLIfReady();
    }

    void parentHierarchyChanged() override
    {
        DocumentWindow::parentHierarchyChanged();
        tryAttachOpenGLIfReady();
    }

private:
    void newOpenGLContextCreated() override
    {
        isOpenGLActive = true;
        juce::Logger::writeToLog ("OpenGL context created for touch demo");
        requestVisualRefresh();
    }

    void renderOpenGL() override
    {
        fpsCounter.frameRendered();
    }

    void openGLContextClosing() override
    {
        isOpenGLActive = false;
        juce::Logger::writeToLog ("OpenGL context closing for touch demo");
    }

    void timerCallback() override
    {
        if (isOpenGLActive.load())
        {
            requestVisualRefresh();
            return;
        }

        tryAttachOpenGLIfReady();

        if (openGLAttachAttempted && ! isOpenGLActive.load())
        {
            const auto elapsedMs = juce::Time::getMillisecondCounterHiRes() - openGLAttachStartMs;

            if (elapsedMs > 1500.0)
            {
                juce::Logger::writeToLog ("OpenGL context was not created within 1500ms; reverting to software renderer");
                shutdownOpenGL();
                requestVisualRefresh();
                stopTimer();
            }
        }
    }

    void tryAttachOpenGLIfReady()
    {
        if (! openGLRequested || openGLAttachAttempted || isOpenGLActive.load())
            return;

        if (getPeer() == nullptr || ! isShowing() || getWidth() <= 0 || getHeight() <= 0)
            return;

        openGLAttachAttempted = true;
        openGLAttachStartMs = juce::Time::getMillisecondCounterHiRes();
        openGLContext.setRenderer (this);
        openGLContext.setComponentPaintingEnabled (true);
        openGLContext.setContinuousRepainting (true);
        openGLContext.attachTo (*this);
        juce::Logger::writeToLog ("Requested OpenGL context attachment");
    }

    void shutdownOpenGL()
    {
        openGLContext.detach();
        openGLContext.setRenderer (nullptr);
        isOpenGLActive = false;
        openGLAttachAttempted = false;
        openGLRequested = false;
    }

    void requestVisualRefresh()
    {
        repaint();
    }

    FpsCounter fpsCounter;
    juce::OpenGLContext openGLContext;
    std::atomic<bool> isOpenGLActive { false };
    bool openGLRequested = false;
    bool openGLAttachAttempted = false;
    double openGLAttachStartMs = 0.0;
};

class QnxTouchFullscreenApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override       { return "JUCE QNX Touch Fullscreen Demo"; }
    const juce::String getApplicationVersion() override    { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override             { return true; }

    void initialise (const juce::String&) override
    {
        setenv ("JUCE_QNX_EMBEDDED_FULLSCREEN", "1", 1);
        logger = createAppLogger();
        juce::Logger::setCurrentLogger (logger.get());
        juce::Logger::writeToLog ("Build version: " + juce::String (JUCE_QNX_TOUCH_FULLSCREEN_DEMO_BUILD_VERSION));
        juce::Logger::writeToLog ("Process PID: " + juce::String ((int) getpid()));
        juce::Logger::writeToLog ("Application initialise()");
        mainWindow = std::make_unique<MainWindow>();
        mainWindow->setVisible (true);
        mainWindow->toFront (true);
        juce::Logger::writeToLog ("Main window added to desktop");
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

START_JUCE_APPLICATION (QnxTouchFullscreenApplication)
