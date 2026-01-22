#include <jni.h>
#include <android/native_window.h> // requires ndk r5 or newer
#include <android/native_window_jni.h> // requires ndk r5 or newer
#include <AndroidExtensions/StdoutLogger.h>
#include <AndroidExtensions/Globals.h>
#include <bgfx/bgfx.h>
#include <GLES3/gl3.h>

#include <stdlib.h>
#include <cstdio>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <optional>
#include <string>
#include <sys/system_properties.h>
#include <unistd.h>

#include <AndroidExtensions/Globals.h>
#include <Babylon/AppRuntime.h>
#include <Babylon/Graphics/Device.h>
#include <Babylon/ScriptLoader.h>
#include <Babylon/Plugins/NativeEncoding.h>
#include <Babylon/Plugins/NativeEngine.h>
#include <Babylon/Plugins/NativeInput.h>
#include <Babylon/Plugins/NativeXr.h>
#include <Babylon/Plugins/NativeCamera.h>
#include <Babylon/Plugins/NativeOptimizations.h>
#include <Babylon/Polyfills/Blob.h>
#include <Babylon/Polyfills/Console.h>
#include <Babylon/Polyfills/Window.h>
#include <Babylon/Polyfills/XMLHttpRequest.h>
#include <Babylon/Polyfills/Canvas.h>
#include <Babylon/DebugTrace.h>

extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM* javaVM, void*)
{
    // Initialize AndroidExtensions globals early so calls like Wrapper.setCurrentActivity can
    // safely attach to the VM before the first surface is created (Android XR launcher flow).
    android::global::Initialize(javaVM, nullptr);
    return JNI_VERSION_1_6;
}

namespace
{
    constexpr const char* RENDERER_LOG_PREFIX = "Renderer selection:";
    constexpr const size_t MAX_EXTENSION_LOG = 1024;

    std::optional<Babylon::Graphics::Device> device{};
    std::optional<Babylon::Graphics::DeviceUpdate> deviceUpdate{};
    std::optional<Babylon::AppRuntime> runtime{};
    std::optional<Babylon::Plugins::NativeXr> nativeXr{};
    Babylon::Plugins::NativeInput* nativeInput{};
    std::optional<Babylon::Polyfills::Canvas> nativeCanvas{};
    std::optional<Babylon::ScriptLoader> scriptLoader{};
    bool isXrActive{};
    ANativeWindow* xrWindow{};
    bool isXrOnlyBuild{};

    Babylon::Graphics::RendererMode GetRequestedRendererMode();
    void LogRendererDetails();

