/*
  ==============================================================================

   This file is part of the JUCE framework.
   Copyright (c) Raw Material Software Limited

   JUCE is an open source framework subject to commercial or open source
   licensing.

   By downloading, installing, or using the JUCE framework, or combining the
   JUCE framework with any other source code, object code, content or any other
   copyrightable work, you agree to the terms of the JUCE End User Licence
   Agreement, and all incorporated terms including the JUCE Privacy Policy and
   the JUCE Website Terms of Service, as applicable, which will bind you. If you
   do not agree to the terms of these agreements, we will not license the JUCE
   framework to you, and you must discontinue the installation or download
   process and cease use of the JUCE framework.

   JUCE End User Licence Agreement: https://juce.com/legal/juce-8-licence/
   JUCE Privacy Policy: https://juce.com/juce-privacy-policy
   JUCE Website Terms of Service: https://juce.com/juce-website-terms-of-service/

   Or:

   You may also use this code under the terms of the AGPLv3:
   https://www.gnu.org/licenses/agpl-3.0.en.html

   THE JUCE FRAMEWORK IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL
   WARRANTIES, WHETHER EXPRESSED OR IMPLIED, INCLUDING WARRANTY OF
   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, ARE DISCLAIMED.

  ==============================================================================
*/

namespace juce
{

static void logQnxOpenGL (const String& message)
{
    Logger::writeToLog ("[QNX OpenGL] " + message);
}

class OpenGLContext::NativeContext
{
public:
    NativeContext (Component& comp,
                   const OpenGLPixelFormat& pixelFormat,
                   void* contextToShareWithIn,
                   bool useMultisamplingIn,
                   OpenGLVersion version)
        : component (comp),
          contextToShareWith (reinterpret_cast<EGLContext> (contextToShareWithIn))
    {
        auto* peer = component.getPeer();

        if (peer == nullptr)
            return;

        nativeWindow = reinterpret_cast<EGLNativeWindowType> (peer->getNativeHandle());

        if (nativeWindow == EGLNativeWindowType{})
        {
            logQnxOpenGL ("Native window handle is null");
            return;
        }

        if (screen_get_window_property_pv (reinterpret_cast<screen_window_t> (nativeWindow),
                                           SCREEN_PROPERTY_CONTEXT,
                                           reinterpret_cast<void**> (&nativeDisplay)) != 0
            || nativeDisplay == EGLNativeDisplayType{})
        {
            logQnxOpenGL ("Failed to query SCREEN_PROPERTY_CONTEXT for native window");
            nativeDisplay = EGLNativeDisplayType{};
            return;
        }

        if (screen_get_window_property_pv (reinterpret_cast<screen_window_t> (nativeWindow),
                                           SCREEN_PROPERTY_DISPLAY,
                                           reinterpret_cast<void**> (&nativeScreenDisplay)) != 0
            || nativeScreenDisplay == nullptr)
        {
            logQnxOpenGL ("Failed to query SCREEN_PROPERTY_DISPLAY for native window");
            nativeScreenDisplay = nullptr;
        }

        if (! initEGLDisplay (pixelFormat, useMultisamplingIn, version))
            return;

        logQnxOpenGL ("Initialised EGL display for native Screen window");
        hasInitialised = true;
    }

    ~NativeContext()
    {
        deactivateCurrentContext();
    }

    InitResult initialiseOnRenderThread (OpenGLContext& c)
    {
        const ScopedLock lock (mutex);

        if (! hasInitialised)
            return InitResult::fatal;

        if (surface != EGL_NO_SURFACE)
            return InitResult::success;

        auto surfaceAttributes = std::array<EGLint, 1> { EGL_NONE };
        surface = eglCreateWindowSurface (display, config, nativeWindow, surfaceAttributes.data());

        if (surface == EGL_NO_SURFACE)
        {
            logQnxOpenGL ("eglCreateWindowSurface failed, error=0x" + String::toHexString ((int) eglGetError()));
            return InitResult::fatal;
        }

        std::array<EGLint, 5> contextAttributes
        {
            EGL_CONTEXT_CLIENT_VERSION,
            getContextVersion (versionRequired),
            EGL_NONE,
            EGL_NONE,
            EGL_NONE
        };

        context = eglCreateContext (display, config, contextToShareWith, contextAttributes.data());

        if (context == EGL_NO_CONTEXT)
        {
            logQnxOpenGL ("eglCreateContext failed, error=0x" + String::toHexString ((int) eglGetError()));
            destroySurface();
            return InitResult::fatal;
        }

        logQnxOpenGL ("Created EGL context and window surface");
        juceContext = &c;
        return InitResult::success;
    }

