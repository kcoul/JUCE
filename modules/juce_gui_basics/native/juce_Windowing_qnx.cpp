#include <screen/screen.h>
#include <sys/keycodes.h>

#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <mutex>
#include <vector>

#include "juce_MultiTouchMapper.h"

namespace juce
{

namespace
{
    constexpr const char* qnxOpenGLPresentationProperty = "juce_qnx_use_opengl_presentation";

    void logQnxWindowing (const String& message)
    {
        Logger::writeToLog ("[QNX Windowing] " + message);
    }

    String qnxPeerStyleFlagsToString (int styleFlags)
    {
        StringArray flags;

        if ((styleFlags & ComponentPeer::windowAppearsOnTaskbar) != 0)   flags.add ("AppearsOnTaskbar");
        if ((styleFlags & ComponentPeer::windowIsTemporary) != 0)        flags.add ("Temporary");
        if ((styleFlags & ComponentPeer::windowIgnoresMouseClicks) != 0) flags.add ("IgnoresMouse");
        if ((styleFlags & ComponentPeer::windowHasTitleBar) != 0)        flags.add ("HasTitleBar");
        if ((styleFlags & ComponentPeer::windowIsResizable) != 0)        flags.add ("Resizable");
        if ((styleFlags & ComponentPeer::windowHasMinimiseButton) != 0)  flags.add ("HasMinimise");
        if ((styleFlags & ComponentPeer::windowHasMaximiseButton) != 0)  flags.add ("HasMaximise");
        if ((styleFlags & ComponentPeer::windowHasCloseButton) != 0)     flags.add ("HasClose");
        if ((styleFlags & ComponentPeer::windowHasDropShadow) != 0)      flags.add ("HasDropShadow");
        if ((styleFlags & ComponentPeer::windowRepaintedExplicitly) != 0) flags.add ("ExplicitRepaint");
        if ((styleFlags & ComponentPeer::windowIgnoresKeyPresses) != 0)  flags.add ("IgnoresKeys");
        if ((styleFlags & ComponentPeer::windowRequiresSynchronousCoreGraphicsRendering) != 0) flags.add ("SyncCoreGraphics");
        if ((styleFlags & ComponentPeer::windowIsSemiTransparent) != 0)  flags.add ("SemiTransparent");

        return flags.isEmpty() ? "<none>" : flags.joinIntoString ("|");
    }

    String describeQnxComponentForPeer (Component& component)
    {
        String description = "type=" + String (typeid (component).name());
        description += " name=\"" + component.getName() + "\"";
        description += " title=\"" + component.getTitle() + "\"";
        description += " opaque=" + String (component.isOpaque() ? "yes" : "no");
        description += " onDesktop=" + String (component.isOnDesktop() ? "yes" : "no");
        return description;
    }

    const char* qnxScreenEventTypeToString (int eventType) noexcept
    {
        switch (eventType)
        {
            case SCREEN_EVENT_NONE:            return "NONE";
            case SCREEN_EVENT_CREATE:          return "CREATE";
            case SCREEN_EVENT_PROPERTY:        return "PROPERTY";
            case SCREEN_EVENT_CLOSE:           return "CLOSE";
            case SCREEN_EVENT_INPUT:           return "INPUT";
            case SCREEN_EVENT_JOG:             return "JOG";
            case SCREEN_EVENT_POINTER:         return "POINTER";
            case SCREEN_EVENT_KEYBOARD:        return "KEYBOARD";
            case SCREEN_EVENT_USER:            return "USER";
            case SCREEN_EVENT_POST:            return "POST";
            case SCREEN_EVENT_DISPLAY:         return "DISPLAY";
            case SCREEN_EVENT_IDLE:            return "IDLE";
            case SCREEN_EVENT_UNREALIZE:       return "UNREALIZE";
            case SCREEN_EVENT_GAMEPAD:         return "GAMEPAD";
            case SCREEN_EVENT_JOYSTICK:        return "JOYSTICK";
            case SCREEN_EVENT_INPUT_CONTROL:   return "INPUT_CONTROL";
            case SCREEN_EVENT_GESTURE:         return "GESTURE";
            case SCREEN_EVENT_MANAGER:         return "MANAGER";
            case SCREEN_EVENT_MTOUCH_PRETOUCH: return "MTOUCH_PRETOUCH";
            case SCREEN_EVENT_MTOUCH_TOUCH:    return "MTOUCH_TOUCH";
            case SCREEN_EVENT_MTOUCH_MOVE:     return "MTOUCH_MOVE";
            case SCREEN_EVENT_MTOUCH_RELEASE:  return "MTOUCH_RELEASE";
            default:                           return "UNKNOWN";
        }
    }

    String& qnxClipboardStorage()
    {
        static String text;
        return text;
    }

    String getWindowPropertyString (screen_window_t window, int property, int maxLen = 256)
    {
        if (window == nullptr || maxLen <= 1)
            return {};

        HeapBlock<char> buffer ((size_t) maxLen, true);

        if (screen_get_window_property_cv (window, property, maxLen, buffer.getData()) != 0)
            return {};

        return String (buffer.getData());
    }

    String getScreenWindowPropertyName (int property)
    {
        switch (property)
        {
            case SCREEN_PROPERTY_MANAGER_STRING: return "MANAGER_STRING";
            case SCREEN_PROPERTY_ID_STRING:      return "ID_STRING";
            case SCREEN_PROPERTY_GROUP:          return "GROUP";
            case SCREEN_PROPERTY_PARENT:         return "PARENT";
            case SCREEN_PROPERTY_FOCUS:          return "FOCUS";
            case SCREEN_PROPERTY_POINTER_FOCUS:  return "POINTER_FOCUS";
            case SCREEN_PROPERTY_POSITION:       return "POSITION";
            case SCREEN_PROPERTY_SIZE:           return "SIZE";
            case SCREEN_PROPERTY_VISIBLE:        return "VISIBLE";
            case SCREEN_PROPERTY_ZORDER:         return "ZORDER";
            case SCREEN_PROPERTY_DISPLAY:        return "DISPLAY";
            case SCREEN_PROPERTY_TYPE:           return "TYPE";
            case SCREEN_PROPERTY_SENSITIVITY:    return "SENSITIVITY";
            default:                             return String (property);
        }
    }

    String getEventUserDataString (screen_event_t event, int maxLen = 256)
    {
        if (event == nullptr || maxLen <= 1)
            return {};

        HeapBlock<char> buffer ((size_t) maxLen, true);

        if (screen_get_event_property_cv (event, SCREEN_PROPERTY_USER_DATA, maxLen, buffer.getData()) != 0)
            return {};

        return String (buffer.getData());
    }

    Point<float>& qnxMousePosition()
    {
        static Point<float> pos;
        return pos;
    }

    int getNextQnxWindowZOrder() noexcept
    {
        static std::atomic<int> nextZOrder { 100 };
        return nextZOrder.fetch_add (1);
    }

    uint64 getNextQnxPeerActivationOrder() noexcept
    {
        static std::atomic<uint64> nextActivationOrder { 1 };
        return nextActivationOrder.fetch_add (1);
    }

    bool& qnxScreenSaverEnabled()
    {
        static bool enabled = true;
        return enabled;
    }

    bool isExperimentalQnxOpenGLEnabled()
    {
        static const bool enabled = SystemStats::getEnvironmentVariable ("JUCE_QNX_ENABLE_OPENGL", "1") != "0";
        return enabled;
    }

    bool shouldUseEmbeddedFullscreenQnxWindow()
    {
        static const bool enabled = SystemStats::getEnvironmentVariable ("JUCE_QNX_EMBEDDED_FULLSCREEN", "0") == "1";
        return enabled;
    }

    // Prototype fast software-present path: honour the accumulated dirty region
    // (render + blit + post only what changed) and reuse a persistent backing
    // image instead of malloc+memset-ing a full-window ARGB image every frame.
    // Off by default so the original path stays the A/B baseline.
    bool shouldUseFastQnxPresent()
    {
        static const bool enabled = SystemStats::getEnvironmentVariable ("JUCE_QNX_FAST_PRESENT", "0") == "1";
        return enabled;
    }

    // Emit a once-per-second "QNX_PRESENT_FPS ..." line from the present path so
    // any JUCE app (incl. SurgeXT) reports achieved present rate with no app code.
    bool shouldLogQnxPresentFps()
    {
        static const bool enabled = SystemStats::getEnvironmentVariable ("JUCE_QNX_LOG_FPS", "0") == "1";
        return enabled;
    }

    ModifierKeys qnxModifiersFromButtons (int buttons)
    {
        auto mods = ModifierKeys::getCurrentModifiersRealtime().withoutMouseButtons();

        if ((buttons & SCREEN_LEFT_MOUSE_BUTTON) != 0)   mods = mods.withFlags (ModifierKeys::leftButtonModifier);
        if ((buttons & SCREEN_MIDDLE_MOUSE_BUTTON) != 0) mods = mods.withFlags (ModifierKeys::middleButtonModifier);
        if ((buttons & SCREEN_RIGHT_MOUSE_BUTTON) != 0)  mods = mods.withFlags (ModifierKeys::rightButtonModifier);

        return mods;
    }

    ModifierKeys qnxModifiersFromKeyboard (int modifiers)
    {
        auto mods = ModifierKeys::currentModifiers.withoutMouseButtons()
                                                  .withoutFlags (ModifierKeys::shiftModifier
                                                               | ModifierKeys::ctrlModifier
                                                               | ModifierKeys::altModifier
                                                               | ModifierKeys::commandModifier);

        if ((modifiers & KEYMOD_SHIFT) != 0) mods = mods.withFlags (ModifierKeys::shiftModifier);
        if ((modifiers & KEYMOD_CTRL)  != 0) mods = mods.withFlags (ModifierKeys::ctrlModifier);
        if ((modifiers & KEYMOD_ALT)   != 0) mods = mods.withFlags (ModifierKeys::altModifier);

        return mods;
    }

    int qnxKeySymToJuceKeyCode (int keySym)
    {
        switch (keySym)
        {
            case KEYCODE_LEFT:       return KeyPress::leftKey;
            case KEYCODE_RIGHT:      return KeyPress::rightKey;
            case KEYCODE_UP:         return KeyPress::upKey;
            case KEYCODE_DOWN:       return KeyPress::downKey;
            case KEYCODE_PG_UP:      return KeyPress::pageUpKey;
            case KEYCODE_PG_DOWN:    return KeyPress::pageDownKey;
            case KEYCODE_HOME:       return KeyPress::homeKey;
            case KEYCODE_END:        return KeyPress::endKey;
            case KEYCODE_INSERT:     return KeyPress::insertKey;
            case KEYCODE_DELETE:     return KeyPress::deleteKey;
            case KEYCODE_BACKSPACE:  return KeyPress::backspaceKey;
            case KEYCODE_TAB:        return KeyPress::tabKey;
            case KEYCODE_RETURN:     return KeyPress::returnKey;
            case KEYCODE_ESCAPE:     return KeyPress::escapeKey;
            case KEYCODE_F1:         return KeyPress::F1Key;
            case KEYCODE_F2:         return KeyPress::F2Key;
            case KEYCODE_F3:         return KeyPress::F3Key;
            case KEYCODE_F4:         return KeyPress::F4Key;
            case KEYCODE_F5:         return KeyPress::F5Key;
            case KEYCODE_F6:         return KeyPress::F6Key;
            case KEYCODE_F7:         return KeyPress::F7Key;
            case KEYCODE_F8:         return KeyPress::F8Key;
            case KEYCODE_F9:         return KeyPress::F9Key;
            case KEYCODE_F10:        return KeyPress::F10Key;
            case KEYCODE_F11:        return KeyPress::F11Key;
            case KEYCODE_F12:        return KeyPress::F12Key;
            case KEYCODE_KP_PLUS:    return KeyPress::numberPadAdd;
            case KEYCODE_KP_MINUS:   return KeyPress::numberPadSubtract;
            case KEYCODE_KP_MULTIPLY:return KeyPress::numberPadMultiply;
            case KEYCODE_KP_DIVIDE:  return KeyPress::numberPadDivide;
            case KEYCODE_KP_DELETE:  return KeyPress::numberPadDecimalPoint;
            case KEYCODE_KP_INSERT:  return KeyPress::numberPad0;
            case KEYCODE_KP_END:     return KeyPress::numberPad1;
            case KEYCODE_KP_DOWN:    return KeyPress::numberPad2;
            case KEYCODE_KP_PG_DOWN: return KeyPress::numberPad3;
            case KEYCODE_KP_LEFT:    return KeyPress::numberPad4;
            case KEYCODE_KP_FIVE:    return KeyPress::numberPad5;
            case KEYCODE_KP_RIGHT:   return KeyPress::numberPad6;
            case KEYCODE_KP_HOME:    return KeyPress::numberPad7;
            case KEYCODE_KP_UP:      return KeyPress::numberPad8;
            case KEYCODE_KP_PG_UP:   return KeyPress::numberPad9;
            case KEYCODE_PLAY:       return KeyPress::playKey;
            case KEYCODE_STOP:       return KeyPress::stopKey;
            case KEYCODE_FAST_FORWARD:return KeyPress::fastForwardKey;
            case KEYCODE_REWIND:     return KeyPress::rewindKey;
            default:                 return keySym;
        }
    }

