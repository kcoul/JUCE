#include <screen/screen.h>

#include <array>
#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#if defined(__QNXNTO__)
 #include <unistd.h>
 #include <libgen.h>
#endif

namespace
{
    std::atomic<bool> keepRunning { true };
    FILE* logFile = nullptr;

    const char* eventTypeToString (int eventType) noexcept
    {
        switch (eventType)
        {
            case SCREEN_EVENT_NONE:            return "NONE";
            case SCREEN_EVENT_CREATE:          return "CREATE";
            case SCREEN_EVENT_PROPERTY:        return "PROPERTY";
            case SCREEN_EVENT_CLOSE:           return "CLOSE";
            case SCREEN_EVENT_INPUT:           return "INPUT";
            case SCREEN_EVENT_POINTER:         return "POINTER";
            case SCREEN_EVENT_KEYBOARD:        return "KEYBOARD";
            case SCREEN_EVENT_USER:            return "USER";
            case SCREEN_EVENT_POST:            return "POST";
            case SCREEN_EVENT_DISPLAY:         return "DISPLAY";
            case SCREEN_EVENT_MANAGER:         return "MANAGER";
            case SCREEN_EVENT_MTOUCH_PRETOUCH: return "MTOUCH_PRETOUCH";
            case SCREEN_EVENT_MTOUCH_TOUCH:    return "MTOUCH_TOUCH";
            case SCREEN_EVENT_MTOUCH_MOVE:     return "MTOUCH_MOVE";
            case SCREEN_EVENT_MTOUCH_RELEASE:  return "MTOUCH_RELEASE";
            default:                           return "UNKNOWN";
        }
    }

    void writeLog (const std::string& line)
    {
        if (logFile == nullptr)
            return;

        std::fputs ((line + "\n").c_str(), logFile);
        std::fflush (logFile);
    }

    void signalHandler (int)
    {
        keepRunning = false;
    }

    std::string getLogPath (const char* argv0)
    {
       #if defined(__QNXNTO__)
        std::array<char, 1024> buffer {};
        std::strncpy (buffer.data(), argv0 != nullptr ? argv0 : ".", buffer.size() - 1);
        const auto* dir = dirname (buffer.data());
        return std::string (dir != nullptr ? dir : ".") + "/QNXMTouchProbe.log";
       #else
        (void) argv0;
        return "QNXMTouchProbe.log";
       #endif
    }
}