    void shutdownOnRenderThread()
    {
        const ScopedLock lock (mutex);
        juceContext = nullptr;
        deactivateCurrentContext();
        destroyContext();
        destroySurface();
    }

    bool makeActive() const noexcept
    {
        const ScopedLock lock (mutex);

        if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT)
        {
            logQnxOpenGL ("makeActive skipped because surface/context is not ready");
            return false;
        }

        if (eglMakeCurrent (display, surface, surface, context) == EGL_TRUE)
            return true;

        logQnxOpenGL ("eglMakeCurrent failed, error=0x" + String::toHexString ((int) eglGetError()));
        return false;
    }

    bool isActive() const noexcept
    {
        const ScopedLock lock (mutex);
        return context != EGL_NO_CONTEXT && eglGetCurrentContext() == context;
    }

    static void deactivateCurrentContext()
    {
        if (sharedDisplay != EGL_NO_DISPLAY)
            eglMakeCurrent (sharedDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }

    void swapBuffers()
    {
        const ScopedLock lock (mutex);

        if (surface != EGL_NO_SURFACE)
        {
            if (eglSwapBuffers (display, surface) != EGL_TRUE)
            {
                logQnxOpenGL ("eglSwapBuffers failed, error=0x" + String::toHexString ((int) eglGetError()));
            }
            else if (++swapCount <= 5 || (swapCount % 60) == 0)
            {
                logQnxOpenGL ("eglSwapBuffers succeeded #" + String (swapCount));
            }
        }
    }

    void updateWindowPosition() {}

    bool setSwapInterval (int numFramesPerSwap)
    {
        const ScopedLock lock (mutex);

        if (display == EGL_NO_DISPLAY)
        {
            logQnxOpenGL ("setSwapInterval skipped because display is not ready");
            return false;
        }

        if (eglSwapInterval (display, numFramesPerSwap) != EGL_TRUE)
        {
            logQnxOpenGL ("eglSwapInterval failed, error=0x" + String::toHexString ((int) eglGetError()));
            return false;
        }

        swapInterval = numFramesPerSwap;
        logQnxOpenGL ("eglSwapInterval succeeded with interval=" + String (numFramesPerSwap));
        return true;
    }

    int getSwapInterval() const                            { return swapInterval; }
    bool createdOk() const noexcept                        { return hasInitialised; }
    void* getRawContext() const noexcept                   { return context; }
    GLuint getFrameBufferID() const noexcept               { return 0; }

    struct Locker
    {
        explicit Locker (NativeContext& ctx) : lock (ctx.mutex) {}
        const ScopedLock lock;
    };

    void addListener (NativeContextListener&) {}
    void removeListener (NativeContextListener&) {}

private:
    static int getContextVersion (OpenGLVersion version)
    {
        return version == OpenGLVersion::openGL4_3 ? 3 : 2;
    }

    bool tryChooseConfig (const OpenGLPixelFormat& pixelFormat,
                          const std::vector<EGLint>& optionalAttribs)
    {
        std::vector<EGLint> attributes
        {
            EGL_RENDERABLE_TYPE,    EGL_OPENGL_ES2_BIT | EGL_OPENGL_ES3_BIT,
            EGL_SURFACE_TYPE,       EGL_WINDOW_BIT,
            EGL_RED_SIZE,           pixelFormat.redBits,
            EGL_GREEN_SIZE,         pixelFormat.greenBits,
            EGL_BLUE_SIZE,          pixelFormat.blueBits,
            EGL_ALPHA_SIZE,         pixelFormat.alphaBits,
            EGL_DEPTH_SIZE,         pixelFormat.depthBufferBits,
            EGL_STENCIL_SIZE,       pixelFormat.stencilBufferBits
        };

        attributes.insert (attributes.end(), optionalAttribs.begin(), optionalAttribs.end());
        attributes.push_back (EGL_NONE);

        EGLint numConfigs = 0;
        return eglChooseConfig (display, attributes.data(), &config, 1, &numConfigs) == EGL_TRUE
            && numConfigs > 0;
    }

    bool initEGLDisplay (const OpenGLPixelFormat& pixelFormat,
                         bool useMultisamplingIn,
                         OpenGLVersion version)
    {
        versionRequired = version;

        if (sharedDisplay == EGL_NO_DISPLAY)
        {
            if (! tryInitDisplayWithPlatformScreen())
                tryInitDisplayWithLegacyEntryPoint();

            if (sharedDisplay == EGL_NO_DISPLAY)
                return false;

            if (eglInitialize (sharedDisplay, nullptr, nullptr) != EGL_TRUE)
            {
                logQnxOpenGL ("eglInitialize failed, error=0x" + String::toHexString ((int) eglGetError()));
                sharedDisplay = EGL_NO_DISPLAY;
                return false;
            }

            logClientExtensions();
            logDisplayExtensions (sharedDisplay);

            if (eglBindAPI (EGL_OPENGL_ES_API) != EGL_TRUE)
            {
                logQnxOpenGL ("eglBindAPI(EGL_OPENGL_ES_API) failed, error=0x" + String::toHexString ((int) eglGetError()));
                eglTerminate (sharedDisplay);
                sharedDisplay = EGL_NO_DISPLAY;
                return false;
            }
        }

        display = sharedDisplay;

        const std::vector<EGLint> multisampleAttributes
        {
            EGL_SAMPLE_BUFFERS, useMultisamplingIn ? 1 : 0,
            EGL_SAMPLES,        pixelFormat.multisamplingLevel
        };

        return tryChooseConfig (pixelFormat, multisampleAttributes)
            || tryChooseConfig (pixelFormat, {});
    }

    bool tryInitDisplayWithPlatformScreen()
    {
       #if defined(EGL_PLATFORM_SCREEN_QNX)
        if (nativeScreenDisplay != nullptr)
        {
            logQnxOpenGL ("Trying eglGetPlatformDisplay(EGL_PLATFORM_SCREEN_QNX, nativeScreenDisplay)");
            sharedDisplay = eglGetPlatformDisplay (EGL_PLATFORM_SCREEN_QNX,
                                                   nativeScreenDisplay,
                                                   nullptr);

            if (sharedDisplay != EGL_NO_DISPLAY)
                return true;

            logQnxOpenGL ("eglGetPlatformDisplay(EGL_PLATFORM_SCREEN_QNX, nativeScreenDisplay) failed, error=0x"
                          + String::toHexString ((int) eglGetError()));
        }

        logQnxOpenGL ("Trying eglGetPlatformDisplay(EGL_PLATFORM_SCREEN_QNX, nativeDisplay)");
        sharedDisplay = eglGetPlatformDisplay (EGL_PLATFORM_SCREEN_QNX,
                                               reinterpret_cast<void*> (nativeDisplay),
                                               nullptr);

        if (sharedDisplay != EGL_NO_DISPLAY)
            return true;

        logQnxOpenGL ("eglGetPlatformDisplay(EGL_PLATFORM_SCREEN_QNX, nativeDisplay) failed, error=0x"
                      + String::toHexString ((int) eglGetError()));
       #else
        logQnxOpenGL ("EGL_PLATFORM_SCREEN_QNX not defined in headers");
       #endif

        return false;
    }

    void tryInitDisplayWithLegacyEntryPoint()
    {
        logQnxOpenGL ("Trying legacy eglGetDisplay(nativeDisplay)");
        sharedDisplay = eglGetDisplay (nativeDisplay);

        if (sharedDisplay == EGL_NO_DISPLAY)
            logQnxOpenGL ("eglGetDisplay(nativeDisplay) failed, error=0x" + String::toHexString ((int) eglGetError()));
    }

    static void logClientExtensions()
    {
        if (const auto* extensions = eglQueryString (EGL_NO_DISPLAY, EGL_EXTENSIONS))
            logQnxOpenGL ("EGL client extensions: " + String (extensions));
    }

    static void logDisplayExtensions (EGLDisplay displayToQuery)
    {
        if (const auto* extensions = eglQueryString (displayToQuery, EGL_EXTENSIONS))
            logQnxOpenGL ("EGL display extensions: " + String (extensions));
    }

    void destroySurface()
    {
        if (surface != EGL_NO_SURFACE)
        {
            eglDestroySurface (display, surface);
            surface = EGL_NO_SURFACE;
        }
    }

    void destroyContext()
    {
        if (context != EGL_NO_CONTEXT)
        {
            eglDestroyContext (display, context);
            context = EGL_NO_CONTEXT;
        }
    }

    CriticalSection mutex;
    Component& component;
    OpenGLContext* juceContext = nullptr;
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;
    EGLContext contextToShareWith = EGL_NO_CONTEXT;
    EGLConfig config = nullptr;
    EGLNativeDisplayType nativeDisplay = EGLNativeDisplayType{};
    screen_display_t nativeScreenDisplay = nullptr;
    EGLNativeWindowType nativeWindow = EGLNativeWindowType{};
    OpenGLVersion versionRequired = OpenGLVersion::defaultGLVersion;
    int swapInterval = 0;
    int swapCount = 0;
    bool hasInitialised = false;

    inline static EGLDisplay sharedDisplay = EGL_NO_DISPLAY;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NativeContext)
};

bool OpenGLHelpers::isContextActive()
{
    return eglGetCurrentContext() != EGL_NO_CONTEXT;
}

} // namespace juce