    void EnsureEngineInitialized(JNIEnv* env, jobject context, ANativeWindow* window)
    {
        if (runtime || window == nullptr)
        {
            return;
        }

        if (context == nullptr)
        {
            printf("EnsureEngineInitialized: missing Context; deferring engine creation.\n");
            return;
        }

        if (!android::StdoutLogger::IsStarted())
        {
            android::StdoutLogger::Start();

            // Android emulator GPU stacks can be extremely noisy to stderr; StdoutLogger maps
            // stderr to ANDROID_LOG_ERROR, so merge stderr into stdout on emulators to avoid
            // spurious ERROR logs.
            char value[PROP_VALUE_MAX]{};
            if (__system_property_get("ro.kernel.qemu", value) > 0 && strcmp(value, "1") == 0)
            {
                dup2(STDOUT_FILENO, STDERR_FILENO);
            }
        }

        JavaVM* javaVM{};
        if (env->GetJavaVM(&javaVM) != JNI_OK)
        {
            throw std::runtime_error("Failed to get Java VM");
        }

        android::global::Initialize(javaVM, context);

        Babylon::DebugTrace::EnableDebugTrace(true);
        Babylon::DebugTrace::SetTraceOutput([](const char* trace) { printf("%s\n", trace); fflush(stdout); });

        int32_t width = ANativeWindow_getWidth(window);
        int32_t height = ANativeWindow_getHeight(window);

        Babylon::Graphics::Configuration graphicsConfig{};
        graphicsConfig.Window = window;
        graphicsConfig.Width = static_cast<size_t>(width);
        graphicsConfig.Height = static_cast<size_t>(height);
        graphicsConfig.RequestedRendererMode = GetRequestedRendererMode();
        switch (graphicsConfig.RequestedRendererMode)
        {
            case Babylon::Graphics::RendererMode::ForceOpenGLES:
                printf("%s OpenGLES (forced)\n", RENDERER_LOG_PREFIX);
                break;
            case Babylon::Graphics::RendererMode::ForceVulkan:
                printf("%s Vulkan (forced)\n", RENDERER_LOG_PREFIX);
                break;
            case Babylon::Graphics::RendererMode::Automatic:
            default:
                printf("%s default\n", RENDERER_LOG_PREFIX);
                break;
        }
        device.emplace(graphicsConfig);
        deviceUpdate.emplace(device->GetUpdate("update"));
        device->StartRenderingCurrentFrame();
        deviceUpdate->Start();
        LogRendererDetails();

        runtime.emplace();

        runtime->Dispatch([](Napi::Env env)
        {
            device->AddToJavaScript(env);

            Babylon::Polyfills::Blob::Initialize(env);

            Babylon::Polyfills::Console::Initialize(env, [](const char* message, Babylon::Polyfills::Console::LogLevel level)
            {
                switch (level)
                {
                case Babylon::Polyfills::Console::LogLevel::Log:
                    printf("%s\n", message);
                    fflush(stdout);
                    break;
                case Babylon::Polyfills::Console::LogLevel::Warn:
                    printf("%s\n", message);
                    fflush(stdout);
                    break;
                case Babylon::Polyfills::Console::LogLevel::Error:
                    printf("%s\n", message);
                    fflush(stdout);
                    break;
                }
            });

            Babylon::Plugins::NativeEncoding::Initialize(env);
            Babylon::Plugins::NativeEngine::Initialize(env);
            Babylon::Plugins::NativeOptimizations::Initialize(env);

            nativeXr.emplace(Babylon::Plugins::NativeXr::Initialize(env));
            nativeXr->SetSessionStateChangedCallback([](bool isXrActive){ ::isXrActive = isXrActive; });
            if (xrWindow != nullptr)
            {
                nativeXr->UpdateWindow(xrWindow);
            }

            nativeInput = &Babylon::Plugins::NativeInput::CreateForJavaScript(env);

            Babylon::Plugins::NativeCamera::Initialize(env);
            Babylon::Polyfills::Window::Initialize(env);

            Babylon::Polyfills::XMLHttpRequest::Initialize(env);
            nativeCanvas.emplace(Babylon::Polyfills::Canvas::Initialize(env));
        });

        scriptLoader.emplace(*runtime);
        scriptLoader->LoadScript("app:///Scripts/ammo.js");
        scriptLoader->LoadScript("app:///Scripts/recast.js");
        scriptLoader->LoadScript("app:///Scripts/babylon.max.js");
        scriptLoader->LoadScript("app:///Scripts/babylonjs.loaders.js");
        scriptLoader->LoadScript("app:///Scripts/babylonjs.materials.js");
        scriptLoader->LoadScript("app:///Scripts/babylon.gui.js");
        scriptLoader->LoadScript("app:///Scripts/babylonjs.serializers.js");
    }

    Babylon::Graphics::RendererMode GetRequestedRendererMode()
    {
        char value[PROP_VALUE_MAX]{};

        // Allow explicit override when debugging GPU issues.
        if (__system_property_get("debug.babylonnative.force_vulkan", value) > 0)
        {
            if (strcmp(value, "1") == 0 || strcasecmp(value, "true") == 0)
            {
                printf("debug.babylonnative.force_vulkan=%s; forcing Vulkan backend.\n", value);
                return Babylon::Graphics::RendererMode::ForceVulkan;
            }
        }

        // Allow explicit override when debugging GPU issues.
        if (__system_property_get("debug.babylonnative.force_gles", value) > 0)
        {
            if (strcmp(value, "1") == 0 || strcasecmp(value, "true") == 0)
            {
                printf("debug.babylonnative.force_gles=%s; forcing OpenGL backend.\n", value);
                return Babylon::Graphics::RendererMode::ForceOpenGLES;
            }
        }

        if (__system_property_get("ro.kernel.qemu", value) > 0 && strcmp(value, "1") == 0)
        {
            printf("Detected emulator (ro.kernel.qemu=1); forcing OpenGL backend for stability.\n");
            return Babylon::Graphics::RendererMode::ForceOpenGLES;
        }

        if (__system_property_get("ro.hardware.vulkan", value) > 0)
        {
            if (strstr(value, "ranchu") || strstr(value, "swiftshader"))
            {
                printf("Detected software Vulkan driver; forcing OpenGL backend.\n");
                return Babylon::Graphics::RendererMode::ForceOpenGLES;
            }
        }
        if (__system_property_get("ro.hardware.egl", value) > 0)
        {
            if (strstr(value, "emulation") || strstr(value, "swiftshader") || strstr(value, "angle"))
            {
                printf("Detected emulator EGL stack; forcing OpenGL backend.\n");
                return Babylon::Graphics::RendererMode::ForceOpenGLES;
            }
        }
        return Babylon::Graphics::RendererMode::Automatic;
    }