    bool isQnxEmergencyQuitChord (int flags, int modifiers, int sym, int keyCap) noexcept
    {
        const bool isKeyDown = (flags & SCREEN_FLAG_KEY_DOWN) != 0;
        const bool isRepeat = (flags & SCREEN_FLAG_KEY_REPEAT) != 0;

        if (! isKeyDown || isRepeat)
            return false;

        const auto keyCode = qnxKeySymToJuceKeyCode (sym != 0 ? sym : keyCap);
        const auto juceModifiers = qnxModifiersFromKeyboard (modifiers);

        return juceModifiers.isCtrlDown()
            && juceModifiers.isAltDown()
            && (keyCode == 'Q' || keyCode == 'q' || keyCode == KeyPress::escapeKey);
    }

    struct PeerState
    {
        struct PendingTouchEvent
        {
            Point<int> position;
            int touchId = -1;
            int eventType = SCREEN_EVENT_NONE;
        };

        std::atomic<class QnxComponentPeer*> peer { nullptr };
        std::atomic<bool> alive { true };
        Component* component = nullptr;
        std::function<void(Point<int>, int, int)> handlePointerEvent;
        std::function<void(Point<int>, int, int)> handleTouchEvent;
        std::function<void(int, int, int, int, int)> handleKeyboardEvent;
        Rectangle<int> bounds;
        int zOrder = 0;
        uint64 activationOrder = 0;
        bool visible = false;
        bool temporary = false;
        bool opaque = false;
        bool semiTransparent = false;
        bool ignoresMouseClicks = false;
        String debugName;
        std::mutex pendingTouchMutex;
        std::vector<PendingTouchEvent> pendingTouchEvents;
        bool touchDispatchPending = false;
    };

    class SharedQnxScreenEventThread final : public Thread
    {
    public:
        static constexpr uint64 eventWaitTimeoutNs = 100000000ULL;

        explicit SharedQnxScreenEventThread (screen_context_t contextIn)
            : Thread ("JUCE QNX Shared Screen Events"),
              context (contextIn)
        {
        }

        void registerWindow (screen_window_t window, std::shared_ptr<PeerState> state)
        {
            const std::scoped_lock lock (mutex);
            registrations[window] = state;
            logQnxWindowing ("Registered window with shared event thread");
        }

        void unregisterWindow (screen_window_t window)
        {
            const std::scoped_lock lock (mutex);
            registrations.erase (window);
            logQnxWindowing ("Unregistered window from shared event thread");
        }

        std::shared_ptr<PeerState> findTopmostPeerAt (Point<int> position, std::shared_ptr<PeerState> fallback) const
        {
            std::shared_ptr<PeerState> best = fallback;

            for (const auto& entry : registrations)
            {
                if (auto candidate = entry.second.lock())
                {
                    if (! candidate->alive || ! candidate->visible)
                        continue;

                    if (! candidate->bounds.contains (position))
                        continue;

                    if (best == nullptr || isBetterHitTarget (*candidate, *best))
                        best = std::move (candidate);
                }
            }

            return best;
        }

        static bool isBetterHitTarget (const PeerState& candidate, const PeerState& currentBest) noexcept
        {
            if (candidate.zOrder != currentBest.zOrder)
                return candidate.zOrder > currentBest.zOrder;

            const auto candidateModalRank = getModalInputRank (candidate);
            const auto currentModalRank = getModalInputRank (currentBest);

            if (candidateModalRank != currentModalRank)
                return candidateModalRank > currentModalRank;

            const auto candidateInputRank = getPointerInputRank (candidate);
            const auto currentInputRank = getPointerInputRank (currentBest);

            if (candidateInputRank != currentInputRank)
                return candidateInputRank > currentInputRank;

            if (candidate.activationOrder != currentBest.activationOrder)
                return candidate.activationOrder > currentBest.activationOrder;

            const auto candidateArea = candidate.bounds.getWidth() * candidate.bounds.getHeight();
            const auto currentBestArea = currentBest.bounds.getWidth() * currentBest.bounds.getHeight();

            if (candidateArea != currentBestArea)
                return candidateArea < currentBestArea;

            return false;
        }

        static int getModalInputRank (const PeerState& state) noexcept
        {
            auto* component = state.component;

            if (component == nullptr)
                return 0;

            if (component->isCurrentlyModal (false))
                return 4;

            if (component->isCurrentlyBlockedByAnotherModalComponent())
                return -4;

            if (auto* modal = Component::getCurrentlyModalComponent())
            {
                if (modal == component || modal->isParentOf (component))
                    return 4;

                if (! modal->canModalEventBeSentToComponent (component))
                    return -4;
            }

            return 0;
        }

        static int getPointerInputRank (const PeerState& state) noexcept
        {
            int rank = 0;

            if (! state.ignoresMouseClicks)
                rank += 8;

            if (state.opaque)
                rank += 4;

            if (! state.semiTransparent)
                rank += 2;

            if (state.temporary)
                rank += 1;

            return rank;
        }

        void run() override
        {
            if (context == nullptr)
                return;

            screen_event_t event = nullptr;

            if (screen_create_event (&event) != 0 || event == nullptr)
            {
                logQnxWindowing ("screen_create_event failed");
                return;
            }

            while (! threadShouldExit())
            {
                if (screen_get_event (context, event, eventWaitTimeoutNs) != 0)
                {
                    if (errno == ETIMEDOUT)
                        continue;

                    if (++getEventErrorCount <= 10)
                        logQnxWindowing ("screen_get_event failed errno=" + String (errno));

                    continue;
                }

                int eventType = SCREEN_EVENT_NONE;
                screen_get_event_property_iv (event, SCREEN_PROPERTY_TYPE, &eventType);

                if (eventType == SCREEN_EVENT_NONE)
                    continue;

                screen_window_t targetWindow = nullptr;
                screen_get_event_property_pv (event, SCREEN_PROPERTY_WINDOW, reinterpret_cast<void**> (&targetWindow));

                std::shared_ptr<PeerState> state;

                {
                    const std::scoped_lock lock (mutex);

                    if (auto it = registrations.find (targetWindow); it != registrations.end())
                        state = it->second.lock();

                    if (state == nullptr && registrations.size() == 1)
                    {
                        if (auto only = registrations.begin()->second.lock())
                            state = only;
                    }

                    if (eventType == SCREEN_EVENT_POINTER
                        || eventType == SCREEN_EVENT_MTOUCH_TOUCH
                        || eventType == SCREEN_EVENT_MTOUCH_MOVE
                        || eventType == SCREEN_EVENT_MTOUCH_RELEASE)
                    {
                        int position[2] { 0, 0 };
                        screen_get_event_property_iv (event, SCREEN_PROPERTY_POSITION, position);
                        auto hitState = findTopmostPeerAt ({ position[0], position[1] }, state);

                        if (eventType == SCREEN_EVENT_POINTER)
                        {
                            int buttons = 0;
                            screen_get_event_property_iv (event, SCREEN_PROPERTY_BUTTONS, &buttons);

                            if (buttons != 0)
                            {
                                if (lastPointerButtons == 0)
                                    pointerCaptureState = hitState;
                                else if (auto captured = pointerCaptureState.lock())
                                    hitState = captured;
                            }
                            else if (lastPointerButtons != 0)
                            {
                                if (auto captured = pointerCaptureState.lock())
                                    hitState = captured;

                                pointerCaptureState.reset();
                            }
                            else
                            {
                                pointerCaptureState.reset();
                            }

                            lastPointerButtons = buttons;
                        }

                        if (hitState != state)
                        {
                            logQnxWindowing ("Retargeted input event to peer "
                                             + (hitState != nullptr ? hitState->debugName : String ("<null>"))
                                             + " at "
                                             + String (position[0]) + "," + String (position[1]));
                        }

                        state = std::move (hitState);
                    }
                }

                ++receivedEventCount;

                if (receivedEventCount <= 25 || (receivedEventCount % 100) == 0)
                    logQnxWindowing ("Screen event #"
                                     + String (receivedEventCount)
                                     + " type="
                                     + qnxScreenEventTypeToString (eventType)
                                     + " objectType="
                                     + String (getObjectType (event))
                                     + " targetRegistered="
                                     + String (state != nullptr ? "yes" : "no"));

                if (eventType == SCREEN_EVENT_PROPERTY && (receivedEventCount <= 50 || state != nullptr))
                {
                    int property = 0;
                    int subtype = 0;
                    screen_get_event_property_iv (event, SCREEN_PROPERTY_NAME, &property);
                    screen_get_event_property_iv (event, SCREEN_PROPERTY_SUBTYPE, &subtype);

                    logQnxWindowing ("Property event pname="
                                     + getScreenWindowPropertyName (property)
                                     + " subtype="
                                     + String (subtype)
                                     + " targetRegistered="
                                     + String (state != nullptr ? "yes" : "no"));
                }

                if ((eventType == SCREEN_EVENT_MANAGER || eventType == SCREEN_EVENT_USER)
                    && (receivedEventCount <= 100 || state != nullptr))
                {
                    int subtype = 0;
                    screen_get_event_property_iv (event, SCREEN_PROPERTY_SUBTYPE, &subtype);
                    const auto userData = getEventUserDataString (event);

                    logQnxWindowing (String (eventType == SCREEN_EVENT_MANAGER ? "Manager" : "User")
                                     + " event subtype="
                                     + String (subtype)
                                     + " userData="
                                     + (userData.isNotEmpty() ? userData : "<empty>")
                                     + " targetRegistered="
                                     + String (state != nullptr ? "yes" : "no"));
                }

                if (eventType == SCREEN_EVENT_KEYBOARD)
                {
                    int flags = 0;
                    int modifiers = 0;
                    int sym = 0;
                    int keyCap = 0;
                    screen_get_event_property_iv (event, SCREEN_PROPERTY_FLAGS, &flags);
                    screen_get_event_property_iv (event, SCREEN_PROPERTY_MODIFIERS, &modifiers);
                    screen_get_event_property_iv (event, SCREEN_PROPERTY_SYM, &sym);
                    screen_get_event_property_iv (event, SCREEN_PROPERTY_KEY_CAP, &keyCap);

                    if (isQnxEmergencyQuitChord (flags, modifiers, sym, keyCap))
                    {
                        logQnxWindowing ("Emergency quit chord received in shared event thread");

                        MessageManager::callAsync ([]()
                        {
                            if (auto* app = JUCEApplicationBase::getInstance())
                                app->systemRequestedQuit();
                        });
                    }
                }

                if (state == nullptr || ! state->alive)
                    continue;

                if (eventType == SCREEN_EVENT_POINTER)
                {
                    int position[2] { 0, 0 };
                    int buttons = 0;
                    int wheelTicks = 0;

                    screen_get_event_property_iv (event, SCREEN_PROPERTY_POSITION, position);
                    screen_get_event_property_iv (event, SCREEN_PROPERTY_BUTTONS, &buttons);
                    screen_get_event_property_iv (event, SCREEN_PROPERTY_MOUSE_WHEEL, &wheelTicks);

                    const auto weakState = std::weak_ptr<PeerState> { state };
                    MessageManager::callAsync ([weakState, positionX = position[0], positionY = position[1], buttons, wheelTicks]
                    {
                        if (auto locked = weakState.lock())
                        {
                            if (! locked->alive)
                                return;

                            if (locked->handlePointerEvent != nullptr)
                                locked->handlePointerEvent ({ positionX, positionY }, buttons, wheelTicks);
                        }
                    });
                }
                else if (eventType == SCREEN_EVENT_MTOUCH_TOUCH
                      || eventType == SCREEN_EVENT_MTOUCH_MOVE
                      || eventType == SCREEN_EVENT_MTOUCH_RELEASE)
                {
                    int position[2] { 0, 0 };
                    int touchId = -1;
                    screen_get_event_property_iv (event, SCREEN_PROPERTY_POSITION, position);
                    screen_get_event_property_iv (event, SCREEN_PROPERTY_TOUCH_ID, &touchId);
                    queueTouchEvent (state, { position[0], position[1] }, touchId, eventType);
                }
                else if (eventType == SCREEN_EVENT_KEYBOARD)
                {
                    int flags = 0;
                    int modifiers = 0;
                    int scan = 0;
                    int sym = 0;
                    int keyCap = 0;
                    screen_get_event_property_iv (event, SCREEN_PROPERTY_FLAGS, &flags);
                    screen_get_event_property_iv (event, SCREEN_PROPERTY_MODIFIERS, &modifiers);
                    screen_get_event_property_iv (event, SCREEN_PROPERTY_SCAN, &scan);
                    screen_get_event_property_iv (event, SCREEN_PROPERTY_SYM, &sym);
                    screen_get_event_property_iv (event, SCREEN_PROPERTY_KEY_CAP, &keyCap);

                    const auto weakState = std::weak_ptr<PeerState> { state };
                    MessageManager::callAsync ([weakState, flags, modifiers, scan, sym, keyCap]
                    {
                        if (auto locked = weakState.lock())
                        {
                            if (! locked->alive)
                                return;

                            if (locked->handleKeyboardEvent != nullptr)
                                locked->handleKeyboardEvent (flags, modifiers, scan, sym, keyCap);
                        }
                    });
                }
                else if (receivedEventCount <= 25)
                {
                    logQnxWindowing ("Unhandled Screen event type for registered window: "
                                     + String (qnxScreenEventTypeToString (eventType)));
                }
            }

            screen_destroy_event (event);
        }