int main (int argc, char* argv[])
{
    const auto logPath = getLogPath (argc > 0 ? argv[0] : nullptr);
    logFile = std::fopen (logPath.c_str(), "a");

    if (logFile == nullptr)
        return 1;

    writeLog ("\n**********************************************************");
    writeLog ("QNX MTouch Probe");
   #if defined(JUCE_QNX_HELLO_WORLD_BUILD_VERSION)
    writeLog (std::string ("Build version: ") + JUCE_QNX_HELLO_WORLD_BUILD_VERSION);
   #endif
   #if defined(__QNXNTO__)
    writeLog ("Process PID: " + std::to_string ((int) getpid()));
   #endif

    std::signal (SIGINT, signalHandler);
    std::signal (SIGTERM, signalHandler);

    screen_context_t context = nullptr;

    if (screen_create_context (&context, SCREEN_APPLICATION_CONTEXT) != 0)
    {
        writeLog ("screen_create_context failed errno=" + std::to_string (errno));
        return 1;
    }

    writeLog ("screen_create_context succeeded");

    int displayCount = 0;
    screen_get_context_property_iv (context, SCREEN_PROPERTY_DISPLAY_COUNT, &displayCount);
    writeLog ("displayCount=" + std::to_string (displayCount));

    if (displayCount <= 0)
    {
        writeLog ("No displays available");
        screen_destroy_context (context);
        return 1;
    }

    std::vector<screen_display_t> displays ((size_t) displayCount);
    if (screen_get_context_property_pv (context, SCREEN_PROPERTY_DISPLAYS, reinterpret_cast<void**> (displays.data())) != 0)
    {
        writeLog ("screen_get_context_property_pv(DISPLAYS) failed errno=" + std::to_string (errno));
        screen_destroy_context (context);
        return 1;
    }

    auto* primaryDisplay = displays.front();
    int displaySize[2] { 0, 0 };
    screen_get_display_property_iv (primaryDisplay, SCREEN_PROPERTY_SIZE, displaySize);
    writeLog ("primaryDisplay size=" + std::to_string (displaySize[0]) + "x" + std::to_string (displaySize[1]));

    screen_session_t mtouchSession = nullptr;
    if (screen_create_session_type (&mtouchSession, context, SCREEN_EVENT_MTOUCH_TOUCH) != 0)
    {
        writeLog ("screen_create_session_type(MTOUCH) failed errno=" + std::to_string (errno));
        screen_destroy_context (context);
        return 1;
    }

    writeLog ("screen_create_session_type(MTOUCH) succeeded");

    auto* displayHandle = reinterpret_cast<void*> (primaryDisplay);
    if (screen_set_session_property_pv (mtouchSession, SCREEN_PROPERTY_DISPLAY, &displayHandle) != 0)
        writeLog ("screen_set_session_property_pv(DISPLAY) failed errno=" + std::to_string (errno));
    else
        writeLog ("Attached mtouch session to primary display");

    const int mode = SCREEN_INPUT_MODE_RAW;
    if (screen_set_session_property_iv (mtouchSession, SCREEN_PROPERTY_MODE, &mode) != 0)
        writeLog ("screen_set_session_property_iv(MODE=RAW) failed errno=" + std::to_string (errno));
    else
        writeLog ("Set mtouch session mode=SCREEN_INPUT_MODE_RAW");

    int deviceCount = 0;
    screen_get_context_property_iv (context, SCREEN_PROPERTY_DEVICE_COUNT, &deviceCount);
    writeLog ("deviceCount=" + std::to_string (deviceCount));

    if (deviceCount > 0)
    {
        std::vector<screen_device_t> devices ((size_t) deviceCount);

        if (screen_get_context_property_pv (context, SCREEN_PROPERTY_DEVICES, reinterpret_cast<void**> (devices.data())) == 0)
        {
            for (auto device : devices)
            {
                if (device == nullptr)
                    continue;

                int deviceType = 0;
                screen_get_device_property_iv (device, SCREEN_PROPERTY_TYPE, &deviceType);
                writeLog ("deviceType=" + std::to_string (deviceType));

                if (deviceType == SCREEN_EVENT_MTOUCH_TOUCH)
                {
                    auto* sessionHandle = reinterpret_cast<void*> (mtouchSession);
                    if (screen_set_device_property_pv (device, SCREEN_PROPERTY_SESSION, &sessionHandle) != 0)
                        writeLog ("screen_set_device_property_pv(SESSION) failed errno=" + std::to_string (errno));
                    else
                        writeLog ("Bound mtouch device to mtouch session");
                }
            }
        }
        else
        {
            writeLog ("screen_get_context_property_pv(DEVICES) failed errno=" + std::to_string (errno));
        }
    }

    screen_flush_context (context, 0);
    writeLog ("Waiting for Screen events");

    screen_event_t event = nullptr;
    if (screen_create_event (&event) != 0)
    {
        writeLog ("screen_create_event failed errno=" + std::to_string (errno));
        screen_destroy_session (mtouchSession);
        screen_destroy_context (context);
        return 1;
    }

    while (keepRunning)
    {
        if (screen_get_event (context, event, ~0ULL) != 0)
        {
            writeLog ("screen_get_event failed errno=" + std::to_string (errno));
            continue;
        }

        int eventType = SCREEN_EVENT_NONE;
        screen_get_event_property_iv (event, SCREEN_PROPERTY_TYPE, &eventType);

        if (eventType == SCREEN_EVENT_NONE)
            continue;

        int position[2] { 0, 0 };
        int touchId = -1;
        screen_get_event_property_iv (event, SCREEN_PROPERTY_POSITION, position);
        screen_get_event_property_iv (event, SCREEN_PROPERTY_TOUCH_ID, &touchId);

        writeLog (std::string ("event type=")
                  + eventTypeToString (eventType)
                  + " position=" + std::to_string (position[0]) + "," + std::to_string (position[1])
                  + " touchId=" + std::to_string (touchId));
    }

    writeLog ("Shutting down");
    screen_destroy_event (event);
    screen_destroy_session (mtouchSession);
    screen_destroy_context (context);
    std::fclose (logFile);
    return 0;
}