    void LogRendererDetails()
    {
        const bgfx::Caps* caps = bgfx::getCaps();
        if (!caps)
        {
            printf("Renderer caps unavailable (bgfx not initialized yet)\n");
            return;
        }

        printf("Renderer caps: type=%s vendorId=0x%x deviceId=0x%x maxTextureSize=%u maxViews=%u\n",
            bgfx::getRendererName(caps->rendererType), caps->vendorId, caps->deviceId, caps->limits.maxTextureSize, caps->limits.maxViews);

        struct CapsFlag { uint64_t flag; const char* name; };
        static const CapsFlag kCapsFlags[] = {
            { BGFX_CAPS_ALPHA_TO_COVERAGE, "ALPHA_TO_COVERAGE" },
            { BGFX_CAPS_BLEND_INDEPENDENT, "BLEND_INDEPENDENT" },
            { BGFX_CAPS_COMPUTE, "COMPUTE" },
            { BGFX_CAPS_CONSERVATIVE_RASTER, "CONSERVATIVE_RASTER" },
            { BGFX_CAPS_DRAW_INDIRECT, "DRAW_INDIRECT" },
            { BGFX_CAPS_FRAGMENT_DEPTH, "FRAGMENT_DEPTH" },
            { BGFX_CAPS_FRAGMENT_ORDERING, "FRAGMENT_ORDERING" },
            { BGFX_CAPS_GRAPHICS_DEBUGGER, "GRAPHICS_DEBUGGER" },
            { BGFX_CAPS_HDR10, "HDR10" },
            { BGFX_CAPS_HIDPI, "HIDPI" },
            { BGFX_CAPS_IMAGE_RW, "IMAGE_RW" },
            { BGFX_CAPS_INDEX32, "INDEX32" },
            { BGFX_CAPS_INSTANCING, "INSTANCING" },
            { BGFX_CAPS_OCCLUSION_QUERY, "OCCLUSION_QUERY" },
            { BGFX_CAPS_RENDERER_MULTITHREADED, "RENDERER_MULTITHREADED" },
            { BGFX_CAPS_SWAP_CHAIN, "SWAP_CHAIN" },
            { BGFX_CAPS_TEXTURE_2D_ARRAY, "TEXTURE_2D_ARRAY" },
            { BGFX_CAPS_TEXTURE_3D, "TEXTURE_3D" },
            { BGFX_CAPS_TEXTURE_BLIT, "TEXTURE_BLIT" },
            { BGFX_CAPS_TRANSPARENT_BACKBUFFER, "TRANSPARENT_BACKBUFFER" },
            { BGFX_CAPS_TEXTURE_COMPARE_ALL, "TEXTURE_COMPARE_ALL" },
            { BGFX_CAPS_TEXTURE_COMPARE_LEQUAL, "TEXTURE_COMPARE_LEQUAL" },
            { BGFX_CAPS_TEXTURE_CUBE_ARRAY, "TEXTURE_CUBE_ARRAY" },
            { BGFX_CAPS_TEXTURE_DIRECT_ACCESS, "TEXTURE_DIRECT_ACCESS" },
            { BGFX_CAPS_TEXTURE_READ_BACK, "TEXTURE_READ_BACK" },
            { BGFX_CAPS_VERTEX_ATTRIB_HALF, "VERTEX_ATTRIB_HALF" },
            { BGFX_CAPS_VERTEX_ATTRIB_UINT10, "VERTEX_ATTRIB_UINT10" },
            { BGFX_CAPS_VERTEX_ATTRIB_INT8, "VERTEX_ATTRIB_INT8" },
            { BGFX_CAPS_VERTEX_ATTRIB_UINT16, "VERTEX_ATTRIB_UINT16" },
            { BGFX_CAPS_VERTEX_ID, "VERTEX_ID" },
            { BGFX_CAPS_PRIMITIVE_ID, "PRIMITIVE_ID" },
            { BGFX_CAPS_VIEWPORT_LAYER_ARRAY, "VIEWPORT_LAYER_ARRAY" },
        };

        std::string supportedCaps;
        for (const auto& entry : kCapsFlags)
        {
            if ((caps->supported & entry.flag) != 0)
            {
                if (!supportedCaps.empty())
                {
                    supportedCaps += ", ";
                }
                supportedCaps += entry.name;
            }
        }
        if (supportedCaps.empty())
        {
            supportedCaps = "none";
        }
        if (supportedCaps.size() > MAX_EXTENSION_LOG)
        {
            supportedCaps = supportedCaps.substr(0, MAX_EXTENSION_LOG) + "...(truncated)";
        }
        printf("Renderer supported caps: %s\n", supportedCaps.c_str());

#if BGFX_CONFIG_RENDERER_OPENGLES
        if (caps->rendererType == bgfx::RendererType::OpenGLES)
        {
            const char* vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
            const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
            const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
            const char* extensions = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));