    private:
        static void queueTouchEvent (const std::shared_ptr<PeerState>& state,
                                     Point<int> position,
                                     int touchId,
                                     int eventType)
        {
            bool shouldScheduleDispatch = false;

            {
                const std::scoped_lock lock (state->pendingTouchMutex);

                if (eventType == SCREEN_EVENT_MTOUCH_MOVE
                    && ! state->pendingTouchEvents.empty())
                {
                    auto& lastEvent = state->pendingTouchEvents.back();

                    if (lastEvent.eventType == SCREEN_EVENT_MTOUCH_MOVE
                        && lastEvent.touchId == touchId)
                    {
                        lastEvent.position = position;
                    }
                    else
                    {
                        state->pendingTouchEvents.push_back ({ position, touchId, eventType });
                    }
                }
                else
                {
                    state->pendingTouchEvents.push_back ({ position, touchId, eventType });
                }

                if (! state->touchDispatchPending)
                {
                    state->touchDispatchPending = true;
                    shouldScheduleDispatch = true;
                }
            }

            if (! shouldScheduleDispatch)
                return;

            const auto weakState = std::weak_ptr<PeerState> { state };
            MessageManager::callAsync ([weakState]
            {
                if (auto locked = weakState.lock())
                {
                    if (! locked->alive)
                        return;

                    for (;;)
                    {
                        std::vector<PeerState::PendingTouchEvent> eventsToProcess;

                        {
                            const std::scoped_lock lock (locked->pendingTouchMutex);

                            if (locked->pendingTouchEvents.empty())
                            {
                                locked->touchDispatchPending = false;
                                break;
                            }

                            eventsToProcess.swap (locked->pendingTouchEvents);
                        }

                        if (locked->handleTouchEvent == nullptr)
                            continue;

                        for (const auto& event : eventsToProcess)
                            locked->handleTouchEvent (event.position, event.touchId, event.eventType);
                    }
                }
            });
        }

        static int getObjectType (screen_event_t event)
        {
            int objectType = 0;
            screen_get_event_property_iv (event, SCREEN_PROPERTY_OBJECT_TYPE, &objectType);
            return objectType;
        }

        screen_context_t context = nullptr;
        std::mutex mutex;
        std::map<screen_window_t, std::weak_ptr<PeerState>> registrations;
        std::weak_ptr<PeerState> pointerCaptureState;
        int lastPointerButtons = 0;
        int receivedEventCount = 0;
        int getEventErrorCount = 0;
    };

    struct SharedQnxScreenContext
    {
        screen_context_t context = nullptr;
        int referenceCount = 0;
        std::mutex mutex;
        std::unique_ptr<SharedQnxScreenEventThread> eventThread;
    };

    SharedQnxScreenContext& getSharedQnxScreenContext()
    {
        static SharedQnxScreenContext sharedContext;
        return sharedContext;
    }

    screen_context_t acquireSharedQnxScreenContext()
    {
        auto& shared = getSharedQnxScreenContext();
        const std::scoped_lock lock (shared.mutex);

        if (shared.context == nullptr)
        {
            if (screen_create_context (&shared.context, SCREEN_APPLICATION_CONTEXT) != 0)
            {
                logQnxWindowing ("screen_create_context failed");
                shared.context = nullptr;
                return nullptr;
            }

            logQnxWindowing ("screen_create_context succeeded (shared)");
            shared.eventThread = std::make_unique<SharedQnxScreenEventThread> (shared.context);
            shared.eventThread->startThread();
            logQnxWindowing ("Started shared Screen event thread");
        }

        ++shared.referenceCount;
        logQnxWindowing ("Acquired shared Screen context, refCount=" + String (shared.referenceCount));
        return shared.context;
    }

    void releaseSharedQnxScreenContext()
    {
        auto& shared = getSharedQnxScreenContext();
        std::unique_ptr<SharedQnxScreenEventThread> eventThread;
        screen_context_t contextToDestroy = nullptr;

        {
            const std::scoped_lock lock (shared.mutex);

            if (shared.context == nullptr)
                return;

            shared.referenceCount = jmax (0, shared.referenceCount - 1);
            logQnxWindowing ("Released shared Screen context, refCount=" + String (shared.referenceCount));

            if (shared.referenceCount == 0)
            {
                eventThread = std::move (shared.eventThread);
                contextToDestroy = shared.context;
                shared.context = nullptr;
                logQnxWindowing ("Tearing down shared Screen context");
            }
        }

        if (eventThread != nullptr)
        {
            eventThread->stopThread (2000);
            eventThread.reset();
            logQnxWindowing ("Stopped shared Screen event thread");
        }

        if (contextToDestroy != nullptr)
        {
            screen_destroy_context (contextToDestroy);
            logQnxWindowing ("Destroyed shared Screen context");
        }
    }

    void registerSharedQnxScreenWindow (screen_window_t window, std::shared_ptr<PeerState> state)
    {
        auto& shared = getSharedQnxScreenContext();
        const std::scoped_lock lock (shared.mutex);

        if (shared.eventThread != nullptr)
            shared.eventThread->registerWindow (window, std::move (state));
    }

    void unregisterSharedQnxScreenWindow (screen_window_t window)
    {
        auto& shared = getSharedQnxScreenContext();
        const std::scoped_lock lock (shared.mutex);

        if (shared.eventThread != nullptr)
            shared.eventThread->unregisterWindow (window);
    }

    screen_display_t getPrimaryQnxScreenDisplay (screen_context_t context)
    {
        if (context == nullptr)
            return nullptr;

        int displayCount = 0;

        if (screen_get_context_property_iv (context, SCREEN_PROPERTY_DISPLAY_COUNT, &displayCount) != 0 || displayCount <= 0)
            return nullptr;

        std::vector<screen_display_t> displays ((size_t) displayCount);

        if (screen_get_context_property_pv (context, SCREEN_PROPERTY_DISPLAYS, reinterpret_cast<void**> (displays.data())) != 0)
            return nullptr;

        return displays.front();
    }

    Rectangle<int> getPrimaryQnxDisplayBounds (screen_context_t context)
    {
        if (auto* display = getPrimaryQnxScreenDisplay (context))
        {
            int size[2] { 1920, 1080 };

            if (screen_get_display_property_iv (display, SCREEN_PROPERTY_SIZE, size) == 0
                && size[0] > 0
                && size[1] > 0)
            {
                return { 0, 0, size[0], size[1] };
            }
        }

        return { 0, 0, 1920, 1080 };
    }

    class QnxComponentPeer final : public ComponentPeer
    {
    public:
        QnxComponentPeer (Component& comp, int windowStyleFlags, void* nativeWindowToAttachTo)
            : ComponentPeer (comp, windowStyleFlags),
              nativeWindow (reinterpret_cast<screen_window_t> (nativeWindowToAttachTo)),
              attachedExternally (nativeWindowToAttachTo)
        {
            logQnxWindowing ("QnxComponentPeer ctor, attachedExternally=" + String (nativeWindow != nullptr ? "yes" : "no"));
            logQnxWindowing ("QnxComponentPeer component " + describeQnxComponentForPeer (component));
            logQnxWindowing ("QnxComponentPeer styleFlags=" + String (windowStyleFlags)
                             + " [" + qnxPeerStyleFlagsToString (windowStyleFlags) + "]");

            getNativeRealtimeModifiers = []() { return ModifierKeys::currentModifiers; };
            peerState->peer = this;
            peerState->component = &component;
            peerState->bounds = bounds;
            peerState->visible = false;
            peerState->zOrder = nativeZOrder;
            peerState->activationOrder = getNextQnxPeerActivationOrder();
            peerState->temporary = isTemporaryPeer();
            peerState->opaque = component.isOpaque();
            peerState->semiTransparent = (windowStyleFlags & ComponentPeer::windowIsSemiTransparent) != 0;
            peerState->ignoresMouseClicks = (windowStyleFlags & ComponentPeer::windowIgnoresMouseClicks) != 0;
            peerState->debugName = component.getName().isNotEmpty() ? component.getName()
                                 : component.getTitle().isNotEmpty() ? component.getTitle()
                                 : String (typeid (component).name());
            peerState->handlePointerEvent = [this] (Point<int> position, int buttons, int wheelTicks)
            {
                handlePointerEvent (position, buttons, wheelTicks);
            };
            peerState->handleTouchEvent = [this] (Point<int> position, int touchId, int eventType)
            {
                handleTouchEvent (position, touchId, eventType);
            };
            peerState->handleKeyboardEvent = [this] (int flags, int modifiers, int scan, int sym, int keyCap)
            {
                handleKeyboardEvent (flags, modifiers, scan, sym, keyCap);
            };

            if (auto componentBounds = component.getBounds(); ! componentBounds.isEmpty())
                bounds = componentBounds;

            if (nativeWindow != nullptr)
            {
                logQnxWindowing ("Using externally attached native window");
                return;
            }

            screenContext = acquireSharedQnxScreenContext();

            if (screenContext == nullptr)
                return;

            usingSharedContext = true;

            const auto embeddedFullscreen = shouldUseEmbeddedFullscreenQnxWindow();
            const auto groupedChildWindow = shouldUseGroupedChildWindow();
            const auto windowType = groupedChildWindow ? SCREEN_CHILD_WINDOW
                                   : embeddedFullscreen ? (SCREEN_APPLICATION_WINDOW | SCREEN_ROOT_WINDOW)
                                                        : SCREEN_APPLICATION_WINDOW;

            if (screen_create_window_type (&nativeWindow, screenContext, windowType) != 0)
            {
                logQnxWindowing ("screen_create_window_type failed, errno=" + String (errno));
                releaseSharedQnxScreenContext();
                screenContext = nullptr;
                usingSharedContext = false;
                return;
            }

            logQnxWindowing ("screen_create_window_type succeeded type="
                             + String (groupedChildWindow ? "SCREEN_CHILD_WINDOW"
                                                         : embeddedFullscreen ? "SCREEN_APPLICATION_WINDOW|SCREEN_ROOT_WINDOW"
                                                                              : "SCREEN_APPLICATION_WINDOW"));

            const int usage = SCREEN_USAGE_NATIVE | SCREEN_USAGE_READ | SCREEN_USAGE_WRITE
                            | SCREEN_USAGE_OPENGL_ES2 | SCREEN_USAGE_OPENGL_ES3;
            screen_set_window_property_iv (nativeWindow, SCREEN_PROPERTY_USAGE, &usage);

            const int format = SCREEN_FORMAT_RGBA8888;
            screen_set_window_property_iv (nativeWindow, SCREEN_PROPERTY_FORMAT, &format);

            const int transparency = component.isOpaque() ? SCREEN_TRANSPARENCY_NONE
                                                          : SCREEN_TRANSPARENCY_SOURCE_OVER;
            screen_set_window_property_iv (nativeWindow, SCREEN_PROPERTY_TRANSPARENCY, &transparency);

            const int sensitivity = SCREEN_SENSITIVITY_ALWAYS;
            screen_set_window_property_iv (nativeWindow, SCREEN_PROPERTY_SENSITIVITY, &sensitivity);

            const auto idString = "JUCEQNXHelloWorld";
            if (screen_set_window_property_cv (nativeWindow,
                                               SCREEN_PROPERTY_ID_STRING,
                                               (int) std::strlen (idString) + 1,
                                               idString) != 0)
            {
                logQnxWindowing ("screen_set_window_property_cv(SCREEN_PROPERTY_ID_STRING) failed, errno=" + String (errno));
            }

            joinedActiveWindowGroup = groupedChildWindow && joinActiveWindowGroup();
            attachWindowToPrimaryDisplay();

            if (embeddedFullscreen)
            {
                logQnxWindowing ("Embedded fullscreen mode active; skipping Screen window-manager/group setup");
                createEmbeddedInputSessions();
            }
            else
            {
                if (! joinedActiveWindowGroup && ! groupedChildWindow)
                {
                    logQnxWindowing ("Managed window mode active; using Screen application window");
                    ensureWindowGroupCreated();
                }

                if (joinedActiveWindowGroup)
                    logQnxWindowing ("Joined active Screen window group");
                else if (groupedChildWindow)
                    logQnxWindowing ("Grouped child window active without active group; leaving window ungrouped");

                if (joinedActiveWindowGroup || groupedChildWindow)
                    requestWindowGroupFocus();
                else
                    logQnxWindowing ("Skipping Screen group-focus request for managed secondary window");
            }

            updateWindowState();
            registerSharedQnxScreenWindow (nativeWindow, peerState);
            repaintTimer.startTimerHz (60);
            logQnxWindowing ("Started repaint timer at 60 Hz");
        }

