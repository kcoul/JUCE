#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_osc/juce_osc.h>
#include <unistd.h>

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
    MainComponent()
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

        g.setColour (juce::Colour::fromRGBA (255, 255, 255, 215));
        g.setFont (juce::FontOptions (29.0f));
        g.drawText ("QNX Multitouch Synth Grid",
                    panel.getX() + 24.0f,
                    panel.getY() + 18.0f,
                    panel.getWidth() - 48.0f,
                    34.0f,
                    juce::Justification::left);

        g.setFont (juce::FontOptions (18.0f));
        g.drawText ("Touch or drag across the grid. X selects pitch, Y sets velocity. Mouse still works for desktop bring-up.",
                    panel.getX() + 24.0f,
                    panel.getY() + 56.0f,
                    panel.getWidth() - 48.0f,
                    24.0f,
                    juce::Justification::left);

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

            g.setColour (juce::Colours::black.withAlpha (0.75f));
            g.setFont (juce::FontOptions (16.0f));
            g.drawText (touch.isTouch ? "T" + juce::String (sourceId - 1)
                                      : "M",
                        markerBounds,
                        juce::Justification::centred);
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

            repaint();
            return true;
        }

        return false;
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

        repaint();
    }

    void releasePointer (const juce::MouseEvent& event)
    {
        const auto sourceId = sourceIdForEvent (event);
        const auto wasTouch = event.source.isTouch();
        synthSource.removeVoice (sourceId);
        activePointers.erase (sourceId);

        updateStatusText ((wasTouch ? "touch#" + juce::String (sourceId - 1) : "mouse") + " up");
        repaint();
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
        repaint();
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

        repaint();
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

    juce::AudioDeviceManager deviceManager;
    juce::AudioSourcePlayer player;
    TouchSynthAudioSource synthSource;
    std::map<int, ActivePointer> activePointers;
    std::set<int> activeOscNotes;
    juce::String lastError;
    juce::String statusText;
    bool audioReady = false;
    bool keyboardHeld = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

class QnxTouchFullscreenApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override       { return "JUCE QNX Touch Fullscreen Demo"; }
    const juce::String getApplicationVersion() override    { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override             { return true; }

    void initialise (const juce::String&) override
    {
        logger = createAppLogger();
        juce::Logger::setCurrentLogger (logger.get());
        juce::Logger::writeToLog ("Build version: " + juce::String (JUCE_QNX_TOUCH_FULLSCREEN_DEMO_BUILD_VERSION));
        juce::Logger::writeToLog ("Process PID: " + juce::String ((int) getpid()));
        juce::Logger::writeToLog ("Application initialise()");
        mainComponent = std::make_unique<MainComponent>();
        mainComponent->setName (getApplicationName());
        mainComponent->addToDesktop (0);
        mainComponent->setBounds (getInitialDisplayArea());
        mainComponent->setVisible (true);
        mainComponent->toFront (true);
        mainComponent->grabKeyboardFocus();
        juce::Logger::writeToLog ("Main component added to desktop");
    }

    void shutdown() override
    {
        juce::Logger::writeToLog ("Application shutdown()");
        if (mainComponent != nullptr)
            mainComponent->removeFromDesktop();

        mainComponent.reset();
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
    std::unique_ptr<MainComponent> mainComponent;
    std::unique_ptr<juce::FileLogger> logger;
};

START_JUCE_APPLICATION (QnxTouchFullscreenApplication)