            if (vendor || renderer || version)
            {
                printf("OpenGL ES strings: vendor=%s renderer=%s version=%s\n",
                    vendor ? vendor : "?", renderer ? renderer : "?", version ? version : "?");
            }

            if (extensions)
            {
                std::string extStr{extensions};
                if (extStr.size() > MAX_EXTENSION_LOG)
                {
                    extStr = extStr.substr(0, MAX_EXTENSION_LOG) + "...(truncated)";
                }
                printf("OpenGL ES extensions: %s\n", extStr.c_str());
            }
        }
#endif
    }
}

extern "C"
{
    JNIEXPORT void JNICALL
    Java_com_library_babylonnative_Wrapper_initEngine(JNIEnv* env, jclass clazz)
    {
    }

    JNIEXPORT void JNICALL
    Java_com_library_babylonnative_Wrapper_finishEngine(JNIEnv* env, jclass clazz)
    {
        if (device)
        {
            deviceUpdate->Finish();
            device->FinishRenderingCurrentFrame();
        }

        isXrActive = false;

        scriptLoader.reset();

        nativeInput = {};
        nativeXr.reset();
        scriptLoader.reset();
        runtime.reset();
        device.reset();
        if (xrWindow != nullptr)
        {
            ANativeWindow_release(xrWindow);
            xrWindow = nullptr;
        }
    }

    JNIEXPORT void JNICALL
    Java_com_library_babylonnative_Wrapper_surfaceCreated(JNIEnv* env, jclass clazz, jobject surface, jobject context)
    {
        ANativeWindow* window{};
        if (isXrOnlyBuild && xrWindow != nullptr)
        {
            window = xrWindow;
        }
        else
        {
            window = ANativeWindow_fromSurface(env, surface);
        }

        EnsureEngineInitialized(env, context, window);
    }

    JNIEXPORT void JNICALL
    Java_com_library_babylonnative_Wrapper_surfaceChanged(JNIEnv* env, jclass clazz, jint width, jint height, jobject surface)
    {
        if (runtime)
        {
            if (isXrOnlyBuild && xrWindow != nullptr)
            {
                return;
            }

            ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
            runtime->Dispatch([window, width = static_cast<size_t>(width), height = static_cast<size_t>(height)](auto) {
                device->UpdateWindow(window);
                device->UpdateSize(width, height);
            });
        }
    }

    JNIEXPORT void JNICALL
    Java_com_library_babylonnative_Wrapper_setCurrentActivity(JNIEnv* env, jclass clazz, jobject currentActivity)
    {
        android::global::SetCurrentActivity(currentActivity);
    }

    JNIEXPORT void JNICALL
    Java_com_library_babylonnative_Wrapper_activityOnPause(JNIEnv* env, jclass clazz)
    {
        android::global::Pause();
        if (runtime)
        {
            runtime->Suspend();
        }
    }

    JNIEXPORT void JNICALL
    Java_com_library_babylonnative_Wrapper_activityOnResume(JNIEnv* env, jclass clazz)
    {
        if (runtime)
        {
            runtime->Resume();
        }
        android::global::Resume();
    }

    JNIEXPORT void JNICALL
    Java_com_library_babylonnative_Wrapper_activityOnRequestPermissionsResult(JNIEnv* env, jclass clazz, jint requestCode, jobjectArray permissions, jintArray grantResults)
    {
        std::vector<std::string> nativePermissions{};
        for (int i = 0; i < env->GetArrayLength(permissions); i++)
        {
            jstring permission = (jstring)env->GetObjectArrayElement(permissions, i);
            const char* utfString{env->GetStringUTFChars(permission, nullptr)};
            nativePermissions.push_back(utfString);
            env->ReleaseStringUTFChars(permission, utfString);
        }

        auto grantResultElements{env->GetIntArrayElements(grantResults, nullptr)};
        auto grantResultElementCount = env->GetArrayLength(grantResults);
        std::vector<int32_t> nativeGrantResults{grantResultElements, grantResultElements + grantResultElementCount};
        env->ReleaseIntArrayElements(grantResults, grantResultElements, 0);

        android::global::RequestPermissionsResult(requestCode, nativePermissions, nativeGrantResults);
    }

    JNIEXPORT void JNICALL
    Java_com_library_babylonnative_Wrapper_loadScript(JNIEnv* env, jclass clazz, jstring path)
    {
        if (scriptLoader)
        {
            scriptLoader->LoadScript(env->GetStringUTFChars(path, nullptr));
        }
    }

    JNIEXPORT void JNICALL
    Java_com_library_babylonnative_Wrapper_eval(JNIEnv* env, jclass clazz, jstring source, jstring sourceURL)
    {
        if (runtime)
        {
            std::string url = env->GetStringUTFChars(sourceURL, nullptr);
            std::string src = env->GetStringUTFChars(source, nullptr);
            scriptLoader->Eval(std::move(src), std::move(url));
        }
    }

    JNIEXPORT void JNICALL
    Java_com_library_babylonnative_Wrapper_setTouchInfo(JNIEnv* env, jclass clazz, jint pointerId, jfloat x, jfloat y, jboolean buttonAction, jint buttonValue)
    {
        if (nativeInput != nullptr)
        {
            if (buttonAction)
            {
                if (buttonValue == 1)
                    nativeInput->TouchDown(pointerId, x, y);
                else
                    nativeInput->TouchUp(pointerId, x, y);
            }
            else {
                nativeInput->TouchMove(pointerId, x, y);
            }
        }
    }

    JNIEXPORT void JNICALL
    Java_com_library_babylonnative_Wrapper_renderFrame(JNIEnv* env, jclass clazz)
    {
        if (device)
        {
            deviceUpdate->Finish();
            device->FinishRenderingCurrentFrame();
            device->StartRenderingCurrentFrame();
            deviceUpdate->Start();
        }
    }

    JNIEXPORT void JNICALL
    Java_com_library_babylonnative_Wrapper_xrSurfaceChanged(JNIEnv* env, jclass clazz, jobject surface)
    {
        ANativeWindow* window{};
        if (surface)
        {
            window = ANativeWindow_fromSurface(env, surface);
        }

        if (xrWindow != nullptr)
        {
            ANativeWindow_release(xrWindow);
        }
        xrWindow = window;

        if (isXrOnlyBuild && !runtime && window != nullptr)
        {
            EnsureEngineInitialized(env, android::global::GetCurrentActivity(), window);
        }

        if (nativeXr)
        {
            nativeXr->UpdateWindow(window);
        }

        if (isXrOnlyBuild && runtime && device && window != nullptr)
        {
            int32_t width = ANativeWindow_getWidth(window);
            int32_t height = ANativeWindow_getHeight(window);
            runtime->Dispatch([window, width = static_cast<size_t>(width), height = static_cast<size_t>(height)](auto) {
                device->UpdateWindow(window);
                device->UpdateSize(width, height);
            });
        }
    }

    JNIEXPORT jboolean JNICALL
    Java_com_library_babylonnative_Wrapper_isXRActive(JNIEnv* env, jclass clazz)
    {
        return isXrActive;
    }

    JNIEXPORT void JNICALL
    Java_com_library_babylonnative_Wrapper_setXrOnly(JNIEnv* env, jclass clazz, jboolean xrOnly)
    {
        isXrOnlyBuild = xrOnly == JNI_TRUE;
        if (isXrOnlyBuild && !runtime && xrWindow != nullptr)
        {
            EnsureEngineInitialized(env, android::global::GetCurrentActivity(), xrWindow);
        }
        if (isXrOnlyBuild && runtime && device && xrWindow != nullptr)
        {
            int32_t width = ANativeWindow_getWidth(xrWindow);
            int32_t height = ANativeWindow_getHeight(xrWindow);
            runtime->Dispatch([window = xrWindow, width = static_cast<size_t>(width), height = static_cast<size_t>(height)](auto) {
                device->UpdateWindow(window);
                device->UpdateSize(width, height);
            });
        }
    }

    JNIEXPORT jboolean JNICALL
    Java_com_library_babylonnative_Wrapper_isEngineReady(JNIEnv* env, jclass clazz)
    {
        return runtime && device && scriptLoader ? JNI_TRUE : JNI_FALSE;
    }
}