        ~QnxComponentPeer() override
        {
            currentTouches.deleteAllTouchesForPeer (this);
            activeTouchContacts.clear();
            peerState->peer = nullptr;
            peerState->component = nullptr;
            peerState->alive = false;
            peerState->handlePointerEvent = {};
            peerState->handleTouchEvent = {};
            peerState->handleKeyboardEvent = {};
            logQnxWindowing ("QnxComponentPeer dtor");
            if (ownsWindow())
            {
                unregisterSharedQnxScreenWindow (nativeWindow);
                destroyEmbeddedInputSessions();
                destroyWindowGroupHandle();
                destroyWindowBuffers();

                if (nativeWindow != nullptr)
                    screen_destroy_window (nativeWindow);

                if (screenContext != nullptr)
                {
                    if (usingSharedContext)
                        releaseSharedQnxScreenContext();
                    else
                        screen_destroy_context (screenContext);
                }
            }
        }

        void* getNativeHandle() const override                             { return nativeWindow; }
        void setVisible (bool shouldBeVisible) override
        {
            isVisible = shouldBeVisible;

            if (shouldBeVisible)
            {
                nativeZOrder = getNextQnxWindowZOrder();
                peerState->activationOrder = getNextQnxPeerActivationOrder();

                if (joinedActiveWindowGroup || shouldUseGroupedChildWindow())
                    requestWindowGroupFocus();

                if (joinedActiveWindowGroup)
                    grabFocus();
            }

            logQnxWindowing ("setVisible(" + String (shouldBeVisible ? "true" : "false") + ")");
            updateWindowState();

            if (shouldBeVisible)
            {
                repaint (component.getLocalBounds());
            }
        }
        void setTitle (const String& newTitle) override                    { title = newTitle; }

        void setBounds (const Rectangle<int>& newBounds, bool isNowFullScreen) override
        {
            bounds = newBounds.withSize (jmax (1, newBounds.getWidth()),
                                         jmax (1, newBounds.getHeight()));
            fullScreen = isNowFullScreen;
            logQnxWindowing ("setBounds to " + bounds.toString() + ", fullscreen=" + String (fullScreen ? "true" : "false"));
            updateWindowState();
            handleMovedOrResized();
            repaint (component.getLocalBounds());
        }

        Rectangle<int> getBounds() const override                         { return bounds; }
        Point<float> localToGlobal (Point<float> p) override              { return p + bounds.getPosition().toFloat(); }
        Point<float> globalToLocal (Point<float> p) override              { return p - bounds.getPosition().toFloat(); }
        using ComponentPeer::localToGlobal;
        using ComponentPeer::globalToLocal;

        void setMinimised (bool shouldBeMinimised) override               { minimised = shouldBeMinimised; }
        bool isMinimised() const override                                 { return minimised; }
        bool isShowing() const override                                   { return isVisible && ! minimised; }
        void setFullScreen (bool shouldBeFullScreen) override             { fullScreen = shouldBeFullScreen; }
        bool isFullScreen() const override                                { return fullScreen; }
        void setIcon (const Image&) override                              {}

        bool contains (Point<int> localPos, bool) const override
        {
            const auto scaled = detail::ComponentHelpers::rawPeerPositionToLocal (component, localPos);
            return component.getLocalBounds().contains (scaled);
        }

        OptionalBorderSize getFrameSizeIfPresent() const override         { return {}; }
        BorderSize<int> getFrameSize() const override                     { return {}; }
        bool setAlwaysOnTop (bool alwaysOnTop) override                   { isAlwaysOnTop = alwaysOnTop; return true; }

        void toFront (bool takeKeyboardFocus) override
        {
            nativeZOrder = getNextQnxWindowZOrder();
            peerState->activationOrder = getNextQnxPeerActivationOrder();
            updateWindowState();
            handleBroughtToFront();

            if (takeKeyboardFocus)
                grabFocus();
        }

        void toBehind (ComponentPeer*) override                           {}
        bool isFocused() const override                                   { return focused; }

        void grabFocus() override
        {
            if (! focused)
            {
                focused = true;
                handleFocusGain();
            }
        }

        void textInputRequired (Point<int>, TextInputTarget&) override    {}
        void closeInputMethodContext() override                           {}
        void dismissPendingTextInput() override                           { closeInputMethodContext(); }

        void repaint (const Rectangle<int>& area) override
        {
            pendingRepaintArea = pendingRepaintArea.getUnion (area);
        }

        void performAnyPendingRepaintsNow() override
        {
            if (pendingRepaintArea.isEmpty())
                return;

            if (! shouldUseSoftwarePresentation())
            {
                if (windowBuffersCreated)
                {
                    logQnxWindowing ("Destroying software Screen buffers because software presentation is disabled");
                    destroyWindowBuffers();
                }

                if (repaintDispatchCount <= 5 || (repaintDispatchCount % 60) == 0)
                    logQnxWindowing ("Skipping software repaint because software presentation is disabled");

                pendingRepaintArea = {};
                return;
            }

            const ScopedValueSetter<bool> repaintSetter (isPerformingRepaint, true);

            const auto fullBounds = bounds.withZeroOrigin();

            if (fullBounds.isEmpty() || ! ensureWindowReady())
            {
                logQnxWindowing ("Skipping repaint, imageBounds=" + fullBounds.toString());
                return;
            }

            if (shouldUseFastQnxPresent())
            {
                performFastRepaint (fullBounds);
                return;
            }

            // --- Original baseline path: full-window re-render every frame. ---
            Image temp (Image::ARGB,
                        fullBounds.getWidth(),
                        fullBounds.getHeight(),
                        true);

            LowLevelGraphicsSoftwareRenderer renderer (temp);
            handlePaint (renderer);
            present (temp, fullBounds);
            pendingRepaintArea = {};
        }

        // Dirty-region-aware software repaint (prototype, JUCE_QNX_FAST_PRESENT=1).
        void performFastRepaint (Rectangle<int> fullBounds)
        {
            auto dirty = pendingRepaintArea.getIntersection (fullBounds);

            if (dirty.isEmpty())
            {
                pendingRepaintArea = {};
                return;
            }

            // Reuse the backing image across frames; only (re)allocate on resize.
            if (! backingImage.isValid()
                || backingImage.getWidth()  != fullBounds.getWidth()
                || backingImage.getHeight() != fullBounds.getHeight())
            {
                backingImage = Image (Image::ARGB, fullBounds.getWidth(), fullBounds.getHeight(), true);
                dirty = fullBounds; // fresh buffer: everything is dirty once
            }

            // Clear just the dirty region (cheap), then paint the component clipped to it.
            {
                Image::BitmapData bd (backingImage, dirty.getX(), dirty.getY(),
                                      dirty.getWidth(), dirty.getHeight(),
                                      Image::BitmapData::writeOnly);

                for (int y = 0; y < dirty.getHeight(); ++y)
                    std::memset (bd.getLinePointer (y), 0, (size_t) dirty.getWidth() * 4u);
            }

            LowLevelGraphicsSoftwareRenderer renderer (backingImage, Point<int>(), RectangleList<int> (dirty));
            handlePaint (renderer);
            presentRegion (backingImage, dirty);
            pendingRepaintArea = {};
        }

        void setAlpha (float newAlpha) override                           { alpha = newAlpha; }
        StringArray getAvailableRenderingEngines() override               { return { "Software Renderer" }; }
        double getPlatformScaleFactor() const noexcept override           { return 1.0; }

    private:
        bool ownsWindow() const noexcept                                  { return attachedExternally == nullptr; }
        bool shouldUseSoftwarePresentation() const noexcept
        {
            return ! shouldUseOpenGLPresentation();
        }

        bool isTemporaryPeer() const noexcept
        {
            return (getStyleFlags() & ComponentPeer::windowIsTemporary) != 0;
        }

        bool shouldUseGroupedChildWindow() const noexcept
        {
            if (isTemporaryPeer())
                return true;

            if (shouldUseEmbeddedFullscreenQnxWindow())
                return false;

            if (dynamic_cast<DocumentWindow*> (&component) == nullptr)
                return false;

            auto* activeWindow = TopLevelWindow::getActiveTopLevelWindow();
            return activeWindow != nullptr && activeWindow != &component;
        }

        bool shouldUseOpenGLPresentation() const noexcept
        {
            if (! isExperimentalQnxOpenGLEnabled())
                return false;

            if (component.getProperties().contains (qnxOpenGLPresentationProperty))
                return static_cast<bool> (component.getProperties()[qnxOpenGLPresentationProperty]);

            return false;
        }

        bool joinActiveWindowGroup()
        {
            if (! shouldUseGroupedChildWindow())
                return false;

            auto* activeWindow = TopLevelWindow::getActiveTopLevelWindow();

            if (activeWindow == nullptr || activeWindow == &component)
                return false;

            auto* activePeer = activeWindow->getPeer();

            if (activePeer == nullptr)
                return false;

            auto* activeNativeWindow = reinterpret_cast<screen_window_t> (activePeer->getNativeHandle());

            if (activeNativeWindow == nullptr || activeNativeWindow == nativeWindow)
                return false;

            const auto groupName = getWindowPropertyString (activeNativeWindow, SCREEN_PROPERTY_GROUP, 64);

            if (groupName.isEmpty())
            {
                logQnxWindowing ("Active peer has no Screen group to join");
                return false;
            }

            if (screen_join_window_group (nativeWindow, groupName.toRawUTF8()) != 0)
            {
                logQnxWindowing ("screen_join_window_group failed for group=" + groupName + " errno=" + String (errno));
                return false;
            }

            screen_group_t group = nullptr;

            if (screen_get_window_property_pv (nativeWindow, SCREEN_PROPERTY_GROUP, reinterpret_cast<void**> (&group)) != 0
                || group == nullptr)
            {
                logQnxWindowing ("Joined active Screen group by name but failed to query group handle");
                return false;
            }

            windowGroup = group;
            logQnxWindowing ("Joined active Screen group name=" + groupName);
            return true;
        }

