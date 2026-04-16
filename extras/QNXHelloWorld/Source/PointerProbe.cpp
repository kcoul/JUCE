#include <screen/screen.h>

#include <array>
#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <string>
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
        return std::string (dir != nullptr ? dir : ".") + "/QNXPointerProbe.log";
       #else
        (void) argv0;
        return "QNXPointerProbe.log";
       #endif
    }

    int getWindowId (screen_window_t window)
    {
        int id = -1;
        if (window != nullptr)
            screen_get_window_property_iv (window, SCREEN_PROPERTY_ID, &id);
        return id;
    }
}

int main (int argc, char* argv[])
{
    const auto logPath = getLogPath (argc > 0 ? argv[0] : nullptr);
    logFile = std::fopen (logPath.c_str(), "a");

    if (logFile == nullptr)
        return 1;

    writeLog ("\n**********************************************************");
    writeLog ("QNX Pointer Probe");
   #if defined(JUCE_QNX_HELLO_WORLD_BUILD_VERSION)
    writeLog (std::string ("Build version: ") + JUCE_QNX_HELLO_WORLD_BUILD_VERSION);
   #endif
   #if defined(__QNXNTO__)
    writeLog ("Process PID: " + std::to_string ((int) getpid()));
   #endif

    std::signal (SIGINT, signalHandler);
    std::signal (SIGTERM, signalHandler);

    screen_context_t context = nullptr;
    screen_window_t window = nullptr;
    screen_event_t event = nullptr;

    if (screen_create_context (&context, SCREEN_APPLICATION_CONTEXT) != 0)
    {
        writeLog ("screen_create_context failed errno=" + std::to_string (errno));
        return 1;
    }

    writeLog ("screen_create_context succeeded");

    if (screen_create_window (&window, context) != 0)
    {
        writeLog ("screen_create_window failed errno=" + std::to_string (errno));
        screen_destroy_context (context);
        return 1;
    }

    writeLog ("screen_create_window succeeded");

    int displayCount = 0;
    screen_get_context_property_iv (context, SCREEN_PROPERTY_DISPLAY_COUNT, &displayCount);
    writeLog ("displayCount=" + std::to_string (displayCount));

    if (displayCount > 0)
    {
        std::vector<screen_display_t> displays ((size_t) displayCount);

        if (screen_get_context_property_pv (context, SCREEN_PROPERTY_DISPLAYS, reinterpret_cast<void**> (displays.data())) == 0)
        {
            auto* primaryDisplay = displays.front();
            auto* displayHandle = reinterpret_cast<void*> (primaryDisplay);
            int displaySize[2] { 0, 0 };
            screen_get_display_property_iv (primaryDisplay, SCREEN_PROPERTY_SIZE, displaySize);
            writeLog ("primaryDisplay size=" + std::to_string (displaySize[0]) + "x" + std::to_string (displaySize[1]));

            if (screen_set_window_property_pv (window, SCREEN_PROPERTY_DISPLAY, &displayHandle) == 0)
                writeLog ("Attached pointer probe window to primary display");
            else
                writeLog ("screen_set_window_property_pv(DISPLAY) failed errno=" + std::to_string (errno));

            const int size[2] { displaySize[0], displaySize[1] };
            const int pos[2] { 0, 0 };
            const int visible = 1;
            const int usage = SCREEN_USAGE_NATIVE | SCREEN_USAGE_READ | SCREEN_USAGE_WRITE;
            const int format = SCREEN_FORMAT_RGBA8888;

            screen_set_window_property_iv (window, SCREEN_PROPERTY_USAGE, &usage);
            screen_set_window_property_iv (window, SCREEN_PROPERTY_FORMAT, &format);
            screen_set_window_property_iv (window, SCREEN_PROPERTY_SIZE, size);
            screen_set_window_property_iv (window, SCREEN_PROPERTY_POSITION, pos);
            screen_set_window_property_iv (window, SCREEN_PROPERTY_VISIBLE, &visible);
        }
    }

    {
        const char* idString = "QNXPointerProbe";
        screen_set_window_property_cv (window, SCREEN_PROPERTY_ID_STRING, (int) std::strlen (idString) + 1, idString);
    }

    screen_flush_context (context, 0);
    writeLog ("windowId=" + std::to_string (getWindowId (window)));

    if (screen_create_event (&event) != 0)
    {
        writeLog ("screen_create_event failed errno=" + std::to_string (errno));
        screen_destroy_window (window);
        screen_destroy_context (context);
        return 1;
    }

    writeLog ("Waiting for Screen events");

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
        int sourcePosition[2] { 0, 0 };
        int buttons = 0;
        int flags = 0;
        screen_window_t targetWindow = nullptr;
        screen_get_event_property_iv (event, SCREEN_PROPERTY_POSITION, position);
        screen_get_event_property_iv (event, SCREEN_PROPERTY_SOURCE_POSITION, sourcePosition);
        screen_get_event_property_iv (event, SCREEN_PROPERTY_BUTTONS, &buttons);
        screen_get_event_property_iv (event, SCREEN_PROPERTY_FLAGS, &flags);
        screen_get_event_property_pv (event, SCREEN_PROPERTY_WINDOW, reinterpret_cast<void**> (&targetWindow));

        writeLog (std::string ("event type=")
                  + eventTypeToString (eventType)
                  + " position=" + std::to_string (position[0]) + "," + std::to_string (position[1])
                  + " sourcePosition=" + std::to_string (sourcePosition[0]) + "," + std::to_string (sourcePosition[1])
                  + " buttons=" + std::to_string (buttons)
                  + " flags=" + std::to_string (flags)
                  + " targetWindowId=" + std::to_string (getWindowId (targetWindow)));
    }

    writeLog ("Shutting down");
    screen_destroy_event (event);
    screen_destroy_window (window);
    screen_destroy_context (context);
    std::fclose (logFile);
    return 0;
}
