#include <screen/screen.h>

#include <cstring>

namespace juce
{

namespace
{
    String& qnxClipboardStorage()
    {
        static String text;
        return text;
    }

    Point<float>& qnxMousePosition()
    {
        static Point<float> pos;
        return pos;
    }

    bool& qnxScreenSaverEnabled()
    {
        static bool enabled = true;
        return enabled;
    }

    class QnxComponentPeer final : public ComponentPeer
    {
    public:
        QnxComponentPeer (Component& comp, int windowStyleFlags, void* nativeWindowToAttachTo)
            : ComponentPeer (comp, windowStyleFlags),
              nativeWindow (reinterpret_cast<screen_window_t> (nativeWindowToAttachTo)),
              attachedExternally (nativeWindowToAttachTo)
        {
            getNativeRealtimeModifiers = []() { return ModifierKeys::currentModifiers; };

            if (auto componentBounds = component.getBounds(); ! componentBounds.isEmpty())
                bounds = componentBounds;

            if (nativeWindow != nullptr)
                return;

            if (screen_create_context (&screenContext, SCREEN_APPLICATION_CONTEXT) != 0)
                return;

            if (screen_create_window (&nativeWindow, screenContext) != 0)
            {
                screen_destroy_context (screenContext);
                screenContext = nullptr;
                return;
            }

            const int usage = SCREEN_USAGE_NATIVE | SCREEN_USAGE_READ | SCREEN_USAGE_WRITE
                            | SCREEN_USAGE_OPENGL_ES2 | SCREEN_USAGE_OPENGL_ES3;
            screen_set_window_property_iv (nativeWindow, SCREEN_PROPERTY_USAGE, &usage);

            const int format = SCREEN_FORMAT_BGRA8888;
            screen_set_window_property_iv (nativeWindow, SCREEN_PROPERTY_FORMAT, &format);

            const int transparency = component.isOpaque() ? SCREEN_TRANSPARENCY_NONE
                                                          : SCREEN_TRANSPARENCY_SOURCE_OVER;
            screen_set_window_property_iv (nativeWindow, SCREEN_PROPERTY_TRANSPARENCY, &transparency);

            updateWindowState();
        }

        ~QnxComponentPeer() override
        {
            if (ownsWindow())
            {
                destroyWindowBuffers();

                if (nativeWindow != nullptr)
                    screen_destroy_window (nativeWindow);

                if (screenContext != nullptr)
                    screen_destroy_context (screenContext);
            }
        }

        void* getNativeHandle() const override                             { return nativeWindow; }
        void setVisible (bool shouldBeVisible) override
        {
            isVisible = shouldBeVisible;
            updateWindowState();

            if (shouldBeVisible)
                repaint (component.getLocalBounds());
        }
        void setTitle (const String& newTitle) override                    { title = newTitle; }

        void setBounds (const Rectangle<int>& newBounds, bool isNowFullScreen) override
        {
            bounds = newBounds.withSize (jmax (1, newBounds.getWidth()),
                                         jmax (1, newBounds.getHeight()));
            fullScreen = isNowFullScreen;
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

            auto imageBounds = bounds.withZeroOrigin();

            if (imageBounds.isEmpty() || ! ensureWindowReady())
                return;

            Image temp (Image::ARGB,
                        imageBounds.getWidth(),
                        imageBounds.getHeight(),
                        true);

            LowLevelGraphicsSoftwareRenderer renderer (temp);
            handlePaint (renderer);
            present (temp, imageBounds);
            pendingRepaintArea = {};
        }

        void setAlpha (float newAlpha) override                           { alpha = newAlpha; }
        StringArray getAvailableRenderingEngines() override               { return { "Software Renderer" }; }
        double getPlatformScaleFactor() const noexcept override           { return 1.0; }

    private:
        bool ownsWindow() const noexcept                                  { return attachedExternally == nullptr; }

        bool ensureWindowReady()
        {
            if (nativeWindow == nullptr)
                return false;

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
                return false;

            windowBuffersCreated = true;
            return true;
        }

        void destroyWindowBuffers()
        {
            if (windowBuffersCreated && nativeWindow != nullptr)
            {
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

            screen_set_window_property_iv (nativeWindow, SCREEN_PROPERTY_POSITION, position);
            screen_set_window_property_iv (nativeWindow, SCREEN_PROPERTY_SIZE, size);
            screen_set_window_property_iv (nativeWindow, SCREEN_PROPERTY_VISIBLE, &visible);
        }

        void present (const Image& image, Rectangle<int> imageBounds)
        {
            screen_buffer_t buffer = nullptr;

            if (screen_dequeue_window_render_buffer (&buffer, nativeWindow, 0) != 0 || buffer == nullptr)
                return;

            void* pointer = nullptr;
            int stride = 0;

            if (screen_get_buffer_property_pv (buffer, SCREEN_PROPERTY_POINTER, &pointer) != 0
                || screen_get_buffer_property_iv (buffer, SCREEN_PROPERTY_STRIDE, &stride) != 0
                || pointer == nullptr
                || stride <= 0)
            {
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
            screen_post_window (nativeWindow, buffer, 1, dirtyRect, 0);
        }

        Rectangle<int> bounds { component.getBounds().isEmpty() ? Rectangle<int> (0, 0, 1, 1)
                                                                 : component.getBounds() };
        Rectangle<int> pendingRepaintArea;
        Point<int> bufferSize { 0, 0 };
        String title;
        screen_context_t screenContext = nullptr;
        screen_window_t nativeWindow = nullptr;
        void* attachedExternally = nullptr;
        float alpha = 1.0f;
        bool isVisible = false;
        bool minimised = false;
        bool fullScreen = false;
        bool focused = false;
        bool isAlwaysOnTop = false;
        bool windowBuffersCreated = false;

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
    return false;
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

    Display display;
    display.isMain = true;
    display.totalArea = { 0, 0, 1280, 720 };
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