        void attachWindowToPrimaryDisplay()
        {
            if (nativeWindow == nullptr || screenContext == nullptr)
                return;

            auto* display = getPrimaryQnxScreenDisplay (screenContext);

            if (display == nullptr)
            {
                logQnxWindowing ("No primary display available for window attachment");
                return;
            }

            auto* displayHandle = reinterpret_cast<void*> (display);

            if (screen_set_window_property_pv (nativeWindow, SCREEN_PROPERTY_DISPLAY, &displayHandle) != 0)
            {
                logQnxWindowing ("screen_set_window_property_pv(SCREEN_PROPERTY_DISPLAY) failed");
                return;
            }

            flushScreenContext ("attachWindowToPrimaryDisplay");

            int windowManagerId = SCREEN_INVALID_ID;
            screen_get_display_property_iv (display, SCREEN_PROPERTY_WINDOW_MANAGER_ID, &windowManagerId);
            updatePrimaryDisplayBoundsFromDisplay (display);

            logQnxWindowing ("Attached window to primary display size="
                             + String (primaryDisplayBounds.getWidth()) + "x" + String (primaryDisplayBounds.getHeight())
                             + " windowManagerId=" + String (windowManagerId));
        }

        void updatePrimaryDisplayBoundsFromContext()
        {
            if (screenContext == nullptr)
                return;

            if (auto* display = getPrimaryQnxScreenDisplay (screenContext))
                updatePrimaryDisplayBoundsFromDisplay (display);
        }

        void updatePrimaryDisplayBoundsFromDisplay (screen_display_t display)
        {
            if (display == nullptr)
                return;

            int displaySize[2] { 0, 0 };
            screen_get_display_property_iv (display, SCREEN_PROPERTY_SIZE, displaySize);
            primaryDisplayBounds = { 0, 0, jmax (1, displaySize[0]), jmax (1, displaySize[1]) };
        }

        void ensureWindowGroupCreated()
        {
            if (nativeWindow == nullptr)
                return;

            if (screen_create_window_group (nativeWindow, nullptr) != 0)
            {
                logQnxWindowing ("screen_create_window_group failed");
                return;
            }

            screen_group_t group = nullptr;

            if (screen_get_window_property_pv (nativeWindow, SCREEN_PROPERTY_GROUP, reinterpret_cast<void**> (&group)) != 0
                || group == nullptr)
            {
                logQnxWindowing ("screen_get_window_property_pv(SCREEN_PROPERTY_GROUP) failed");
                return;
            }

            windowGroup = group;

            char groupName[64] = {};
            const auto groupNameResult = screen_get_window_property_cv (nativeWindow,
                                                                        SCREEN_PROPERTY_GROUP,
                                                                        (int) sizeof (groupName),
                                                                        groupName);

            logQnxWindowing ("Window group created name="
                             + String (groupNameResult == 0 && groupName[0] != '\0' ? groupName : "<unnamed>"));
        }

        void requestWindowGroupFocus()
        {
            if (windowGroup == nullptr || nativeWindow == nullptr)
                return;

            auto* windowHandle = reinterpret_cast<void*> (nativeWindow);

            if (screen_set_group_property_pv (windowGroup, SCREEN_PROPERTY_FOCUS, &windowHandle) != 0)
            {
                logQnxWindowing ("screen_set_group_property_pv(SCREEN_PROPERTY_FOCUS) failed");
                return;
            }

            flushScreenContext ("requestWindowGroupFocus");
            logQnxWindowing ("Requested window-group focus");
        }

        void destroyWindowGroupHandle()
        {
            if (windowGroup != nullptr)
            {
                logQnxWindowing ("Destroying local window-group handle");
                screen_destroy_group (windowGroup);
                windowGroup = nullptr;
            }
        }

        void logManagerString (const char* reason)
        {
            managerString = getWindowPropertyString (nativeWindow, SCREEN_PROPERTY_MANAGER_STRING, 256);

            logQnxWindowing (String ("Manager string after ")
                             + reason
                             + "="
                             + (managerString.isNotEmpty() ? managerString : "<empty>"));
        }

        void dispatchDeferredRepaints()
        {
            if (! isVisible || minimised)
                return;

            if (! shouldUseSoftwarePresentation() && windowBuffersCreated)
            {
                logQnxWindowing ("Destroying software Screen buffers because software presentation is disabled");
                destroyWindowBuffers();
            }

            if (pendingRepaintArea.isEmpty())
                return;

            ++repaintDispatchCount;

            if (repaintDispatchCount <= 5 || (repaintDispatchCount % 60) == 0)
                logQnxWindowing ("Dispatching repaint #" + String (repaintDispatchCount));

            performAnyPendingRepaintsNow();
        }

        void handlePointerEvent (Point<int> eventPosition, int buttons, int wheelTicks)
        {
            auto localPos = getLocalEventPosition (eventPosition);

            const auto mods = qnxModifiersFromButtons (buttons);

            ModifierKeys::currentModifiers = mods;
            qnxMousePosition() = eventPosition.toFloat();

            ++pointerEventCount;

            const auto shouldLogPointerEvent = buttons != 0
                                            || wheelTicks != 0
                                            || pointerEventCount <= 10
                                            || (pointerEventCount % 100) == 0;

            if (shouldLogPointerEvent)
                logQnxWindowing ("Pointer event pos="
                                 + String (eventPosition.x) + "," + String (eventPosition.y)
                                 + " local=" + String (roundToInt (localPos.x)) + "," + String (roundToInt (localPos.y))
                                 + " display=" + String (primaryDisplayBounds.getWidth()) + "x" + String (primaryDisplayBounds.getHeight())
                                 + " bounds=" + String (bounds.getWidth()) + "x" + String (bounds.getHeight())
                                 + " buttons=" + String (buttons)
                                 + " wheel=" + String (wheelTicks));

            handleMouseEvent (MouseInputSource::InputSourceType::mouse,
                              localPos,
                              mods,
                              0.0f,
                              0.0f,
                              Time::currentTimeMillis(),
                              {},
                              0);

            if (wheelTicks != 0)
            {
                MouseWheelDetails wheel;
                wheel.deltaX = 0.0f;
                wheel.deltaY = (float) wheelTicks / 3.0f;
                wheel.isReversed = false;
                wheel.isSmooth = false;
                wheel.isInertial = false;

                handleMouseWheel (MouseInputSource::InputSourceType::mouse,
                                  localPos,
                                  Time::currentTimeMillis(),
                                  wheel,
                                  0);
            }
        }

        void handleTouchEvent (Point<int> eventPosition, int touchId, int eventType)
        {
            const auto localPos = getLocalEventPosition (eventPosition);
            const auto time = Time::currentTimeMillis();
            const auto stableTouchId = getStableTouchId (localPos, touchId, eventType);
            const auto touchIndex = currentTouches.getIndexOfTouch (this, stableTouchId);
            auto modsToSend = ModifierKeys::getCurrentModifiers().withoutMouseButtons();
            bool shouldSendCancel = false;
            static int touchEventLogCount = 0;

            ++touchEventLogCount;

            const auto shouldLogTouchEvent = eventType != SCREEN_EVENT_MTOUCH_MOVE
                                          || touchEventLogCount <= 20
                                          || (touchEventLogCount % 100) == 0;

            if (shouldLogTouchEvent)
            {
                logQnxWindowing ("Touch event type="
                                 + String (qnxScreenEventTypeToString (eventType))
                                 + " rawTouchId=" + String (touchId)
                                 + " stableTouchId=" + String (stableTouchId)
                                 + String (touchId <= 0 ? " (fallback)" : "")
                                 + " touchIndex=" + String (touchIndex)
                                 + " global=" + String (eventPosition.x) + "," + String (eventPosition.y)
                                 + " local=" + String (roundToInt (localPos.x)) + "," + String (roundToInt (localPos.y)));
            }

            if (eventType == SCREEN_EVENT_MTOUCH_TOUCH)
            {
                ModifierKeys::currentModifiers = modsToSend.withFlags (ModifierKeys::leftButtonModifier);
                modsToSend = ModifierKeys::currentModifiers;

                handleMouseEvent (MouseInputSource::InputSourceType::touch,
                                  localPos,
                                  modsToSend.withoutMouseButtons(),
                                  MouseInputSource::defaultPressure,
                                  0.0f,
                                  time,
                                  {},
                                  touchIndex);
            }
            else if (eventType == SCREEN_EVENT_MTOUCH_RELEASE)
            {
                ModifierKeys::currentModifiers = modsToSend;
                currentTouches.clearTouch (touchIndex);
                releaseFallbackTouch (touchId, stableTouchId);
                shouldSendCancel = ! currentTouches.areAnyTouchesActive();
            }
            else
            {
                ModifierKeys::currentModifiers = modsToSend.withFlags (ModifierKeys::leftButtonModifier);
                modsToSend = ModifierKeys::currentModifiers;
            }

            handleMouseEvent (MouseInputSource::InputSourceType::touch,
                              localPos,
                              modsToSend,
                              MouseInputSource::defaultPressure,
                              0.0f,
                              time,
                              {},
                              touchIndex);

            if (eventType == SCREEN_EVENT_MTOUCH_RELEASE)
            {
                handleMouseEvent (MouseInputSource::InputSourceType::touch,
                                  MouseInputSource::offscreenMousePos,
                                  ModifierKeys::getCurrentModifiers().withoutMouseButtons(),
                                  MouseInputSource::defaultPressure,
                                  0.0f,
                                  time,
                                  {},
                                  touchIndex);

                if (shouldSendCancel)
                    currentTouches.clear();
            }
        }

        int getStableTouchId (Point<float> position, int rawTouchId, int eventType)
        {
            if (eventType == SCREEN_EVENT_MTOUCH_TOUCH)
            {
                if (const auto existingTouchId = findBestMatchingActiveTouch (position, rawTouchId, 20.0f);
                    existingTouchId != 0)
                {
                    updateActiveTouchContact (existingTouchId, rawTouchId, position);
                    return existingTouchId;
                }

                const auto stableTouchId = nextStableTouchId++;
                updateActiveTouchContact (stableTouchId, rawTouchId, position);
                return stableTouchId;
            }

            if (const auto existingTouchId = findBestMatchingActiveTouch (position, rawTouchId, std::numeric_limits<float>::max());
                existingTouchId != 0)
            {
                updateActiveTouchContact (existingTouchId, rawTouchId, position);
                return existingTouchId;
            }

            const auto stableTouchId = nextStableTouchId++;
            updateActiveTouchContact (stableTouchId, rawTouchId, position);
            return stableTouchId;
        }

        int findBestMatchingActiveTouch (Point<float> position, int rawTouchId, float maxDistance) const
        {
            auto bestTouchId = 0;
            auto bestScore = std::numeric_limits<float>::max();

            for (const auto& [stableTouchId, contact] : activeTouchContacts)
            {
                const auto delta = contact.position - position;
                const auto distanceSquared = delta.x * delta.x + delta.y * delta.y;

                if (distanceSquared > maxDistance * maxDistance)
                    continue;

                auto score = distanceSquared;

                if (rawTouchId > 0 && contact.rawTouchId == rawTouchId)
                    score -= 1000000.0f;

                if (score < bestScore)
                {
                    bestScore = score;
                    bestTouchId = stableTouchId;
                }
            }

            return bestTouchId;
        }

        void updateActiveTouchContact (int stableTouchId, int rawTouchId, Point<float> position)
        {
            activeTouchContacts[stableTouchId] = { stableTouchId, rawTouchId, position };
        }

        void releaseFallbackTouch (int rawTouchId, int stableTouchId)
        {
            ignoreUnused (rawTouchId);
            activeTouchContacts.erase (stableTouchId);
        }

        Point<float> getLocalEventPosition (Point<int> eventPosition)
        {
            auto localPos = globalToLocal (eventPosition.toFloat());

            if (! joinedActiveWindowGroup
                && ! isTemporaryPeer()
                && primaryDisplayBounds.getWidth() > 1
                && primaryDisplayBounds.getHeight() > 1
                && ! primaryDisplayBounds.isEmpty()
                && (primaryDisplayBounds.getWidth() != bounds.getWidth()
                    || primaryDisplayBounds.getHeight() != bounds.getHeight()))
            {
                localPos.x = ((float) eventPosition.x / (float) primaryDisplayBounds.getWidth()) * (float) bounds.getWidth();
                localPos.y = ((float) eventPosition.y / (float) primaryDisplayBounds.getHeight()) * (float) bounds.getHeight();
            }

            return localPos;
        }

