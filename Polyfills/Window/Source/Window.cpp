#include "Window.h"

#include <Babylon/Graphics/DeviceContext.h>
#include <Babylon/Polyfills/Scheduling.h>

#include <basen.hpp>

namespace Babylon::Polyfills::Internal
{
    namespace
    {
        constexpr auto JS_CLASS_NAME = "Window";
        constexpr auto JS_A_TO_B_NAME = "atob";
        constexpr auto JS_ADD_EVENT_LISTENER_NAME = "addEventListener";
        constexpr auto JS_REMOVE_EVENT_LISTENER_NAME = "removeEventListener";
        constexpr auto JS_DEVICE_PIXEL_RATIO_NAME = "devicePixelRatio";
        constexpr auto JS_DOCUMENT_NAME = "document";
        constexpr auto JS_CREATE_EVENT_NAME = "createEvent";
        constexpr auto JS_LOCATION_NAME = "location";
        constexpr auto JS_HREF_NAME = "href";
        constexpr auto JS_INIT_EVENT_NAME = "initEvent";
        constexpr auto JS_TYPE_NAME = "type";
        constexpr auto JS_BUBBLES_NAME = "bubbles";
        constexpr auto JS_CANCELABLE_NAME = "cancelable";
    }

    void Window::Initialize(Napi::Env env)
    {
        Napi::HandleScope scope{env};

        Napi::Function constructor = DefineClass(
            env,
            JS_CLASS_NAME,
            {});

        auto global = env.Global();
        auto jsNative = JsRuntime::NativeObject::GetFromJavaScript(env);
        auto jsWindow = constructor.New({});

        jsNative.Set(JS_WINDOW_NAME, jsWindow);

        Napi::Object jsLocation{};
        auto existingLocation = global.Get(JS_LOCATION_NAME);
        if (existingLocation.IsObject())
        {
            jsLocation = existingLocation.As<Napi::Object>();
        }
        else
        {
            jsLocation = Napi::Object::New(env);
        }

        if (jsLocation.Get(JS_HREF_NAME).IsUndefined())
        {
            jsLocation.Set(JS_HREF_NAME, Napi::String::New(env, "app:///"));
        }

        global.Set(JS_LOCATION_NAME, jsLocation);
        jsWindow.Set(JS_LOCATION_NAME, jsLocation);

        auto existingDocument = global.Get(JS_DOCUMENT_NAME);
        if (existingDocument.IsObject())
        {
            auto jsDocument = existingDocument.As<Napi::Object>();

            if (jsDocument.Get(JS_CREATE_EVENT_NAME).IsUndefined())
            {
                jsDocument.Set(JS_CREATE_EVENT_NAME, Napi::Function::New(env, &Window::CreateEvent, JS_CREATE_EVENT_NAME));
            }

            jsWindow.Set(JS_DOCUMENT_NAME, jsDocument);
        }

        Scheduling::Initialize(env);

        if (global.Get(JS_A_TO_B_NAME).IsUndefined())
        {
            global.Set(JS_A_TO_B_NAME, Napi::Function::New(env, &Window::DecodeBase64, JS_A_TO_B_NAME));
        }

        if (global.Get(JS_ADD_EVENT_LISTENER_NAME).IsUndefined())
        {
            global.Set(JS_ADD_EVENT_LISTENER_NAME, Napi::Function::New(env, &Window::AddEventListener, JS_ADD_EVENT_LISTENER_NAME));
        }

        if (global.Get(JS_REMOVE_EVENT_LISTENER_NAME).IsUndefined())
        {
            global.Set(JS_REMOVE_EVENT_LISTENER_NAME, Napi::Function::New(env, &Window::RemoveEventListener, JS_REMOVE_EVENT_LISTENER_NAME));
        }

        if (global.Get(JS_DEVICE_PIXEL_RATIO_NAME).IsUndefined())
        {
            // Create an accessor to add to the window object to define window.devicePixelRatio
            Napi::Object descriptor{Napi::Object::New(env)};
            descriptor.Set("enumerable", Napi::Value::From(env, true));
            descriptor.Set("get", Napi::Function::New(env, &Window::GetDevicePixelRatio, JS_DEVICE_PIXEL_RATIO_NAME, &jsWindow));
            Napi::Object object{global.Get("Object").As<Napi::Object>()};
            Napi::Function defineProperty{object.Get("defineProperty").As<Napi::Function>()};
            defineProperty.Call(object, {global, Napi::String::New(env, JS_DEVICE_PIXEL_RATIO_NAME), descriptor});
        }
    }

    Window& Window::GetFromJavaScript(Napi::Env env)
    {
        return *Window::Unwrap(JsRuntime::NativeObject::GetFromJavaScript(env).Get(JS_WINDOW_NAME).As<Napi::Object>());
    }

    Window::Window(const Napi::CallbackInfo& info)
        : Napi::ObjectWrap<Window>{info}
        , m_runtime{JsRuntime::GetFromJavaScript(info.Env())}
    {
    }

    Napi::Value Window::DecodeBase64(const Napi::CallbackInfo& info)
    {
        std::string encodedData = info[0].As<Napi::String>().Utf8Value();
        std::u16string decodedData;
        bn::decode_b64(encodedData.begin(), encodedData.end(), std::back_inserter(decodedData));
        return Napi::Value::From(info.Env(), decodedData);
    }

    Napi::Value Window::CreateEvent(const Napi::CallbackInfo& info)
    {
        auto env = info.Env();
        auto event = Napi::Object::New(env);

        event.Set(JS_TYPE_NAME, Napi::String::New(env, ""));
        event.Set(JS_BUBBLES_NAME, Napi::Boolean::New(env, false));
        event.Set(JS_CANCELABLE_NAME, Napi::Boolean::New(env, false));
        event.Set(JS_INIT_EVENT_NAME, Napi::Function::New(env, [](const Napi::CallbackInfo& info) {
            auto event = info.This().As<Napi::Object>();
            event.Set(JS_TYPE_NAME, info[0].IsUndefined() ? Napi::String::New(info.Env(), "") : info[0].ToString());
            event.Set(JS_BUBBLES_NAME, Napi::Boolean::New(info.Env(), info[1].ToBoolean()));
            event.Set(JS_CANCELABLE_NAME, Napi::Boolean::New(info.Env(), info[2].ToBoolean()));
        }, JS_INIT_EVENT_NAME));

        return event;
    }

    void Window::AddEventListener(const Napi::CallbackInfo& /*info*/)
    {
        // TODO: handle events
    }

    void Window::RemoveEventListener(const Napi::CallbackInfo& /*info*/)
    {
        // TODO: handle events
    }

    Napi::Value Window::GetDevicePixelRatio(const Napi::CallbackInfo& info)
    {
        auto env{info.Env()};
        return Napi::Value::From(env, Graphics::DeviceContext::GetFromJavaScript(env).GetDevicePixelRatio());
    }
}

namespace Babylon::Polyfills::Window
{
    void BABYLON_API Initialize(Napi::Env env)
    {
        Internal::Window::Initialize(env);
    }
}