        void handleKeyboardEvent (int flags, int modifiers, int scan, int sym, int keyCap)
        {
            const bool isKeyDown = (flags & SCREEN_FLAG_KEY_DOWN) != 0;
            const bool isRepeat = (flags & SCREEN_FLAG_KEY_REPEAT) != 0;
            const auto newModifiers = qnxModifiersFromKeyboard (modifiers);
            const bool modifiersChanged = newModifiers != ModifierKeys::currentModifiers;
            ModifierKeys::currentModifiers = newModifiers;

            if (! focused)
            {
                focused = true;
                handleFocusGain();
            }

            if (modifiersChanged)
                handleModifierKeysChange();

            if (++keyboardEventCount <= 20 || (keyboardEventCount % 50) == 0)
                logQnxWindowing ("Keyboard event flags=" + String (flags)
                                 + " modifiers=" + String (modifiers)
                                 + " scan=" + String (scan)
                                 + " sym=" + String (sym)
                                 + " keyCap=" + String (keyCap)
                                 + " isKeyDown=" + String (isKeyDown ? "yes" : "no")
                                 + " repeat=" + String (isRepeat ? "yes" : "no"));

            const int keyCode = qnxKeySymToJuceKeyCode (sym != 0 ? sym : keyCap);
            const juce_wchar textCharacter = (keyCap >= 0x20 && keyCap != KEYCODE_DELETE) ? (juce_wchar) keyCap : 0;
            if (isQnxEmergencyQuitChord (flags, modifiers, sym, keyCap))
            {
                logQnxWindowing ("Emergency quit chord received in peer");

                MessageManager::callAsync ([]()
                {
                    if (auto* app = JUCEApplicationBase::getInstance())
                        app->systemRequestedQuit();
                });

                return;
            }

            if (isKeyDown)
            {
                handleKeyUpOrDown (true);
                handleKeyPress (keyCode, textCharacter);
            }
            else
            {
                handleKeyUpOrDown (false);
            }
        }

        void createEmbeddedInputSessions()
        {
            if (screenContext == nullptr || nativeWindow == nullptr)
                return;

            auto* windowHandle = reinterpret_cast<void*> (nativeWindow);
            auto* display = getPrimaryQnxScreenDisplay (screenContext);

            if (screen_create_session_type (&pointerSession, screenContext, SCREEN_EVENT_POINTER) == 0)
            {
                if (screen_set_session_property_pv (pointerSession, SCREEN_PROPERTY_WINDOW, &windowHandle) == 0)
                    logQnxWindowing ("Created embedded pointer session for root window");
                else
                    logQnxWindowing ("screen_set_session_property_pv(pointer, SCREEN_PROPERTY_WINDOW) failed, errno=" + String (errno));

                attachEmbeddedSessionToPrimaryDisplay (pointerSession, display, "pointer");
            }
            else
            {
                logQnxWindowing ("screen_create_session_type(SCREEN_EVENT_POINTER) failed, errno=" + String (errno));
            }

            if (screen_create_session_type (&mtouchSession, screenContext, SCREEN_EVENT_MTOUCH_TOUCH) == 0)
            {
                logQnxWindowing ("Created embedded mtouch session for primary display");
                attachEmbeddedSessionToPrimaryDisplay (mtouchSession, display, "mtouch");

                const int mode = SCREEN_INPUT_MODE_RAW;
                if (screen_set_session_property_iv (mtouchSession, SCREEN_PROPERTY_MODE, &mode) == 0)
                    logQnxWindowing ("Set embedded mtouch session mode=SCREEN_INPUT_MODE_RAW");
                else
                    logQnxWindowing ("screen_set_session_property_iv(mtouch, SCREEN_PROPERTY_MODE) failed, errno=" + String (errno));
            }
            else
            {
                logQnxWindowing ("screen_create_session_type(SCREEN_EVENT_MTOUCH_TOUCH) failed, errno=" + String (errno));
            }

            if (screen_create_session_type (&keyboardSession, screenContext, SCREEN_EVENT_KEYBOARD) == 0)
            {
                if (screen_set_session_property_pv (keyboardSession, SCREEN_PROPERTY_WINDOW, &windowHandle) == 0)
                    logQnxWindowing ("Created embedded keyboard session for root window");
                else
                    logQnxWindowing ("screen_set_session_property_pv(keyboard, SCREEN_PROPERTY_WINDOW) failed, errno=" + String (errno));

                attachEmbeddedSessionToPrimaryDisplay (keyboardSession, display, "keyboard");
            }
            else
            {
                logQnxWindowing ("screen_create_session_type(SCREEN_EVENT_KEYBOARD) failed, errno=" + String (errno));
            }

            updateEmbeddedInputSessions();
            bindEmbeddedInputDevices();
            flushScreenContext ("createEmbeddedInputSessions");
        }

        void attachEmbeddedSessionToPrimaryDisplay (screen_session_t session,
                                                    screen_display_t display,
                                                    const char* name)
        {
            if (session == nullptr || display == nullptr)
                return;

            auto* displayHandle = reinterpret_cast<void*> (display);

            if (screen_set_session_property_pv (session, SCREEN_PROPERTY_DISPLAY, &displayHandle) == 0)
                logQnxWindowing ("Attached embedded " + String (name) + " session to primary display");
            else
                logQnxWindowing ("screen_set_session_property_pv(" + String (name) + ", SCREEN_PROPERTY_DISPLAY) failed, errno=" + String (errno));
        }

        void updateEmbeddedInputSessions()
        {
            if (nativeWindow == nullptr)
                return;

            const int position[2] { bounds.getX(), bounds.getY() };
            const int size[2] { jmax (1, bounds.getWidth()), jmax (1, bounds.getHeight()) };
            const int zOrder = 1000;
            const int visible = isVisible ? 1 : 0;

            auto updateSession = [this, &position, &size, &zOrder, &visible] (screen_session_t session,
                                                                               const char* name,
                                                                               bool shouldSetVisible)
            {
                if (session == nullptr)
                    return;

                if (screen_set_session_property_iv (session, SCREEN_PROPERTY_POSITION, position) != 0)
                    logQnxWindowing ("screen_set_session_property_iv(" + String (name) + ", SCREEN_PROPERTY_POSITION) failed, errno=" + String (errno));

                if (screen_set_session_property_iv (session, SCREEN_PROPERTY_SIZE, size) != 0)
                    logQnxWindowing ("screen_set_session_property_iv(" + String (name) + ", SCREEN_PROPERTY_SIZE) failed, errno=" + String (errno));

                if (screen_set_session_property_iv (session, SCREEN_PROPERTY_ZORDER, &zOrder) != 0)
                    logQnxWindowing ("screen_set_session_property_iv(" + String (name) + ", SCREEN_PROPERTY_ZORDER) failed, errno=" + String (errno));

                if (shouldSetVisible
                    && screen_set_session_property_iv (session, SCREEN_PROPERTY_VISIBLE, &visible) != 0)
                {
                    logQnxWindowing ("screen_set_session_property_iv(" + String (name) + ", SCREEN_PROPERTY_VISIBLE) failed, errno=" + String (errno));
                }
            };

            updateSession (pointerSession, "pointer", false);
            updateSession (mtouchSession, "mtouch", true);
            updateSession (keyboardSession, "keyboard", false);

            if (++sessionStateLogCount <= 10 || (sessionStateLogCount % 25) == 0)
                logQnxWindowing ("Updated embedded input sessions size="
                                 + String (size[0]) + "x" + String (size[1])
                                 + " visible=" + String (visible));
        }

        void bindEmbeddedInputDevices()
        {
            if (screenContext == nullptr)
                return;

            int deviceCount = 0;

            if (screen_get_context_property_iv (screenContext, SCREEN_PROPERTY_DEVICE_COUNT, &deviceCount) != 0 || deviceCount <= 0)
            {
                logQnxWindowing ("No Screen input devices available for embedded session binding");
                return;
            }

            std::vector<screen_device_t> devices ((size_t) deviceCount);

            if (screen_get_context_property_pv (screenContext, SCREEN_PROPERTY_DEVICES, reinterpret_cast<void**> (devices.data())) != 0)
            {
                logQnxWindowing ("screen_get_context_property_pv(SCREEN_PROPERTY_DEVICES) failed");
                return;
            }

            for (auto device : devices)
            {
                if (device == nullptr)
                    continue;

                int deviceType = 0;
                screen_get_device_property_iv (device, SCREEN_PROPERTY_TYPE, &deviceType);
                logQnxWindowing ("Embedded input device type=" + String (deviceType));

                if (pointerSession != nullptr && deviceType == SCREEN_EVENT_POINTER)
                {
                    auto* sessionHandle = reinterpret_cast<void*> (pointerSession);
                    if (screen_set_device_property_pv (device, SCREEN_PROPERTY_SESSION, &sessionHandle) == 0)
                        logQnxWindowing ("Bound pointer device to embedded pointer session");
                    else
                        logQnxWindowing ("screen_set_device_property_pv(pointer, SCREEN_PROPERTY_SESSION) failed, errno=" + String (errno));
                }

                if (mtouchSession != nullptr && deviceType == SCREEN_EVENT_MTOUCH_TOUCH)
                {
                    auto* sessionHandle = reinterpret_cast<void*> (mtouchSession);
                    if (screen_set_device_property_pv (device, SCREEN_PROPERTY_SESSION, &sessionHandle) == 0)
                        logQnxWindowing ("Bound mtouch device to embedded mtouch session");
                    else
                        logQnxWindowing ("screen_set_device_property_pv(mtouch, SCREEN_PROPERTY_SESSION) failed, errno=" + String (errno));
                }

                if (keyboardSession != nullptr && deviceType == SCREEN_EVENT_KEYBOARD)
                {
                    auto* sessionHandle = reinterpret_cast<void*> (keyboardSession);
                    if (screen_set_device_property_pv (device, SCREEN_PROPERTY_SESSION, &sessionHandle) == 0)
                        logQnxWindowing ("Bound keyboard device to embedded keyboard session");
                    else
                        logQnxWindowing ("screen_set_device_property_pv(keyboard, SCREEN_PROPERTY_SESSION) failed, errno=" + String (errno));
                }
            }
        }

        void destroyEmbeddedInputSessions()
        {
            if (pointerSession != nullptr)
            {
                logQnxWindowing ("Destroying embedded pointer session");
                screen_destroy_session (pointerSession);
                pointerSession = nullptr;
            }

            if (mtouchSession != nullptr)
            {
                logQnxWindowing ("Destroying embedded mtouch session");
                screen_destroy_session (mtouchSession);
                mtouchSession = nullptr;
            }

            if (keyboardSession != nullptr)
            {
                logQnxWindowing ("Destroying embedded keyboard session");
                screen_destroy_session (keyboardSession);
                keyboardSession = nullptr;
            }
        }

        void flushScreenContext (const char* reason) const
        {
            if (screenContext == nullptr)
                return;

            if (screen_flush_context (screenContext, 0) != 0)
                logQnxWindowing ("screen_flush_context failed after " + String (reason));
        }

        static int getWindowId (screen_window_t window) noexcept
        {
            if (window == nullptr)
                return -1;

            int id = -1;
            screen_get_window_property_iv (window, SCREEN_PROPERTY_ID, &id);
            return id;
        }

        bool ensureWindowReady()
        {
            if (nativeWindow == nullptr)
            {
                logQnxWindowing ("ensureWindowReady failed: nativeWindow is null");
                return false;
            }

            if (! shouldUseSoftwarePresentation())
            {
                if (windowBuffersCreated)
                {
                    logQnxWindowing ("Destroying software Screen buffers because software presentation is disabled");
                    destroyWindowBuffers();
                }

                return true;
            }

            const auto requestedSize = Point<int> (bounds.getWidth(), bounds.getHeight());

            if (windowBuffersCreated && bufferSize == requestedSize)
                return true;

            if (! ownsWindow())
                return true;

            destroyWindowBuffers();

            bufferSize = requestedSize;

            const int size[2] = { bufferSize.x, bufferSize.y };
            screen_set_window_property_iv (nativeWindow, SCREEN_PROPERTY_BUFFER_SIZE, size);

            if (screen_create_window_buffers (nativeWindow, 1) != 0)
            {
                logQnxWindowing ("screen_create_window_buffers failed for size "
                                 + String (bufferSize.x) + "x" + String (bufferSize.y));
                return false;
            }

            windowBuffersCreated = true;
            logQnxWindowing ("screen_create_window_buffers succeeded for size "
                             + String (bufferSize.x) + "x" + String (bufferSize.y));
            return true;
        }

        void destroyWindowBuffers()
        {
            if (windowBuffersCreated && nativeWindow != nullptr)
            {
                logQnxWindowing ("Destroying Screen window buffers");
                screen_destroy_window_buffers (nativeWindow);
                windowBuffersCreated = false;
            }
        }

        void updateWindowState()
        {
            if (nativeWindow == nullptr)
                return;

            const int position[2] = { bounds.getX(), bounds.getY() };
            const int size[2] = { bounds.getWidth(), bounds.getHeight() };
            const int visible = isVisible ? 1 : 0;
            const int zOrder = isAlwaysOnTop ? jmax (nativeZOrder, 10000) : nativeZOrder;

            peerState->bounds = bounds;
            peerState->visible = isVisible;
            peerState->zOrder = zOrder;

            screen_set_window_property_iv (nativeWindow, SCREEN_PROPERTY_POSITION, position);
            screen_set_window_property_iv (nativeWindow, SCREEN_PROPERTY_SIZE, size);
            screen_set_window_property_iv (nativeWindow, SCREEN_PROPERTY_VISIBLE, &visible);
            screen_set_window_property_iv (nativeWindow, SCREEN_PROPERTY_ZORDER, &zOrder);
            updateEmbeddedInputSessions();
            flushScreenContext ("updateWindowState");

            int hasFocus = 0;
            int hasPointerFocus = 0;
            int actualZOrder = 0;
            int status = 0;
            int type = 0;
            int sensitivity = 0;
            int ownerPid = -1;
            screen_get_window_property_iv (nativeWindow, SCREEN_PROPERTY_FOCUS, &hasFocus);
            screen_get_window_property_iv (nativeWindow, SCREEN_PROPERTY_POINTER_FOCUS, &hasPointerFocus);
            screen_get_window_property_iv (nativeWindow, SCREEN_PROPERTY_ZORDER, &actualZOrder);
            screen_get_window_property_iv (nativeWindow, SCREEN_PROPERTY_STATUS, &status);
            screen_get_window_property_iv (nativeWindow, SCREEN_PROPERTY_TYPE, &type);
            screen_get_window_property_iv (nativeWindow, SCREEN_PROPERTY_SENSITIVITY, &sensitivity);
            screen_get_window_property_iv (nativeWindow, SCREEN_PROPERTY_OWNER_PID, &ownerPid);

            if (lastLoggedStatus != status)
            {
                logQnxWindowing ("Window status transition " + String (lastLoggedStatus) + " -> " + String (status));
                lastLoggedStatus = status;
            }

            const auto groupName = getWindowPropertyString (nativeWindow, SCREEN_PROPERTY_GROUP, 64);
            const auto className = getWindowPropertyString (nativeWindow, SCREEN_PROPERTY_CLASS, 128);
            const auto idName = getWindowPropertyString (nativeWindow, SCREEN_PROPERTY_ID, 128);
            const auto idString = getWindowPropertyString (nativeWindow, SCREEN_PROPERTY_ID_STRING, 128);
            const auto parentId = getWindowPropertyString (nativeWindow, SCREEN_PROPERTY_PARENT, 128);
            if (managerString.isEmpty())
                managerString = getWindowPropertyString (nativeWindow, SCREEN_PROPERTY_MANAGER_STRING, 256);

            void* parent = nullptr;
            void* display = nullptr;
            void* contextFocus = nullptr;
            void* contextPointerFocus = nullptr;
            void* contextMTouchFocus = nullptr;
            void* groupFocus = nullptr;
            screen_get_window_property_pv (nativeWindow, SCREEN_PROPERTY_PARENT, &parent);
            screen_get_window_property_pv (nativeWindow, SCREEN_PROPERTY_DISPLAY, &display);
            if (screenContext != nullptr)
            {
                screen_get_context_property_pv (screenContext, SCREEN_PROPERTY_FOCUS, &contextFocus);
                screen_get_context_property_pv (screenContext, SCREEN_PROPERTY_POINTER_FOCUS, &contextPointerFocus);
                screen_get_context_property_pv (screenContext, SCREEN_PROPERTY_MTOUCH_FOCUS, &contextMTouchFocus);
            }

            if (windowGroup != nullptr)
                screen_get_group_property_pv (windowGroup, SCREEN_PROPERTY_FOCUS, &groupFocus);

            const auto ownWindowId = getWindowId (nativeWindow);
            const auto contextFocusId = getWindowId (reinterpret_cast<screen_window_t> (contextFocus));
            const auto contextPointerFocusId = getWindowId (reinterpret_cast<screen_window_t> (contextPointerFocus));
            const auto contextMTouchFocusId = getWindowId (reinterpret_cast<screen_window_t> (contextMTouchFocus));
            const auto groupFocusId = getWindowId (reinterpret_cast<screen_window_t> (groupFocus));

            logQnxWindowing ("updateWindowState position="
                             + String (position[0]) + "," + String (position[1])
                             + " size=" + String (size[0]) + "x" + String (size[1])
                             + " visible=" + String (visible)
                             + " focus=" + String (hasFocus)
                             + " pointerFocus=" + String (hasPointerFocus)
                             + " zOrder=" + String (actualZOrder)
                             + " status=" + String (status)
                             + " type=" + String (type)
                             + " sensitivity=" + String (sensitivity)
                             + " ownerPid=" + String (ownerPid)
                             + " hasParent=" + String (parent != nullptr ? "yes" : "no")
                             + " hasDisplay=" + String (display != nullptr ? "yes" : "no")
                             + " contextFocusMatches=" + String (contextFocus == nativeWindow ? "yes" : "no")
                             + " contextFocusId=" + String (contextFocusId)
                             + " contextPointerFocusMatches=" + String (contextPointerFocus == nativeWindow ? "yes" : "no")
                             + " contextPointerFocusId=" + String (contextPointerFocusId)
                             + " contextMTouchFocusMatches=" + String (contextMTouchFocus == nativeWindow ? "yes" : "no")
                             + " contextMTouchFocusId=" + String (contextMTouchFocusId)
                             + " groupFocusMatches=" + String (groupFocus == nativeWindow ? "yes" : "no")
                             + " groupFocusId=" + String (groupFocusId)
                             + " ownWindowId=" + String (ownWindowId)
                             + " group=" + (groupName.isNotEmpty() ? groupName : "<none>")
                             + " class=" + (className.isNotEmpty() ? className : "<none>")
                             + " id=" + (idName.isNotEmpty() ? idName : "<none>")
                             + " idString=" + (idString.isNotEmpty() ? idString : "<none>")
                             + " parentId=" + (parentId.isNotEmpty() ? parentId : "<none>")
                             + " managerString=" + (managerString.isNotEmpty() ? managerString : "<empty>"));
        }

        void present (const Image& image, Rectangle<int> imageBounds)
        {
            screen_buffer_t buffer = nullptr;

            if (screen_dequeue_window_render_buffer (&buffer, nativeWindow, 0) != 0 || buffer == nullptr)
            {
                logQnxWindowing ("screen_dequeue_window_render_buffer failed");
                return;
            }

            void* pointer = nullptr;
            int stride = 0;

            if (screen_get_buffer_property_pv (buffer, SCREEN_PROPERTY_POINTER, &pointer) != 0
                || screen_get_buffer_property_iv (buffer, SCREEN_PROPERTY_STRIDE, &stride) != 0
                || pointer == nullptr
                || stride <= 0)
            {
                logQnxWindowing ("Failed to query Screen buffer pointer/stride");
                return;
            }

            const Image::BitmapData bitmapData (image, Image::BitmapData::readOnly);

            for (int y = 0; y < imageBounds.getHeight(); ++y)
            {
                auto* srcLine = bitmapData.getLinePointer (y);
                auto* dstLine = static_cast<uint8*> (pointer) + (ptrdiff_t) y * (ptrdiff_t) stride;
                std::memcpy (dstLine, srcLine, (size_t) imageBounds.getWidth() * 4u);
            }

            const int dirtyRect[4] = { 0, 0, imageBounds.getWidth(), imageBounds.getHeight() };
            const auto postResult = screen_post_window (nativeWindow, buffer, 1, dirtyRect, 0);

            ++presentCount;
            notePresentForFps();

            if (postResult != 0)
            {
                logQnxWindowing ("screen_post_window failed");
                return;
            }

            if (presentCount <= 5 || (presentCount % 60) == 0)
                logQnxWindowing ("Presented frame #" + String (presentCount)
                                 + " size=" + String (imageBounds.getWidth()) + "x" + String (imageBounds.getHeight())
                                 + " stride=" + String (stride));
        }

        // Blit + post only the dirty sub-rectangle (prototype fast path). The single
        // native Screen buffer persists between posts, so prior content is retained
        // and partial updates accumulate correctly.
        void presentRegion (const Image& image, Rectangle<int> dirty)
        {
            screen_buffer_t buffer = nullptr;

            if (screen_dequeue_window_render_buffer (&buffer, nativeWindow, 0) != 0 || buffer == nullptr)
            {
                logQnxWindowing ("screen_dequeue_window_render_buffer failed (region)");
                return;
            }

            void* pointer = nullptr;
            int stride = 0;

            if (screen_get_buffer_property_pv (buffer, SCREEN_PROPERTY_POINTER, &pointer) != 0
                || screen_get_buffer_property_iv (buffer, SCREEN_PROPERTY_STRIDE, &stride) != 0
                || pointer == nullptr
                || stride <= 0)
            {
                logQnxWindowing ("Failed to query Screen buffer pointer/stride (region)");
                return;
            }

            const Image::BitmapData bitmapData (image, Image::BitmapData::readOnly);
            constexpr int bytesPerPixel = 4;

            for (int row = 0; row < dirty.getHeight(); ++row)
            {
                const int y = dirty.getY() + row;
                auto* srcLine = bitmapData.getLinePointer (y) + (ptrdiff_t) dirty.getX() * bytesPerPixel;
                auto* dstLine = static_cast<uint8*> (pointer)
                              + (ptrdiff_t) y * (ptrdiff_t) stride
                              + (ptrdiff_t) dirty.getX() * bytesPerPixel;
                std::memcpy (dstLine, srcLine, (size_t) dirty.getWidth() * bytesPerPixel);
            }

            // Screen dirty rects are [x1, y1, x2, y2].
            const int dirtyRect[4] = { dirty.getX(), dirty.getY(), dirty.getRight(), dirty.getBottom() };
            const auto postResult = screen_post_window (nativeWindow, buffer, 1, dirtyRect, 0);

            ++presentCount;
            notePresentForFps();

            if (postResult != 0)
            {
                logQnxWindowing ("screen_post_window failed (region)");
                return;
            }

            if (presentCount <= 5 || (presentCount % 60) == 0)
                logQnxWindowing ("Presented region #" + String (presentCount)
                                 + " rect=" + dirty.toString()
                                 + " stride=" + String (stride));
        }

        void notePresentForFps()
        {
            if (! shouldLogQnxPresentFps())
                return;

            const auto now = Time::getMillisecondCounterHiRes();

            if (fpsWindowStartMs <= 0.0)
            {
                fpsWindowStartMs = now;
                fpsWindowFrames = 0;
                return;
            }

            ++fpsWindowFrames;
            const auto elapsed = now - fpsWindowStartMs;

            if (elapsed >= 1000.0)
            {
                logQnxWindowing ("QNX_PRESENT_FPS fps=" + String (1000.0 * (double) fpsWindowFrames / elapsed, 1)
                                 + " frames=" + String (fpsWindowFrames)
                                 + " windowMs=" + String (elapsed, 1)
                                 + " mode=" + String (shouldUseFastQnxPresent() ? "fast" : "full"));
                fpsWindowStartMs = now;
                fpsWindowFrames = 0;
            }
        }

        Rectangle<int> bounds { component.getBounds().isEmpty() ? Rectangle<int> (0, 0, 1, 1)
                                                                 : component.getBounds() };
        Rectangle<int> pendingRepaintArea;
        Image backingImage;                 // reused across frames in fast-present mode
        double fpsWindowStartMs = 0.0;      // JUCE_QNX_LOG_FPS accounting
        int fpsWindowFrames = 0;
        Point<int> bufferSize { 0, 0 };
        String title;
        screen_context_t screenContext = nullptr;
        screen_window_t nativeWindow = nullptr;
        Rectangle<int> primaryDisplayBounds;
        screen_group_t windowGroup = nullptr;
        screen_session_t pointerSession = nullptr;
        screen_session_t mtouchSession = nullptr;
        screen_session_t keyboardSession = nullptr;
        void* attachedExternally = nullptr;
        bool joinedActiveWindowGroup = false;
        String managerString;
        bool hasRequestedWindowManagement = false;
        int lastLoggedStatus = -1;
        bool usingSharedContext = false;
        float alpha = 1.0f;
        bool isVisible = false;
        bool minimised = false;
        bool fullScreen = false;
        bool focused = false;
        bool isAlwaysOnTop = false;
        int nativeZOrder = getNextQnxWindowZOrder();
        bool isPerformingRepaint = false;
        bool windowBuffersCreated = false;
        int repaintDispatchCount = 0;
        int presentCount = 0;
        int pointerEventCount = 0;
        int keyboardEventCount = 0;
        int sessionStateLogCount = 0;
        struct ActiveTouchContact
        {
            int stableTouchId = 0;
            int rawTouchId = -1;
            Point<float> position;
        };

        std::map<int, ActiveTouchContact> activeTouchContacts;
        int nextStableTouchId = 1;
        static inline MultiTouchMapper<int> currentTouches;
        std::shared_ptr<PeerState> peerState = std::make_shared<PeerState>();
        TimedCallback repaintTimer { [this]() { dispatchDeferredRepaints(); } };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (QnxComponentPeer)
    };
}

ComponentPeer* Component::createNewPeer (int styleFlags, void* nativeWindowToAttachTo)
{
    return new QnxComponentPeer (*this, styleFlags, nativeWindowToAttachTo);
}

bool Desktop::canUseSemiTransparentWindows() noexcept
{
    return true;
}

class Desktop::NativeDarkModeChangeDetectorImpl
{
public:
    bool isDarkModeEnabled() const noexcept { return false; }
};

std::unique_ptr<Desktop::NativeDarkModeChangeDetectorImpl> Desktop::createNativeDarkModeChangeDetectorImpl()
{
    return std::make_unique<NativeDarkModeChangeDetectorImpl>();
}

bool Desktop::isDarkModeActive() const
{
    return nativeDarkModeChangeDetectorImpl != nullptr && nativeDarkModeChangeDetectorImpl->isDarkModeEnabled();
}

JUCE_API bool JUCE_CALLTYPE Process::isForegroundProcess()    { return true; }
JUCE_API void JUCE_CALLTYPE Process::makeForegroundProcess()  {}
JUCE_API void JUCE_CALLTYPE Process::hide()                   {}

void Desktop::setScreenSaverEnabled (bool isEnabled)
{
    qnxScreenSaverEnabled() = isEnabled;
}

bool Desktop::isScreenSaverEnabled()
{
    return qnxScreenSaverEnabled();
}

double Desktop::getDefaultMasterScale()                             { return 1.0; }
Desktop::DisplayOrientation Desktop::getCurrentOrientation() const  { return upright; }
void Desktop::allowedOrientationsChanged()                          {}

bool detail::MouseInputSourceList::addSource()
{
    if (sources.isEmpty())
    {
        addSource (0, MouseInputSource::InputSourceType::mouse);
        return true;
    }

    return false;
}

bool detail::MouseInputSourceList::canUseTouch() const
{
    return true;
}

Point<float> MouseInputSource::getCurrentRawMousePosition()
{
    return qnxMousePosition();
}

void MouseInputSource::setRawMousePosition (Point<float> newPosition)
{
    qnxMousePosition() = newPosition;
}

class MouseCursor::PlatformSpecificHandle
{
public:
    explicit PlatformSpecificHandle (const MouseCursor::StandardCursorType) {}
    explicit PlatformSpecificHandle (const detail::CustomMouseCursorInfo&)  {}

    static void showInWindow (PlatformSpecificHandle*, ComponentPeer*)      {}
};

namespace
{
    constexpr int qnxExtendedKeyModifier = 0x10000;
}

const int KeyPress::spaceKey              = 0x20;
const int KeyPress::returnKey             = 0x0d;
const int KeyPress::escapeKey             = 0x1b;
const int KeyPress::backspaceKey          = 0x08;
const int KeyPress::leftKey               = qnxExtendedKeyModifier + 1;
const int KeyPress::rightKey              = qnxExtendedKeyModifier + 2;
const int KeyPress::upKey                 = qnxExtendedKeyModifier + 3;
const int KeyPress::downKey               = qnxExtendedKeyModifier + 4;
const int KeyPress::pageUpKey             = qnxExtendedKeyModifier + 5;
const int KeyPress::pageDownKey           = qnxExtendedKeyModifier + 6;
const int KeyPress::endKey                = qnxExtendedKeyModifier + 7;
const int KeyPress::homeKey               = qnxExtendedKeyModifier + 8;
const int KeyPress::insertKey             = qnxExtendedKeyModifier + 9;
const int KeyPress::deleteKey             = 0x7f;
const int KeyPress::tabKey                = 0x09;
const int KeyPress::F1Key                 = qnxExtendedKeyModifier + 20;
const int KeyPress::F2Key                 = qnxExtendedKeyModifier + 21;
const int KeyPress::F3Key                 = qnxExtendedKeyModifier + 22;
const int KeyPress::F4Key                 = qnxExtendedKeyModifier + 23;
const int KeyPress::F5Key                 = qnxExtendedKeyModifier + 24;
const int KeyPress::F6Key                 = qnxExtendedKeyModifier + 25;
const int KeyPress::F7Key                 = qnxExtendedKeyModifier + 26;
const int KeyPress::F8Key                 = qnxExtendedKeyModifier + 27;
const int KeyPress::F9Key                 = qnxExtendedKeyModifier + 28;
const int KeyPress::F10Key                = qnxExtendedKeyModifier + 29;
const int KeyPress::F11Key                = qnxExtendedKeyModifier + 30;
const int KeyPress::F12Key                = qnxExtendedKeyModifier + 31;
const int KeyPress::F13Key                = qnxExtendedKeyModifier + 32;
const int KeyPress::F14Key                = qnxExtendedKeyModifier + 33;
const int KeyPress::F15Key                = qnxExtendedKeyModifier + 34;
const int KeyPress::F16Key                = qnxExtendedKeyModifier + 35;
const int KeyPress::F17Key                = qnxExtendedKeyModifier + 36;
const int KeyPress::F18Key                = qnxExtendedKeyModifier + 37;
const int KeyPress::F19Key                = qnxExtendedKeyModifier + 38;
const int KeyPress::F20Key                = qnxExtendedKeyModifier + 39;
const int KeyPress::F21Key                = qnxExtendedKeyModifier + 40;
const int KeyPress::F22Key                = qnxExtendedKeyModifier + 41;
const int KeyPress::F23Key                = qnxExtendedKeyModifier + 42;
const int KeyPress::F24Key                = qnxExtendedKeyModifier + 43;
const int KeyPress::F25Key                = qnxExtendedKeyModifier + 44;
const int KeyPress::F26Key                = qnxExtendedKeyModifier + 45;
const int KeyPress::F27Key                = qnxExtendedKeyModifier + 46;
const int KeyPress::F28Key                = qnxExtendedKeyModifier + 47;
const int KeyPress::F29Key                = qnxExtendedKeyModifier + 48;
const int KeyPress::F30Key                = qnxExtendedKeyModifier + 49;
const int KeyPress::F31Key                = qnxExtendedKeyModifier + 50;
const int KeyPress::F32Key                = qnxExtendedKeyModifier + 51;
const int KeyPress::F33Key                = qnxExtendedKeyModifier + 52;
const int KeyPress::F34Key                = qnxExtendedKeyModifier + 53;
const int KeyPress::F35Key                = qnxExtendedKeyModifier + 54;
const int KeyPress::numberPad0            = qnxExtendedKeyModifier + 60;
const int KeyPress::numberPad1            = qnxExtendedKeyModifier + 61;
const int KeyPress::numberPad2            = qnxExtendedKeyModifier + 62;
const int KeyPress::numberPad3            = qnxExtendedKeyModifier + 63;
const int KeyPress::numberPad4            = qnxExtendedKeyModifier + 64;
const int KeyPress::numberPad5            = qnxExtendedKeyModifier + 65;
const int KeyPress::numberPad6            = qnxExtendedKeyModifier + 66;
const int KeyPress::numberPad7            = qnxExtendedKeyModifier + 67;
const int KeyPress::numberPad8            = qnxExtendedKeyModifier + 68;
const int KeyPress::numberPad9            = qnxExtendedKeyModifier + 69;
const int KeyPress::numberPadAdd          = qnxExtendedKeyModifier + 70;
const int KeyPress::numberPadSubtract     = qnxExtendedKeyModifier + 71;
const int KeyPress::numberPadMultiply     = qnxExtendedKeyModifier + 72;
const int KeyPress::numberPadDivide       = qnxExtendedKeyModifier + 73;
const int KeyPress::numberPadSeparator    = qnxExtendedKeyModifier + 74;
const int KeyPress::numberPadDecimalPoint = qnxExtendedKeyModifier + 75;
const int KeyPress::numberPadEquals       = qnxExtendedKeyModifier + 76;
const int KeyPress::numberPadDelete       = qnxExtendedKeyModifier + 77;
const int KeyPress::playKey               = qnxExtendedKeyModifier + 80;
const int KeyPress::stopKey               = qnxExtendedKeyModifier + 81;
const int KeyPress::fastForwardKey        = qnxExtendedKeyModifier + 82;
const int KeyPress::rewindKey             = qnxExtendedKeyModifier + 83;

bool KeyPress::isKeyCurrentlyDown (int)
{
    return false;
}

bool DragAndDropContainer::performExternalDragDropOfFiles (const StringArray&, bool, Component*, std::function<void()>)
{
    return false;
}

bool DragAndDropContainer::performExternalDragDropOfText (const String&, Component*, std::function<void()>)
{
    return false;
}

void SystemClipboard::copyTextToClipboard (const String& clipText)
{
    qnxClipboardStorage() = clipText;
}

String SystemClipboard::getTextFromClipboard()
{
    return qnxClipboardStorage();
}

void LookAndFeel::playAlertSound()
{
    std::cout << "\a" << std::flush;
}

Image detail::WindowingHelpers::createIconForFile (const File&)
{
    return {};
}

Image createSnapshotOfNativeWindow (void*)
{
    return {};
}

void Desktop::setKioskComponent (Component*, bool, bool)
{
}

void Displays::findDisplays (const Desktop&)
{
    displays.clearQuick();

    auto displayArea = Rectangle<int> (0, 0, 1920, 1080);
    auto* context = acquireSharedQnxScreenContext();

    if (context != nullptr)
    {
        displayArea = getPrimaryQnxDisplayBounds (context);
        releaseSharedQnxScreenContext();
    }

    Display display;
    display.isMain = true;
    display.logicalBounds = displayArea.toFloat();
    display.userBounds = display.logicalBounds;
    display.totalArea = displayArea;
    display.userArea = display.totalArea;
    display.scale = 1.0;
    display.dpi = 96.0;
    displays.add (display);
}

bool FileChooser::isPlatformDialogAvailable()
{
    return false;
}

std::shared_ptr<FileChooser::Pimpl> FileChooser::showPlatformDialog (FileChooser&, int, FilePreviewComponent*)
{
    return {};
}

} // namespace juce

namespace juce::detail
{

std::unique_ptr<ScopedMessageBoxInterface> ScopedMessageBoxInterface::create (const MessageBoxOptions& options)
{
    class MessageBox final : public ScopedMessageBoxInterface
    {
    public:
        explicit MessageBox (const MessageBoxOptions& opts)
            : inner (detail::AlertWindowHelpers::create (opts)),
              numButtons (opts.getNumButtons())
        {
        }

        void runAsync (std::function<void (int)> fn) override
        {
            inner->runAsync ([fn, n = numButtons] (int result)
                             {
                                 fn (map (result, n));
                             });
        }

        int runSync() override
        {
            return map (inner->runSync(), numButtons);
        }

        void close() override
        {
            inner->close();
        }

    private:
        static int map (int button, int numButtons)
        {
            if (numButtons <= 0)
                return 0;

            return (button + numButtons - 1) % numButtons;
        }

        std::unique_ptr<ScopedMessageBoxInterface> inner;
        int numButtons = 0;
    };

    return std::make_unique<MessageBox> (options);
}

} // namespace juce::detail
