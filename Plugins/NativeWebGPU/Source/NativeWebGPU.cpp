#include <Babylon/Plugins/NativeWebGPU.h>
#include <Babylon/JsRuntime.h>
#include <Babylon/Graphics/WgpuInterop.h>

#include <napi/napi.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <functional>
#ifdef BABYLON_NATIVE_WEBGPU_TEST_HOOKS
#include <future>
#endif
#include <initializer_list>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <utility>

namespace Babylon::Plugins::NativeWebGPU
{
    namespace
    {
        std::atomic_bool g_sawWebGpuDrawCall{false};
        std::atomic_uint64_t g_renderPipelineCreateCount{0};
        std::atomic_uint64_t g_commandEncoderCreateCount{0};
        std::atomic_uint64_t g_renderPassBeginCount{0};
        std::atomic_uint64_t g_queueSubmitCount{0};
        std::atomic_uint64_t g_drawCallCount{0};
        std::atomic_uint64_t g_textureCreateCount{0};
        std::atomic_uint64_t g_textureViewCreateCount{0};
        std::atomic_uint64_t g_bindGroupCreateCount{0};
        std::atomic_uint64_t g_bufferCreateCount{0};
        std::atomic_uint64_t g_bufferRequestedBytes{0};
        std::atomic_uint64_t g_nextCanvasContextId{1};
        constexpr auto kBackendMode = "wgpu-native-command-recording";
        constexpr auto kWebGpuDeveloperFeaturesMode = "webgpu-developer-features";
        constexpr auto kUnsafeWebGpuMode = "unsafe-webgpu";

        constexpr auto JS_NAVIGATOR_NAME = "navigator";
        constexpr auto JS_GPU_NAME = "gpu";
        constexpr auto JS_NATIVE_HANDLE_NAME = "__babylonNativeWebGPUHandle";
        constexpr auto JS_NATIVE_HANDLE_ID_NAME = "__babylonNativeWebGPUHandleId";
        constexpr auto JS_NATIVE_HANDLE_KIND_NAME = "__babylonNativeWebGPUHandleKind";
        constexpr auto JS_NATIVE_JSON_REPLACER_NAME = "__nativeWebGpuJsonReplacer";

        enum class NativeResourceKind : uint32_t
        {
            Buffer = 1,
            Texture = 2,
            TextureView = 3,
            Sampler = 4,
            ShaderModule = 5,
            BindGroupLayout = 6,
            PipelineLayout = 7,
            BindGroup = 8,
            RenderPipeline = 9,
            CommandEncoder = 10,
            RenderPass = 11,
            CommandBuffer = 12,
            ComputePipeline = 13,
            ComputePass = 14,
        };

        struct NativeHandleState final
        {
            NativeResourceKind Kind{};
            uint64_t Id{};
            size_t Size{};
            bool Mapped{};
        };

        struct ByteSpan final
        {
            const uint8_t* Data{};
            size_t Size{};
        };

        struct TextureDescriptorData final
        {
            std::string Label{};
            std::string Format{"bgra8unorm"};
            std::string Dimension{"2d"};
            uint32_t Width{1};
            uint32_t Height{1};
            uint32_t DepthOrArrayLayers{1};
            uint32_t MipLevelCount{1};
            uint32_t SampleCount{1};
            uint32_t Usage{16};
        };

        struct CanvasContextState final
        {
            uint64_t CanvasId{};
            std::string Format{"bgra8unorm"};
            uint32_t Width{1280};
            uint32_t Height{720};
            uint32_t Usage{16};
            bool Configured{};
            bool Destroyed{};
            Napi::ObjectReference CachedTexture{};
        };

        struct RenderBundleState final
        {
            uint64_t DrawCallCount{};
            std::vector<std::function<void(uint64_t)>> Commands{};
        };

        struct CommandEncoderState final
        {
            Napi::ObjectReference RenderPass{};
            Napi::ObjectReference ComputePass{};
            Napi::ObjectReference CommandBuffer{};
        };

        std::string GetLastWgpuError()
        {
            std::array<char, 2048> buffer{};
            if (babylon_wgpu_get_last_error(buffer.data(), buffer.size()) && buffer[0] != '\0')
            {
                return buffer.data();
            }

            return {};
        }

        bool IsWebGpuTraceEnabled()
        {
            static const bool enabled = std::getenv("BABYLON_NATIVE_WEBGPU_TRACE") != nullptr;
            return enabled;
        }

        std::string NativeWebGpuErrorMessage(const char* message)
        {
            const auto detail = GetLastWgpuError();
            if (!detail.empty())
            {
                return std::string{message} + ": " + detail;
            }

            return message;
        }

        uint32_t ToUint32(const Napi::Value& value, uint32_t fallback)
        {
            if (!value.IsNumber())
            {
                return fallback;
            }

            const auto raw = value.As<Napi::Number>().Int64Value();
            if (raw <= 0)
            {
                return fallback;
            }

            return static_cast<uint32_t>(std::min<int64_t>(raw, std::numeric_limits<uint32_t>::max()));
        }

        double ToFiniteNumber(const Napi::Value& value, double fallback)
        {
            if (!value.IsNumber())
            {
                return fallback;
            }

            const auto raw = value.As<Napi::Number>().DoubleValue();
            return std::isfinite(raw) ? raw : fallback;
        }

        uint32_t ToNonNegativeUint32(double value)
        {
            if (value <= 0)
            {
                return 0;
            }

            return static_cast<uint32_t>(std::min<double>(std::floor(value), std::numeric_limits<uint32_t>::max()));
        }

        struct ScissorRect final
        {
            uint32_t X{};
            uint32_t Y{};
            uint32_t Width{};
            uint32_t Height{};
        };

        ScissorRect ReadScissorRect(const Napi::CallbackInfo& info)
        {
            auto x = info.Length() > 0 ? ToFiniteNumber(info[0], 0) : 0;
            auto y = info.Length() > 1 ? ToFiniteNumber(info[1], 0) : 0;
            auto width = info.Length() > 2 ? ToFiniteNumber(info[2], 1) : 1;
            auto height = info.Length() > 3 ? ToFiniteNumber(info[3], 1) : 1;

            if (x < 0)
            {
                width += x;
                x = 0;
            }
            if (y < 0)
            {
                height += y;
                y = 0;
            }

            return {
                ToNonNegativeUint32(x),
                ToNonNegativeUint32(y),
                ToNonNegativeUint32(width),
                ToNonNegativeUint32(height),
            };
        }

        std::vector<uint32_t> GetDynamicOffsets(const Napi::CallbackInfo& info, size_t index = 2)
        {
            std::vector<uint32_t> dynamicOffsets;
            if (info.Length() <= index || !(info[index].IsArray() || info[index].IsTypedArray()))
            {
                return dynamicOffsets;
            }

            const auto offsetObject = info[index].As<Napi::Object>();
            const uint32_t sourceLength = info[index].IsArray()
                ? info[index].As<Napi::Array>().Length()
                : static_cast<uint32_t>(info[index].As<Napi::TypedArray>().ElementLength());
            const uint32_t start = info.Length() > index + 1 ? ToUint32(info[index + 1], 0) : 0;
            const uint32_t available = start < sourceLength ? sourceLength - start : 0;
            const uint32_t count = info.Length() > index + 2 ? std::min(ToUint32(info[index + 2], available), available) : available;
            dynamicOffsets.reserve(count);
            for (uint32_t i = 0; i < count; ++i)
            {
                dynamicOffsets.push_back(ToUint32(offsetObject.Get(start + i), 0));
            }
            return dynamicOffsets;
        }

        uint32_t GetUint32(const Napi::Object& object, const char* key, uint32_t fallback)
        {
            return object.Has(key) ? ToUint32(object.Get(key), fallback) : fallback;
        }

        bool GetBool(const Napi::Object& object, const char* key, bool fallback)
        {
            return object.Has(key) && object.Get(key).IsBoolean()
                ? object.Get(key).As<Napi::Boolean>().Value()
                : fallback;
        }

        uint32_t GetOriginCoordinate(const Napi::Value& origin, const char* key, uint32_t index)
        {
            if (origin.IsArray())
            {
                auto array = origin.As<Napi::Array>();
                return array.Length() > index ? ToUint32(array.Get(index), 0) : 0;
            }
            if (origin.IsObject())
            {
                return GetUint32(origin.As<Napi::Object>(), key, 0);
            }
            return 0;
        }

        std::string GetString(const Napi::Object& object, const char* key, std::string_view fallback = "")
        {
            if (!object.Has(key))
            {
                return std::string{fallback};
            }

            const auto value = object.Get(key);
            if (!value.IsString())
            {
                return std::string{fallback};
            }

            return value.As<Napi::String>().Utf8Value();
        }

        NativeHandleState* GetNativeHandleState(const Napi::Value& value)
        {
            if (!value.IsObject())
            {
                return nullptr;
            }

            auto object = value.As<Napi::Object>();
            if (!object.Has(JS_NATIVE_HANDLE_NAME))
            {
                return nullptr;
            }

            auto handleValue = object.Get(JS_NATIVE_HANDLE_NAME);
            if (!handleValue.IsExternal())
            {
                return nullptr;
            }

            return handleValue.As<Napi::External<NativeHandleState>>().Data();
        }

        uint64_t GetNativeHandleId(const Napi::Value& value, NativeResourceKind expectedKind)
        {
            auto* state = GetNativeHandleState(value);
            if (state == nullptr || state->Id == 0 || state->Kind != expectedKind)
            {
                return 0;
            }
            return state->Id;
        }

        void DestroyNativeHandleState(NativeHandleState* state)
        {
            if (state != nullptr && state->Id != 0)
            {
                babylon_wgpu_native_destroy_resource(static_cast<uint32_t>(state->Kind), state->Id);
                state->Id = 0;
            }
        }

        void FinalizeNativeHandleState(Napi::Env, NativeHandleState* state)
        {
            DestroyNativeHandleState(state);
            delete state;
        }

        Napi::Object AttachNativeHandle(Napi::Object object, NativeResourceKind kind, uint64_t id, size_t size = 0)
        {
            auto* state = new NativeHandleState{kind, id, size, false};
            object.Set(
                JS_NATIVE_HANDLE_NAME,
                Napi::External<NativeHandleState>::New(object.Env(), state, &FinalizeNativeHandleState));
            object.Set(JS_NATIVE_HANDLE_ID_NAME, Napi::Number::From(object.Env(), static_cast<double>(id)));
            object.Set(JS_NATIVE_HANDLE_KIND_NAME, Napi::Number::From(object.Env(), static_cast<uint32_t>(kind)));
            return object;
        }

        void AppendJsonString(std::string& output, std::string_view value)
        {
            output.push_back('"');
            for (char c : value)
            {
                switch (c)
                {
                    case '"':
                        output += "\\\"";
                        break;
                    case '\\':
                        output += "\\\\";
                        break;
                    case '\b':
                        output += "\\b";
                        break;
                    case '\f':
                        output += "\\f";
                        break;
                    case '\n':
                        output += "\\n";
                        break;
                    case '\r':
                        output += "\\r";
                        break;
                    case '\t':
                        output += "\\t";
                        break;
                    default:
                        if (static_cast<unsigned char>(c) < 0x20)
                        {
                            const auto byte = static_cast<unsigned char>(c);
                            output += "\\u00";
                            constexpr char kHex[] = "0123456789abcdef";
                            output.push_back(kHex[(byte >> 4) & 0xf]);
                            output.push_back(kHex[byte & 0xf]);
                        }
                        else
                        {
                            output.push_back(c);
                        }
                        break;
                }
            }
            output.push_back('"');
        }

        void AppendJsonValue(std::string& output, const Napi::Value& value, uint32_t depth = 0)
        {
            if (value.IsUndefined() || value.IsNull() || depth > 32)
            {
                output += "null";
                return;
            }

            if (auto* state = GetNativeHandleState(value); state != nullptr && state->Id != 0)
            {
                output += "{\"$nativeId\":";
                output += std::to_string(state->Id);
                output += ",\"$nativeKind\":";
                output += std::to_string(static_cast<uint32_t>(state->Kind));
                output += "}";
                return;
            }

            if (value.IsBoolean())
            {
                output += value.As<Napi::Boolean>().Value() ? "true" : "false";
                return;
            }

            if (value.IsNumber())
            {
                output += value.As<Napi::Number>().ToString().Utf8Value();
                return;
            }

            if (value.IsString())
            {
                AppendJsonString(output, value.As<Napi::String>().Utf8Value());
                return;
            }

            if (value.IsArray())
            {
                auto array = value.As<Napi::Array>();
                output.push_back('[');
                bool first = true;
                for (uint32_t i = 0; i < array.Length(); ++i)
                {
                    if (!first)
                    {
                        output.push_back(',');
                    }
                    first = false;
                    AppendJsonValue(output, array.Get(i), depth + 1);
                }
                output.push_back(']');
                return;
            }

            if (!value.IsObject() || value.IsArrayBuffer() || value.IsTypedArray() || value.IsDataView())
            {
                output += "null";
                return;
            }

            auto object = value.As<Napi::Object>();
            auto names = object.GetPropertyNames();
            output.push_back('{');
            bool first = true;
            for (uint32_t i = 0; i < names.Length(); ++i)
            {
                auto keyValue = names.Get(i);
                if (!keyValue.IsString())
                {
                    continue;
                }
                auto key = keyValue.As<Napi::String>().Utf8Value();
                if (key == JS_NATIVE_HANDLE_NAME)
                {
                    continue;
                }
                if (key == JS_NATIVE_HANDLE_ID_NAME)
                {
                    continue;
                }
                if (key == JS_NATIVE_HANDLE_KIND_NAME)
                {
                    continue;
                }

                auto propertyValue = object.Get(key);
                if (propertyValue.IsUndefined() || propertyValue.IsFunction())
                {
                    continue;
                }

                if (!first)
                {
                    output.push_back(',');
                }
                first = false;
                AppendJsonString(output, key);
                output.push_back(':');
                AppendJsonValue(output, propertyValue, depth + 1);
            }
            output.push_back('}');
        }

        std::string ToJson(const Napi::Value& value)
        {
            auto env = value.Env();
            try
            {
                auto nativeObject = JsRuntime::NativeObject::GetFromJavaScript(env);
                Napi::Function replacer{};
                if (nativeObject.Has(JS_NATIVE_JSON_REPLACER_NAME) &&
                    nativeObject.Get(JS_NATIVE_JSON_REPLACER_NAME).IsFunction())
                {
                    replacer = nativeObject.Get(JS_NATIVE_JSON_REPLACER_NAME).As<Napi::Function>();
                }
                else
                {
                    replacer = Napi::Function::New(env, [](const Napi::CallbackInfo& info) -> Napi::Value {
                        if (info.Length() > 1 && info[1].IsObject())
                        {
                            auto object = info[1].As<Napi::Object>();
                            if (object.Has(JS_NATIVE_HANDLE_ID_NAME) && object.Has(JS_NATIVE_HANDLE_KIND_NAME))
                            {
                                auto nativeId = object.Get(JS_NATIVE_HANDLE_ID_NAME);
                                auto nativeKind = object.Get(JS_NATIVE_HANDLE_KIND_NAME);
                                if (nativeId.IsNumber() && nativeKind.IsNumber())
                                {
                                    auto replacement = Napi::Object::New(info.Env());
                                    replacement.Set("$nativeId", nativeId);
                                    replacement.Set("$nativeKind", nativeKind);
                                    return replacement;
                                }
                            }
                        }
                        return info.Length() > 1 ? info[1] : info.Env().Undefined();
                    });
                    nativeObject.Set(JS_NATIVE_JSON_REPLACER_NAME, replacer);
                }

                auto jsonObjectValue = env.Global().Get("JSON");
                if (jsonObjectValue.IsObject())
                {
                    auto jsonObject = jsonObjectValue.As<Napi::Object>();
                    auto stringifyValue = jsonObject.Get("stringify");
                    if (stringifyValue.IsFunction())
                    {
                        auto result = stringifyValue.As<Napi::Function>().Call(jsonObject, {value, replacer});
                        if (result.IsString())
                        {
                            return result.As<Napi::String>().Utf8Value();
                        }
                    }
                }
            }
            catch (const Napi::Error&)
            {
                if (env.IsExceptionPending())
                {
                    env.GetAndClearPendingException();
                }
            }

            std::string output;
            output.reserve(1024);
            AppendJsonValue(output, value);
            return output;
        }

        ByteSpan GetByteSpan(const Napi::Value& value, size_t byteOffset = 0, std::optional<size_t> byteLength = std::nullopt)
        {
            const uint8_t* data{};
            size_t length{};
            if (value.IsArrayBuffer())
            {
                auto arrayBuffer = value.As<Napi::ArrayBuffer>();
                data = static_cast<const uint8_t*>(arrayBuffer.Data());
                length = arrayBuffer.ByteLength();
            }
            else if (value.IsTypedArray())
            {
                auto typedArray = value.As<Napi::TypedArray>();
                auto arrayBuffer = typedArray.ArrayBuffer();
                data = static_cast<const uint8_t*>(arrayBuffer.Data()) + typedArray.ByteOffset();
                length = typedArray.ByteLength();
            }
            else if (value.IsDataView())
            {
                auto dataView = value.As<Napi::DataView>();
                data = static_cast<const uint8_t*>(dataView.Data());
                length = dataView.ByteLength();
            }
            else
            {
                return {};
            }

            if (byteOffset > length)
            {
                return {};
            }
            data += byteOffset;
            length -= byteOffset;
            if (byteLength.has_value())
            {
                length = std::min(length, *byteLength);
            }
            return {data, length};
        }

        std::optional<Napi::Object> ExtractExternalImageSource(const Napi::Value& sourceDescriptor)
        {
            if (!sourceDescriptor.IsObject())
            {
                return std::nullopt;
            }

            auto descriptor = sourceDescriptor.As<Napi::Object>();
            if (descriptor.Has("source") && descriptor.Get("source").IsObject())
            {
                return descriptor.Get("source").As<Napi::Object>();
            }

            return descriptor;
        }

        struct ExternalImageData final
        {
            Napi::Object Payload;
            ByteSpan Bytes{};
            uint32_t Width{};
            uint32_t Height{};
            uint32_t OriginX{};
            uint32_t OriginY{};
            bool FlipY{};
        };

        std::optional<ExternalImageData> ExtractExternalImageData(const Napi::Value& sourceDescriptor)
        {
            auto sourceObject = ExtractExternalImageSource(sourceDescriptor);
            if (!sourceObject.has_value() ||
                !sourceObject->Has("_getNativeImageData") ||
                !sourceObject->Get("_getNativeImageData").IsFunction())
            {
                return std::nullopt;
            }

            auto payloadValue = sourceObject->Get("_getNativeImageData").As<Napi::Function>().Call(*sourceObject, {});
            if (!payloadValue.IsObject())
            {
                return std::nullopt;
            }

            auto payload = payloadValue.As<Napi::Object>();
            if (!payload.Has("data"))
            {
                return std::nullopt;
            }

            auto bytes = GetByteSpan(payload.Get("data"));
            const uint32_t width = payload.Has("width") ? ToUint32(payload.Get("width"), 1) : 1;
            const uint32_t height = payload.Has("height") ? ToUint32(payload.Get("height"), 1) : 1;
            if (bytes.Data == nullptr || bytes.Size == 0 || width == 0 || height == 0)
            {
                return std::nullopt;
            }

            auto descriptor = sourceDescriptor.As<Napi::Object>();
            uint32_t originX{};
            uint32_t originY{};
            if (descriptor.Has("origin"))
            {
                const auto origin = descriptor.Get("origin");
                originX = GetOriginCoordinate(origin, "x", 0);
                originY = GetOriginCoordinate(origin, "y", 1);
            }
            return ExternalImageData{
                std::move(payload),
                bytes,
                width,
                height,
                originX,
                originY,
                GetBool(descriptor, "flipY", false),
            };
        }

        bool ImportCanvasTexturePayload(const Napi::Object& payload)
        {
            if (!payload.Has("nativeTexture"))
            {
                return false;
            }

            const auto nativeTextureValue = payload.Get("nativeTexture");
            if (!nativeTextureValue.IsExternal())
            {
                return false;
            }

            const void* nativeTexture = nativeTextureValue.As<Napi::External<void>>().Data();
            if (nativeTexture == nullptr)
            {
                return false;
            }

            const uint32_t width = payload.Has("width") ? ToUint32(payload.Get("width"), 1) : 1;
            const uint32_t height = payload.Has("height") ? ToUint32(payload.Get("height"), 1) : 1;
            return babylon_wgpu_import_canvas_texture_from_native(nativeTexture, width, height);
        }

        std::optional<Napi::Object> ExtractCanvasTexturePayload(const Napi::Value& sourceDescriptor)
        {
            if (!sourceDescriptor.IsObject())
            {
                return std::nullopt;
            }

            auto descriptor = sourceDescriptor.As<Napi::Object>();
            if (descriptor.Has("nativeTexture") && descriptor.Get("nativeTexture").IsExternal())
            {
                return descriptor;
            }

            if (!descriptor.Has("source"))
            {
                return std::nullopt;
            }

            auto sourceValue = descriptor.Get("source");
            if (!sourceValue.IsObject())
            {
                return std::nullopt;
            }

            auto sourceObject = sourceValue.As<Napi::Object>();
            if (sourceObject.Has("nativeTexture") && sourceObject.Get("nativeTexture").IsExternal())
            {
                return sourceObject;
            }

            if (!sourceObject.Has("getCanvasTexture"))
            {
                return std::nullopt;
            }

            if (sourceObject.Has("getContext") && sourceObject.Get("getContext").IsFunction())
            {
                auto contextValue = sourceObject.Get("getContext").As<Napi::Function>().Call(
                    sourceObject,
                    {Napi::String::New(sourceObject.Env(), "2d")});
                if (contextValue.IsObject())
                {
                    auto contextObject = contextValue.As<Napi::Object>();
                    if (contextObject.Has("flush") && contextObject.Get("flush").IsFunction())
                    {
                        contextObject.Get("flush").As<Napi::Function>().Call(contextObject, {});
                    }
                }
            }

            auto getCanvasTextureValue = sourceObject.Get("getCanvasTexture");
            if (!getCanvasTextureValue.IsFunction())
            {
                return std::nullopt;
            }

            auto payloadValue = getCanvasTextureValue.As<Napi::Function>().Call(sourceObject, {});
            if (!payloadValue.IsObject())
            {
                return std::nullopt;
            }

            auto payloadObject = payloadValue.As<Napi::Object>();
            if (!payloadObject.Has("nativeTexture") || !payloadObject.Get("nativeTexture").IsExternal())
            {
                return std::nullopt;
            }

            return payloadObject;
        }

        void NoOpCallback(const Napi::CallbackInfo& info)
        {
            (void)info;
        }

        void MarkDrawRequestedCallback(const Napi::CallbackInfo& info)
        {
            (void)info;
            g_sawWebGpuDrawCall.store(true, std::memory_order_release);
            babylon_wgpu_mark_webgpu_draw_requested();
        }

        void MarkDrawCallCallback(const Napi::CallbackInfo& info)
        {
            (void)info;
            g_sawWebGpuDrawCall.store(true, std::memory_order_release);
            g_drawCallCount.fetch_add(1, std::memory_order_relaxed);
            babylon_wgpu_mark_webgpu_draw_requested();
        }

        Napi::Function GetCachedFunction(Napi::Env env, const char* key, void (*callback)(const Napi::CallbackInfo&))
        {
            auto nativeObject = JsRuntime::NativeObject::GetFromJavaScript(env);
            if (nativeObject.Has(key))
            {
                auto cached = nativeObject.Get(key);
                if (cached.IsFunction())
                {
                    return cached.As<Napi::Function>();
                }
            }

            auto function = Napi::Function::New(env, callback);
            nativeObject.Set(key, function);
            return function;
        }

        Napi::Function GetNoOpFunction(Napi::Env env)
        {
            return GetCachedFunction(env, "__nativeWebGpuNoOp", &NoOpCallback);
        }

        [[maybe_unused]] Napi::Function GetMarkDrawRequestedFunction(Napi::Env env)
        {
            return GetCachedFunction(env, "__nativeWebGpuMarkDrawRequested", &MarkDrawRequestedCallback);
        }

        constexpr bool kBuildEnableWebGpuDeveloperFeatures =
#if defined(BABYLON_NATIVE_ENABLE_WEBGPU_DEVELOPER_FEATURES) || defined(BABYLON_NATIVE_WEBGPU_TEST_HOOKS)
            true;
#else
            false;
#endif

        constexpr bool kBuildEnableUnsafeWebGpu =
#if defined(BABYLON_NATIVE_ENABLE_UNSAFE_WEBGPU) || defined(BABYLON_NATIVE_WEBGPU_TEST_HOOKS)
            true;
#else
            false;
#endif

        bool ReadBooleanFlag(const Napi::Object& object, const char* key)
        {
            if (!object.Has(key))
            {
                return false;
            }

            auto value = object.Get(key);
            if (value.IsBoolean())
            {
                return value.As<Napi::Boolean>().Value();
            }

            if (value.IsNumber())
            {
                return value.As<Napi::Number>().Int64Value() != 0;
            }

            if (value.IsString())
            {
                const auto text = value.As<Napi::String>().Utf8Value();
                return text == "1" || text == "true" || text == "on";
            }

            return false;
        }

        bool IsWebGpuDeveloperFeaturesEnabled(Napi::Env env)
        {
            if (kBuildEnableWebGpuDeveloperFeatures)
            {
                return true;
            }

            auto global = env.Global();
            // Chromium/WebKit-aligned naming for non-standard developer surfaces.
            static constexpr std::array<const char*, 4> kFlagNames{
                "__enableWebGPUDeveloperFeatures",
                "__webgpuDeveloperFeatures",
                "__webkitWebGPUDeveloperModeEnabled",
                "__webkitWebGPUDeveloperExtrasEnabled",
            };
            return std::any_of(kFlagNames.begin(), kFlagNames.end(), [&global](const char* flagName) {
                return ReadBooleanFlag(global, flagName);
            });
        }

        bool IsUnsafeWebGpuEnabled(Napi::Env env)
        {
            if (kBuildEnableUnsafeWebGpu)
            {
                return true;
            }

            auto global = env.Global();
            // Chromium-aligned "unsafe webgpu" naming for host-only interop hooks.
            static constexpr std::array<const char*, 3> kFlagNames{
                "__enableUnsafeWebGPU",
                "__unsafeWebGPU",
                "__allowUnsafeWebGPU",
            };
            return std::any_of(kFlagNames.begin(), kFlagNames.end(), [&global](const char* flagName) {
                return ReadBooleanFlag(global, flagName);
            });
        }

        using PromiseResolveFactory = std::function<Napi::Value(Napi::Env)>;

#ifdef BABYLON_NATIVE_WEBGPU_TEST_HOOKS
        struct FuturePromiseState final
        {
            std::future<std::string> Future{};
            std::shared_ptr<Napi::Promise::Deferred> Deferred{};
            PromiseResolveFactory ResolveFactory{};
            std::string OperationName{};
            std::string CallSiteStack{};
        };
#endif

        std::string CaptureCallSiteStack(Napi::Env env, const std::string& operationName)
        {
            auto errorValue = Napi::Error::New(env, operationName).Value();
            if (!errorValue.IsObject())
            {
                return {};
            }

            auto errorObject = errorValue.As<Napi::Object>();
            if (!errorObject.Has("stack"))
            {
                return {};
            }

            auto stackValue = errorObject.Get("stack");
            if (!stackValue.IsString())
            {
                return {};
            }

            return stackValue.As<Napi::String>().Utf8Value();
        }

        std::string MergeCallSiteStack(
            const std::string& errorMessage,
            const std::string& callSiteStack,
            const std::string& operationName,
            const char* nativeFramePrefix)
        {
            std::string stack{"Error: " + errorMessage};

            if (!callSiteStack.empty())
            {
                const auto newlineIndex = callSiteStack.find('\n');
                if (newlineIndex != std::string::npos)
                {
                    stack += callSiteStack.substr(newlineIndex);
                }
            }

            if (!operationName.empty())
            {
                stack += "\n    at ";
                stack += nativeFramePrefix;
                stack += operationName;
            }

            return stack;
        }

        Napi::Value CreateRejectedErrorValue(Napi::Env env, const std::string& errorMessage, const std::string& operationName, const std::string& callSiteStack)
        {
            auto rejectValue = Napi::Error::New(env, errorMessage).Value();
            if (!rejectValue.IsObject())
            {
                return rejectValue;
            }

            auto rejectObject = rejectValue.As<Napi::Object>();
            rejectObject.Set("nativeOperation", Napi::String::New(env, operationName));

            const auto mergedStack = MergeCallSiteStack(errorMessage, callSiteStack, operationName, "[native async] ");
            if (!mergedStack.empty())
            {
                rejectObject.Set("stack", Napi::String::New(env, mergedStack));
            }

            return rejectValue;
        }

        Napi::Error CreateNativeOperationError(Napi::Env env, const std::string& errorMessage, const std::string& operationName)
        {
            auto error = Napi::Error::New(env, errorMessage);
            auto errorValue = error.Value();
            if (errorValue.IsObject() && !operationName.empty())
            {
                auto errorObject = errorValue.As<Napi::Object>();
                errorObject.Set("nativeOperation", Napi::String::New(env, operationName));
                const auto callSiteStack = CaptureCallSiteStack(env, operationName);
                errorObject.Set(
                    "stack",
                    Napi::String::New(env, MergeCallSiteStack(errorMessage, callSiteStack, operationName, "[native] ")));
            }

            return error;
        }

        void ThrowNativeOperationError(Napi::Env env, const std::string& operationName, const std::string& errorMessage)
        {
            CreateNativeOperationError(env, errorMessage, operationName).ThrowAsJavaScriptException();
        }

        void ThrowNativeWebGpuError(Napi::Env env, const char* message, const char* operationName = nullptr)
        {
            ThrowNativeOperationError(env, operationName != nullptr ? operationName : "", NativeWebGpuErrorMessage(message));
        }

#ifdef BABYLON_NATIVE_WEBGPU_TEST_HOOKS
        void ScheduleFuturePromiseSettlement(
            Babylon::JsRuntime& runtime,
            std::shared_ptr<FuturePromiseState> state)
        {
            runtime.Dispatch([&runtime, state = std::move(state)](Napi::Env callbackEnv) {
                if (state->Future.wait_for(std::chrono::milliseconds{0}) != std::future_status::ready)
                {
                    ScheduleFuturePromiseSettlement(runtime, state);
                    return;
                }

                std::string errorMessage{};
                try
                {
                    errorMessage = state->Future.get();
                }
                catch (const std::exception& exception)
                {
                    errorMessage = exception.what();
                }
                catch (...)
                {
                    errorMessage = "Unknown asynchronous failure.";
                }

                Napi::HandleScope scope{callbackEnv};
                if (errorMessage.empty())
                {
                    try
                    {
                        state->Deferred->Resolve(state->ResolveFactory(callbackEnv));
                    }
                    catch (const Napi::Error& error)
                    {
                        errorMessage = error.Message();
                    }
                    catch (const std::exception& exception)
                    {
                        errorMessage = exception.what();
                    }
                    catch (...)
                    {
                        errorMessage = "Unknown JavaScript conversion failure.";
                    }
                }

                if (errorMessage.empty())
                {
                    return;
                }

                state->Deferred->Reject(CreateRejectedErrorValue(callbackEnv, errorMessage, state->OperationName, state->CallSiteStack));
            });
        }

        Napi::Promise ResolvePromiseFromFuture(
            Napi::Env env,
            std::future<std::string>&& future,
            PromiseResolveFactory resolveFactory,
            std::string operationName)
        {
            auto state = std::make_shared<FuturePromiseState>();
            state->Future = std::move(future);
            state->Deferred = std::make_shared<Napi::Promise::Deferred>(Napi::Promise::Deferred::New(env));
            state->ResolveFactory = std::move(resolveFactory);
            state->OperationName = std::move(operationName);
            state->CallSiteStack = CaptureCallSiteStack(env, state->OperationName);

            auto promise = state->Deferred->Promise();
            auto& runtime = Babylon::JsRuntime::GetFromJavaScript(env);
            ScheduleFuturePromiseSettlement(runtime, state);

            return promise;
        }
#endif

        Napi::Promise ResolvePromiseAsync(
            Napi::Env env,
            PromiseResolveFactory resolveFactory,
            std::string operationName)
        {
            auto deferred = std::make_shared<Napi::Promise::Deferred>(Napi::Promise::Deferred::New(env));
            auto callSiteStack = CaptureCallSiteStack(env, operationName);
            auto promise = deferred->Promise();
            auto& runtime = Babylon::JsRuntime::GetFromJavaScript(env);

            runtime.Dispatch([deferred = std::move(deferred),
                                 resolveFactory = std::move(resolveFactory),
                                 operationName = std::move(operationName),
                                 callSiteStack = std::move(callSiteStack)](Napi::Env callbackEnv) mutable {
                Napi::HandleScope scope{callbackEnv};

                try
                {
                    deferred->Resolve(resolveFactory(callbackEnv));
                    return;
                }
                catch (const Napi::Error& error)
                {
                    deferred->Reject(CreateRejectedErrorValue(callbackEnv, error.Message(), operationName, callSiteStack));
                    return;
                }
                catch (const std::exception& exception)
                {
                    deferred->Reject(CreateRejectedErrorValue(callbackEnv, exception.what(), operationName, callSiteStack));
                    return;
                }
                catch (...)
                {
                    deferred->Reject(CreateRejectedErrorValue(callbackEnv, "Unknown asynchronous failure.", operationName, callSiteStack));
                    return;
                }
            });

            return promise;
        }

        Napi::Value GetCachedResolvedUndefinedPromise(Napi::Env env)
        {
            constexpr auto CACHE_KEY = "__nativeWebGpuResolvedUndefinedPromise";
            auto global = env.Global();
            if (global.Has(CACHE_KEY))
            {
                auto cached = global.Get(CACHE_KEY);
                if (cached.IsObject())
                {
                    return cached;
                }
            }

            auto deferred = Napi::Promise::Deferred::New(env);
            deferred.Resolve(env.Undefined());
            auto promise = deferred.Promise();
            // Hot-path APIs (mapAsync/popErrorScope) can be
            // called every frame; reusing a settled Promise avoids per-frame churn.
            // wgpu-native currently reports NULL_FUTURE for these async C-ABI calls
            // on our target matrix, so completion is callback/immediate-driven and
            // we intentionally do not model per-call future identity here.
            // Non-CTS note: this is intentionally not per-call Promise identity.
            global.Set(CACHE_KEY, promise);
            return promise;
        }

        Napi::Value CreateNeverPromise(Napi::Env env)
        {
            auto promiseCtorValue = env.Global().Get("Promise");
            if (!promiseCtorValue.IsFunction())
            {
                return env.Undefined();
            }

            auto executor = Napi::Function::New(env, [](const Napi::CallbackInfo& info) {
                (void)info;
            });

            return promiseCtorValue.As<Napi::Function>().New({executor});
        }

        Napi::Object CreateSet(Napi::Env env)
        {
            auto setCtorValue = env.Global().Get("Set");
            if (!setCtorValue.IsFunction())
            {
                return Napi::Array::New(env);
            }

            return setCtorValue.As<Napi::Function>().New({});
        }

        // TODO(spec-compliance): These limits are hardcoded conservative defaults rather
        // than queried from the actual GPU adapter via the Rust backend. They should be
        // forwarded from the adapter's real limits once the FFI surface supports it.
        Napi::Object CreateLimits(Napi::Env env)
        {
            auto limits = Napi::Object::New(env);

            limits.Set("maxTextureDimension1D", Napi::Number::From(env, 8192));
            limits.Set("maxTextureDimension2D", Napi::Number::From(env, 8192));
            limits.Set("maxTextureDimension3D", Napi::Number::From(env, 2048));
            limits.Set("maxTextureArrayLayers", Napi::Number::From(env, 256));
            limits.Set("maxBindGroups", Napi::Number::From(env, 4));
            limits.Set("maxBindingsPerBindGroup", Napi::Number::From(env, 1000));
            limits.Set("maxDynamicUniformBuffersPerPipelineLayout", Napi::Number::From(env, 8));
            limits.Set("maxDynamicStorageBuffersPerPipelineLayout", Napi::Number::From(env, 4));
            limits.Set("maxSampledTexturesPerShaderStage", Napi::Number::From(env, 16));
            limits.Set("maxSamplersPerShaderStage", Napi::Number::From(env, 16));
            limits.Set("maxStorageBuffersPerShaderStage", Napi::Number::From(env, 8));
            limits.Set("maxStorageTexturesPerShaderStage", Napi::Number::From(env, 4));
            limits.Set("maxUniformBuffersPerShaderStage", Napi::Number::From(env, 12));
            limits.Set("maxUniformBufferBindingSize", Napi::Number::From(env, 65536));
            limits.Set("maxStorageBufferBindingSize", Napi::Number::From(env, 134217728));
            limits.Set("maxVertexBuffers", Napi::Number::From(env, 8));
            limits.Set("maxBufferSize", Napi::Number::From(env, 268435456));
            limits.Set("maxVertexAttributes", Napi::Number::From(env, 16));
            limits.Set("maxVertexBufferArrayStride", Napi::Number::From(env, 2048));
            limits.Set("maxInterStageShaderComponents", Napi::Number::From(env, 60));
            limits.Set("maxInterStageShaderVariables", Napi::Number::From(env, 15));
            limits.Set("maxColorAttachments", Napi::Number::From(env, 8));
            limits.Set("maxColorAttachmentBytesPerSample", Napi::Number::From(env, 32));
            limits.Set("maxComputeWorkgroupStorageSize", Napi::Number::From(env, 16384));
            limits.Set("maxComputeInvocationsPerWorkgroup", Napi::Number::From(env, 256));
            limits.Set("maxComputeWorkgroupSizeX", Napi::Number::From(env, 256));
            limits.Set("maxComputeWorkgroupSizeY", Napi::Number::From(env, 256));
            limits.Set("maxComputeWorkgroupSizeZ", Napi::Number::From(env, 64));
            limits.Set("maxComputeWorkgroupsPerDimension", Napi::Number::From(env, 65535));

            return limits;
        }

        TextureDescriptorData ParseTextureDescriptor(const Napi::CallbackInfo& info, uint32_t fallbackWidth = 1, uint32_t fallbackHeight = 1, const std::string& fallbackFormat = "bgra8unorm")
        {
            TextureDescriptorData descriptor{};
            descriptor.Width = fallbackWidth;
            descriptor.Height = fallbackHeight;
            descriptor.Format = fallbackFormat;

            if (info.Length() == 0 || !info[0].IsObject())
            {
                return descriptor;
            }

            const auto jsDescriptor = info[0].As<Napi::Object>();
            descriptor.Label = GetString(jsDescriptor, "label", "");
            descriptor.Format = GetString(jsDescriptor, "format", descriptor.Format);
            descriptor.Dimension = GetString(jsDescriptor, "dimension", descriptor.Dimension);
            descriptor.MipLevelCount = GetUint32(jsDescriptor, "mipLevelCount", descriptor.MipLevelCount);
            descriptor.SampleCount = GetUint32(jsDescriptor, "sampleCount", descriptor.SampleCount);
            descriptor.Usage = GetUint32(jsDescriptor, "usage", descriptor.Usage);

            if (jsDescriptor.Has("size"))
            {
                auto size = jsDescriptor.Get("size");
                if (size.IsArray())
                {
                    auto array = size.As<Napi::Array>();
                    descriptor.Width = array.Length() > 0 ? ToUint32(array.Get(static_cast<uint32_t>(0)), descriptor.Width) : descriptor.Width;
                    descriptor.Height = array.Length() > 1 ? ToUint32(array.Get(static_cast<uint32_t>(1)), descriptor.Height) : descriptor.Height;
                    descriptor.DepthOrArrayLayers = array.Length() > 2 ? ToUint32(array.Get(static_cast<uint32_t>(2)), descriptor.DepthOrArrayLayers) : descriptor.DepthOrArrayLayers;
                }
                else if (size.IsObject())
                {
                    auto sizeObject = size.As<Napi::Object>();
                    descriptor.Width = GetUint32(sizeObject, "width", descriptor.Width);
                    descriptor.Height = GetUint32(sizeObject, "height", descriptor.Height);
                    descriptor.DepthOrArrayLayers = GetUint32(sizeObject, "depthOrArrayLayers", descriptor.DepthOrArrayLayers);
                }
            }

            return descriptor;
        }

        Napi::Object CreateGpuTextureObject(Napi::Env env, const TextureDescriptorData& descriptor, uint64_t nativeId)
        {
            auto texture = Napi::Object::New(env);
            g_textureCreateCount.fetch_add(1, std::memory_order_relaxed);
            AttachNativeHandle(texture, NativeResourceKind::Texture, nativeId);

            texture.Set("label", Napi::String::New(env, descriptor.Label));
            texture.Set("format", Napi::String::New(env, descriptor.Format));
            texture.Set("dimension", Napi::String::New(env, descriptor.Dimension));
            texture.Set("width", Napi::Number::From(env, descriptor.Width));
            texture.Set("height", Napi::Number::From(env, descriptor.Height));
            texture.Set("depthOrArrayLayers", Napi::Number::From(env, descriptor.DepthOrArrayLayers));
            texture.Set("mipLevelCount", Napi::Number::From(env, descriptor.MipLevelCount));
            texture.Set("sampleCount", Napi::Number::From(env, descriptor.SampleCount));
            texture.Set("usage", Napi::Number::From(env, descriptor.Usage));
            // The Babylon render loop requests a default texture view every frame.
            // Cache the descriptor-less view to avoid transient JS allocations.
            texture.Set("__defaultView", env.Undefined());

            texture.Set("createView", Napi::Function::New(env, [descriptor](const Napi::CallbackInfo& viewInfo) -> Napi::Value {
                const bool hasDescriptor = viewInfo.Length() > 0 && viewInfo[0].IsObject();
                auto textureObject = viewInfo.This().As<Napi::Object>();
                if (!hasDescriptor && textureObject.Has("__defaultView"))
                {
                    auto cachedView = textureObject.Get("__defaultView");
                    if (cachedView.IsObject())
                    {
                        return cachedView;
                    }
                }

                auto viewFormat = descriptor.Format;
                auto viewDimension = descriptor.Dimension;
                auto viewAspect = std::string{"all"};
                auto baseMipLevel = uint32_t{0};
                auto mipLevelCount = descriptor.MipLevelCount;
                auto baseArrayLayer = uint32_t{0};
                auto arrayLayerCount = descriptor.DepthOrArrayLayers;
                double descriptorCacheHash{0.0};
                std::string viewLabel{};

                if (hasDescriptor)
                {
                    const auto viewDescriptor = viewInfo[0].As<Napi::Object>();
                    viewFormat = GetString(viewDescriptor, "format", viewFormat);
                    viewDimension = GetString(viewDescriptor, "dimension", viewDimension);
                    viewAspect = GetString(viewDescriptor, "aspect", viewAspect);
                    baseMipLevel = GetUint32(viewDescriptor, "baseMipLevel", baseMipLevel);
                    mipLevelCount = GetUint32(viewDescriptor, "mipLevelCount", mipLevelCount);
                    baseArrayLayer = GetUint32(viewDescriptor, "baseArrayLayer", baseArrayLayer);
                    arrayLayerCount = GetUint32(viewDescriptor, "arrayLayerCount", arrayLayerCount);
                    viewLabel = GetString(viewDescriptor, "label", "");

                    // Hash the descriptor components to avoid per-frame string
                    // allocation for cache key comparison. FNV-1a on the concatenated
                    // format|dimension|mipLevel|arrayLayer values.
                    auto fnvHash = [](std::string_view s, uint64_t h = 14695981039346656037ULL) -> uint64_t {
                        for (auto c : s) { h ^= static_cast<uint64_t>(c); h *= 1099511628211ULL; }
                        return h;
                    };
                    auto h = fnvHash(viewFormat);
                    h = fnvHash(viewDimension, h ^ 0xff);
                    h = fnvHash(viewAspect, h ^ 0xfe);
                    h ^= static_cast<uint64_t>(baseMipLevel) * 3266489917ULL;
                    h ^= static_cast<uint64_t>(mipLevelCount) * 2654435761ULL;
                    h ^= static_cast<uint64_t>(baseArrayLayer) * 668265263ULL;
                    h ^= static_cast<uint64_t>(arrayLayerCount) * 2246822519ULL;
                    descriptorCacheHash = static_cast<double>(h);

                    if (textureObject.Has("__descriptorViewKey"))
                    {
                        auto cachedKey = textureObject.Get("__descriptorViewKey");
                        if (cachedKey.IsNumber() &&
                            cachedKey.As<Napi::Number>().DoubleValue() == descriptorCacheHash &&
                            textureObject.Has("__descriptorView"))
                        {
                            auto cachedView = textureObject.Get("__descriptorView");
                            if (cachedView.IsObject())
                            {
                                return cachedView;
                            }
                        }
                    }

                }

                auto view = Napi::Object::New(viewInfo.Env());
                g_textureViewCreateCount.fetch_add(1, std::memory_order_relaxed);
                const auto textureId = GetNativeHandleId(textureObject, NativeResourceKind::Texture);
                const auto viewDescriptorJson = hasDescriptor ? ToJson(viewInfo[0]) : std::string{};
                if (IsWebGpuTraceEnabled())
                {
                    const auto textureLabel = textureObject.Has("label") && textureObject.Get("label").IsString()
                        ? textureObject.Get("label").As<Napi::String>().Utf8Value()
                        : std::string{};
                    std::fprintf(
                        stderr,
                        "NativeWebGPU trace createTextureView: texture=%llu label=%s desc=%s\n",
                        static_cast<unsigned long long>(textureId),
                        textureLabel.c_str(),
                        hasDescriptor ? viewDescriptorJson.c_str() : "{}");
                }
                const auto nativeViewId = babylon_wgpu_native_create_texture_view(
                    textureId,
                    hasDescriptor ? viewDescriptorJson.c_str() : nullptr);
                if (nativeViewId == 0)
                {
                    ThrowNativeWebGpuError(viewInfo.Env(), "NativeWebGPU failed to create GPUTextureView", "GPUTexture.createView");
                }
                AttachNativeHandle(view, NativeResourceKind::TextureView, nativeViewId);
                view.Set("label", Napi::String::New(viewInfo.Env(), viewLabel));

                view.Set("format", Napi::String::New(viewInfo.Env(), viewFormat));
                view.Set("dimension", Napi::String::New(viewInfo.Env(), viewDimension));
                view.Set("aspect", Napi::String::New(viewInfo.Env(), viewAspect));
                view.Set("baseMipLevel", Napi::Number::From(viewInfo.Env(), baseMipLevel));
                view.Set("mipLevelCount", Napi::Number::From(viewInfo.Env(), mipLevelCount));
                view.Set("baseArrayLayer", Napi::Number::From(viewInfo.Env(), baseArrayLayer));
                view.Set("arrayLayerCount", Napi::Number::From(viewInfo.Env(), arrayLayerCount));
                view.Set("texture", textureObject);

                if (!hasDescriptor)
                {
                    textureObject.Set("__defaultView", view);
                }
                else
                {
                    textureObject.Set("__descriptorViewKey", Napi::Number::New(viewInfo.Env(), descriptorCacheHash));
                    textureObject.Set("__descriptorView", view);
                }

                return view;
            }));

            texture.Set("destroy", Napi::Function::New(env, [](const Napi::CallbackInfo& destroyInfo) {
                if (auto* state = GetNativeHandleState(destroyInfo.This()))
                {
                    DestroyNativeHandleState(state);
                }
                destroyInfo.This().As<Napi::Object>().Set("__destroyed", Napi::Boolean::New(destroyInfo.Env(), true));
            }));

            return texture;
        }

        Napi::Object CreateGpuTexture(const Napi::CallbackInfo& info, const TextureDescriptorData& descriptor)
        {
            auto env = info.Env();
            const auto descriptorJson = (info.Length() > 0 && info[0].IsObject()) ? ToJson(info[0]) : std::string{"{}"};
            if (IsWebGpuTraceEnabled())
            {
                std::fprintf(stderr, "NativeWebGPU trace createTexture: %s\n", descriptorJson.c_str());
            }
            const auto nativeId = babylon_wgpu_native_create_texture(descriptorJson.c_str());
            if (nativeId == 0)
            {
                ThrowNativeWebGpuError(env, "NativeWebGPU failed to create GPUTexture.", "GPUDevice.createTexture");
            }
            return CreateGpuTextureObject(env, descriptor, nativeId);
        }

        Napi::Object CreateGpuRenderPassEncoder(Napi::Env env, uint64_t nativePassId)
        {
            auto pass = Napi::Object::New(env);
            AttachNativeHandle(pass, NativeResourceKind::RenderPass, nativePassId);

            pass.Set("setPipeline", Napi::Function::New(env, [](const Napi::CallbackInfo& info) {
                const auto passId = GetNativeHandleId(info.This(), NativeResourceKind::RenderPass);
                const auto pipelineId = info.Length() > 0 ? GetNativeHandleId(info[0], NativeResourceKind::RenderPipeline) : 0;
                if (babylon_wgpu_native_render_pass_set_pipeline(passId, pipelineId))
                {
                    MarkDrawRequestedCallback(info);
                }
            }));
            pass.Set("setBindGroup", Napi::Function::New(env, [](const Napi::CallbackInfo& info) {
                const auto passId = GetNativeHandleId(info.This(), NativeResourceKind::RenderPass);
                const auto index = info.Length() > 0 ? ToUint32(info[0], 0) : 0;
                const auto bindGroupId = info.Length() > 1 ? GetNativeHandleId(info[1], NativeResourceKind::BindGroup) : 0;
                auto dynamicOffsets = GetDynamicOffsets(info);
                babylon_wgpu_native_render_pass_set_bind_group(
                    passId,
                    index,
                    bindGroupId,
                    dynamicOffsets.empty() ? nullptr : dynamicOffsets.data(),
                    dynamicOffsets.size());
            }));
            pass.Set("setVertexBuffer", Napi::Function::New(env, [](const Napi::CallbackInfo& info) {
                const auto passId = GetNativeHandleId(info.This(), NativeResourceKind::RenderPass);
                const auto slot = info.Length() > 0 ? ToUint32(info[0], 0) : 0;
                const auto bufferId = info.Length() > 1 ? GetNativeHandleId(info[1], NativeResourceKind::Buffer) : 0;
                const auto offset = info.Length() > 2 && info[2].IsNumber() ? static_cast<uint64_t>(std::max<int64_t>(0, info[2].As<Napi::Number>().Int64Value())) : 0;
                const auto size = info.Length() > 3 && info[3].IsNumber() ? static_cast<uint64_t>(std::max<int64_t>(0, info[3].As<Napi::Number>().Int64Value())) : UINT64_MAX;
                babylon_wgpu_native_render_pass_set_vertex_buffer(passId, slot, bufferId, offset, size);
            }));
            pass.Set("setIndexBuffer", Napi::Function::New(env, [](const Napi::CallbackInfo& info) {
                const auto passId = GetNativeHandleId(info.This(), NativeResourceKind::RenderPass);
                const auto bufferId = info.Length() > 0 ? GetNativeHandleId(info[0], NativeResourceKind::Buffer) : 0;
                const auto format = info.Length() > 1 && info[1].IsString() ? info[1].As<Napi::String>().Utf8Value() : std::string{"uint16"};
                const auto offset = info.Length() > 2 && info[2].IsNumber() ? static_cast<uint64_t>(std::max<int64_t>(0, info[2].As<Napi::Number>().Int64Value())) : 0;
                const auto size = info.Length() > 3 && info[3].IsNumber() ? static_cast<uint64_t>(std::max<int64_t>(0, info[3].As<Napi::Number>().Int64Value())) : UINT64_MAX;
                babylon_wgpu_native_render_pass_set_index_buffer(passId, bufferId, format.c_str(), offset, size);
            }));
            pass.Set("setViewport", Napi::Function::New(env, [](const Napi::CallbackInfo& info) {
                const auto passId = GetNativeHandleId(info.This(), NativeResourceKind::RenderPass);
                const auto f = [&info](size_t index, double fallback) {
                    return info.Length() > index && info[index].IsNumber() ? info[index].As<Napi::Number>().DoubleValue() : fallback;
                };
                babylon_wgpu_native_render_pass_set_viewport(passId, static_cast<float>(f(0, 0)), static_cast<float>(f(1, 0)), static_cast<float>(f(2, 1)), static_cast<float>(f(3, 1)), static_cast<float>(f(4, 0)), static_cast<float>(f(5, 1)));
            }));
            pass.Set("setScissorRect", Napi::Function::New(env, [](const Napi::CallbackInfo& info) {
                const auto passId = GetNativeHandleId(info.This(), NativeResourceKind::RenderPass);
                const auto scissor = ReadScissorRect(info);
                babylon_wgpu_native_render_pass_set_scissor_rect(passId, scissor.X, scissor.Y, scissor.Width, scissor.Height);
            }));
            pass.Set("setStencilReference", Napi::Function::New(env, [](const Napi::CallbackInfo& info) {
                babylon_wgpu_native_render_pass_set_stencil_reference(
                    GetNativeHandleId(info.This(), NativeResourceKind::RenderPass),
                    info.Length() > 0 ? ToUint32(info[0], 0) : 0);
            }));
            pass.Set("setBlendConstant", Napi::Function::New(env, [](const Napi::CallbackInfo& info) {
                const auto passId = GetNativeHandleId(info.This(), NativeResourceKind::RenderPass);
                double r{}, g{}, b{}, a{1.0};
                if (info.Length() > 0 && info[0].IsObject())
                {
                    auto color = info[0].As<Napi::Object>();
                    r = color.Has("r") && color.Get("r").IsNumber() ? color.Get("r").As<Napi::Number>().DoubleValue() : 0.0;
                    g = color.Has("g") && color.Get("g").IsNumber() ? color.Get("g").As<Napi::Number>().DoubleValue() : 0.0;
                    b = color.Has("b") && color.Get("b").IsNumber() ? color.Get("b").As<Napi::Number>().DoubleValue() : 0.0;
                    a = color.Has("a") && color.Get("a").IsNumber() ? color.Get("a").As<Napi::Number>().DoubleValue() : 1.0;
                }
                babylon_wgpu_native_render_pass_set_blend_constant(passId, r, g, b, a);
            }));
            pass.Set("draw", Napi::Function::New(env, [](const Napi::CallbackInfo& info) {
                const auto passId = GetNativeHandleId(info.This(), NativeResourceKind::RenderPass);
                if (babylon_wgpu_native_render_pass_draw(
                        passId,
                        info.Length() > 0 ? ToUint32(info[0], 0) : 0,
                        info.Length() > 1 ? ToUint32(info[1], 1) : 1,
                        info.Length() > 2 ? ToUint32(info[2], 0) : 0,
                        info.Length() > 3 ? ToUint32(info[3], 0) : 0))
                {
                    MarkDrawCallCallback(info);
                }
            }));
            pass.Set("drawIndexed", Napi::Function::New(env, [](const Napi::CallbackInfo& info) {
                const auto passId = GetNativeHandleId(info.This(), NativeResourceKind::RenderPass);
                if (babylon_wgpu_native_render_pass_draw_indexed(
                        passId,
                        info.Length() > 0 ? ToUint32(info[0], 0) : 0,
                        info.Length() > 1 ? ToUint32(info[1], 1) : 1,
                        info.Length() > 2 ? ToUint32(info[2], 0) : 0,
                        info.Length() > 3 && info[3].IsNumber() ? static_cast<int32_t>(info[3].As<Napi::Number>().Int32Value()) : 0,
                        info.Length() > 4 ? ToUint32(info[4], 0) : 0))
                {
                    MarkDrawCallCallback(info);
                }
            }));
            pass.Set("drawIndirect", Napi::Function::New(env, [](const Napi::CallbackInfo& info) {
                const auto passId = GetNativeHandleId(info.This(), NativeResourceKind::RenderPass);
                const auto bufferId = info.Length() > 0 ? GetNativeHandleId(info[0], NativeResourceKind::Buffer) : 0;
                const auto offset = info.Length() > 1 && info[1].IsNumber()
                    ? static_cast<uint64_t>(std::max<int64_t>(0, info[1].As<Napi::Number>().Int64Value()))
                    : 0;
                if (babylon_wgpu_native_render_pass_draw_indirect(passId, bufferId, offset))
                {
                    MarkDrawCallCallback(info);
                }
            }));
            pass.Set("drawIndexedIndirect", Napi::Function::New(env, [](const Napi::CallbackInfo& info) {
                const auto passId = GetNativeHandleId(info.This(), NativeResourceKind::RenderPass);
                const auto bufferId = info.Length() > 0 ? GetNativeHandleId(info[0], NativeResourceKind::Buffer) : 0;
                const auto offset = info.Length() > 1 && info[1].IsNumber()
                    ? static_cast<uint64_t>(std::max<int64_t>(0, info[1].As<Napi::Number>().Int64Value()))
                    : 0;
                if (babylon_wgpu_native_render_pass_draw_indexed_indirect(passId, bufferId, offset))
                {
                    MarkDrawCallCallback(info);
                }
            }));
            pass.Set("executeBundles", Napi::Function::New(env, [](const Napi::CallbackInfo& info) {
                if (info.Length() == 0 || !info[0].IsArray())
                {
                    return;
                }

                const auto passId = GetNativeHandleId(info.This(), NativeResourceKind::RenderPass);
                const auto bundles = info[0].As<Napi::Array>();
                uint64_t bundledDrawCalls{};

                for (uint32_t i = 0; i < bundles.Length(); ++i)
                {
                    const auto bundleValue = bundles.Get(i);
                    if (!bundleValue.IsObject())
                    {
                        continue;
                    }

                    const auto bundle = bundleValue.As<Napi::Object>();
                    if (!bundle.Has("__drawCallCount") || !bundle.Get("__drawCallCount").IsNumber())
                    {
                        continue;
                    }

                    if (bundle.Has("__execute") && bundle.Get("__execute").IsFunction())
                    {
                        bundle.Get("__execute").As<Napi::Function>().Call(bundle, {
                            Napi::Number::From(info.Env(), static_cast<double>(passId)),
                        });
                    }

                    const auto drawCount = bundle.Get("__drawCallCount").As<Napi::Number>().Int64Value();
                    if (drawCount > 0)
                    {
                        bundledDrawCalls += static_cast<uint64_t>(drawCount);
                    }
                }

                if (bundledDrawCalls > 0)
                {
                    g_sawWebGpuDrawCall.store(true, std::memory_order_release);
                    g_drawCallCount.fetch_add(bundledDrawCalls, std::memory_order_relaxed);
                    babylon_wgpu_mark_webgpu_draw_requested();
                }
            }));
            pass.Set("end", Napi::Function::New(env, [](const Napi::CallbackInfo& info) {
                const auto passId = GetNativeHandleId(info.This(), NativeResourceKind::RenderPass);
                babylon_wgpu_native_render_pass_end(passId);
                if (auto* state = GetNativeHandleState(info.This()))
                {
                    state->Id = 0;
                }
            }));

            return pass;
        }

        Napi::Object CreateGpuRenderBundleEncoder(Napi::Env env)
        {
            auto encoder = Napi::Object::New(env);
            auto state = std::make_shared<RenderBundleState>();
            auto noOp = GetNoOpFunction(env);

            encoder.Set("setPipeline", Napi::Function::New(env, [state](const Napi::CallbackInfo& info) {
                const auto pipelineId = info.Length() > 0 ? GetNativeHandleId(info[0], NativeResourceKind::RenderPipeline) : 0;
                state->Commands.emplace_back([pipelineId](uint64_t passId) {
                    babylon_wgpu_native_render_pass_set_pipeline(passId, pipelineId);
                });
            }));
            encoder.Set("setBindGroup", Napi::Function::New(env, [state](const Napi::CallbackInfo& info) {
                const auto index = info.Length() > 0 ? ToUint32(info[0], 0) : 0;
                const auto bindGroupId = info.Length() > 1 ? GetNativeHandleId(info[1], NativeResourceKind::BindGroup) : 0;
                auto dynamicOffsets = GetDynamicOffsets(info);
                state->Commands.emplace_back([index, bindGroupId, dynamicOffsets = std::move(dynamicOffsets)](uint64_t passId) {
                    babylon_wgpu_native_render_pass_set_bind_group(
                        passId,
                        index,
                        bindGroupId,
                        dynamicOffsets.empty() ? nullptr : dynamicOffsets.data(),
                        dynamicOffsets.size());
                });
            }));
            encoder.Set("setVertexBuffer", Napi::Function::New(env, [state](const Napi::CallbackInfo& info) {
                const auto slot = info.Length() > 0 ? ToUint32(info[0], 0) : 0;
                const auto bufferId = info.Length() > 1 ? GetNativeHandleId(info[1], NativeResourceKind::Buffer) : 0;
                const auto offset = info.Length() > 2 && info[2].IsNumber() ? static_cast<uint64_t>(std::max<int64_t>(0, info[2].As<Napi::Number>().Int64Value())) : 0;
                const auto size = info.Length() > 3 && info[3].IsNumber() ? static_cast<uint64_t>(std::max<int64_t>(0, info[3].As<Napi::Number>().Int64Value())) : UINT64_MAX;
                state->Commands.emplace_back([slot, bufferId, offset, size](uint64_t passId) {
                    babylon_wgpu_native_render_pass_set_vertex_buffer(passId, slot, bufferId, offset, size);
                });
            }));
            encoder.Set("setIndexBuffer", Napi::Function::New(env, [state](const Napi::CallbackInfo& info) {
                const auto bufferId = info.Length() > 0 ? GetNativeHandleId(info[0], NativeResourceKind::Buffer) : 0;
                const auto format = info.Length() > 1 && info[1].IsString() ? info[1].As<Napi::String>().Utf8Value() : std::string{"uint16"};
                const auto offset = info.Length() > 2 && info[2].IsNumber() ? static_cast<uint64_t>(std::max<int64_t>(0, info[2].As<Napi::Number>().Int64Value())) : 0;
                const auto size = info.Length() > 3 && info[3].IsNumber() ? static_cast<uint64_t>(std::max<int64_t>(0, info[3].As<Napi::Number>().Int64Value())) : UINT64_MAX;
                state->Commands.emplace_back([bufferId, format, offset, size](uint64_t passId) {
                    babylon_wgpu_native_render_pass_set_index_buffer(passId, bufferId, format.c_str(), offset, size);
                });
            }));
            encoder.Set("setViewport", Napi::Function::New(env, [state](const Napi::CallbackInfo& info) {
                const auto f = [&info](size_t index, double fallback) {
                    return info.Length() > index && info[index].IsNumber() ? info[index].As<Napi::Number>().DoubleValue() : fallback;
                };
                const auto x = static_cast<float>(f(0, 0));
                const auto y = static_cast<float>(f(1, 0));
                const auto width = static_cast<float>(f(2, 1));
                const auto height = static_cast<float>(f(3, 1));
                const auto minDepth = static_cast<float>(f(4, 0));
                const auto maxDepth = static_cast<float>(f(5, 1));
                state->Commands.emplace_back([x, y, width, height, minDepth, maxDepth](uint64_t passId) {
                    babylon_wgpu_native_render_pass_set_viewport(passId, x, y, width, height, minDepth, maxDepth);
                });
            }));
            encoder.Set("setScissorRect", Napi::Function::New(env, [state](const Napi::CallbackInfo& info) {
                const auto scissor = ReadScissorRect(info);
                state->Commands.emplace_back([scissor](uint64_t passId) {
                    babylon_wgpu_native_render_pass_set_scissor_rect(passId, scissor.X, scissor.Y, scissor.Width, scissor.Height);
                });
            }));
            encoder.Set("setStencilReference", Napi::Function::New(env, [state](const Napi::CallbackInfo& info) {
                const auto reference = info.Length() > 0 ? ToUint32(info[0], 0) : 0;
                state->Commands.emplace_back([reference](uint64_t passId) {
                    babylon_wgpu_native_render_pass_set_stencil_reference(passId, reference);
                });
            }));
            encoder.Set("setBlendConstant", Napi::Function::New(env, [state](const Napi::CallbackInfo& info) {
                double r{}, g{}, b{}, a{1.0};
                if (info.Length() > 0 && info[0].IsObject())
                {
                    auto color = info[0].As<Napi::Object>();
                    r = color.Has("r") && color.Get("r").IsNumber() ? color.Get("r").As<Napi::Number>().DoubleValue() : 0.0;
                    g = color.Has("g") && color.Get("g").IsNumber() ? color.Get("g").As<Napi::Number>().DoubleValue() : 0.0;
                    b = color.Has("b") && color.Get("b").IsNumber() ? color.Get("b").As<Napi::Number>().DoubleValue() : 0.0;
                    a = color.Has("a") && color.Get("a").IsNumber() ? color.Get("a").As<Napi::Number>().DoubleValue() : 1.0;
                }
                state->Commands.emplace_back([r, g, b, a](uint64_t passId) {
                    babylon_wgpu_native_render_pass_set_blend_constant(passId, r, g, b, a);
                });
            }));
            encoder.Set("pushDebugGroup", noOp);
            encoder.Set("popDebugGroup", noOp);
            encoder.Set("insertDebugMarker", noOp);

            encoder.Set("draw", Napi::Function::New(env, [state](const Napi::CallbackInfo& info) {
                const auto vertexCount = info.Length() > 0 ? ToUint32(info[0], 0) : 0;
                const auto instanceCount = info.Length() > 1 ? ToUint32(info[1], 1) : 1;
                const auto firstVertex = info.Length() > 2 ? ToUint32(info[2], 0) : 0;
                const auto firstInstance = info.Length() > 3 ? ToUint32(info[3], 0) : 0;
                state->Commands.emplace_back([vertexCount, instanceCount, firstVertex, firstInstance](uint64_t passId) {
                    babylon_wgpu_native_render_pass_draw(
                        passId,
                        vertexCount,
                        instanceCount,
                        firstVertex,
                        firstInstance);
                });
                state->DrawCallCount += 1;
            }));
            encoder.Set("drawIndexed", Napi::Function::New(env, [state](const Napi::CallbackInfo& info) {
                const auto indexCount = info.Length() > 0 ? ToUint32(info[0], 0) : 0;
                const auto instanceCount = info.Length() > 1 ? ToUint32(info[1], 1) : 1;
                const auto firstIndex = info.Length() > 2 ? ToUint32(info[2], 0) : 0;
                const auto baseVertex = info.Length() > 3 && info[3].IsNumber() ? static_cast<int32_t>(info[3].As<Napi::Number>().Int32Value()) : 0;
                const auto firstInstance = info.Length() > 4 ? ToUint32(info[4], 0) : 0;
                state->Commands.emplace_back([indexCount, instanceCount, firstIndex, baseVertex, firstInstance](uint64_t passId) {
                    babylon_wgpu_native_render_pass_draw_indexed(
                        passId,
                        indexCount,
                        instanceCount,
                        firstIndex,
                        baseVertex,
                        firstInstance);
                });
                state->DrawCallCount += 1;
            }));
            encoder.Set("drawIndirect", Napi::Function::New(env, [state](const Napi::CallbackInfo& info) {
                const auto bufferId = info.Length() > 0 ? GetNativeHandleId(info[0], NativeResourceKind::Buffer) : 0;
                const auto offset = info.Length() > 1 && info[1].IsNumber()
                    ? static_cast<uint64_t>(std::max<int64_t>(0, info[1].As<Napi::Number>().Int64Value()))
                    : 0;
                state->Commands.emplace_back([bufferId, offset](uint64_t passId) {
                    babylon_wgpu_native_render_pass_draw_indirect(passId, bufferId, offset);
                });
                state->DrawCallCount += 1;
            }));
            encoder.Set("drawIndexedIndirect", Napi::Function::New(env, [state](const Napi::CallbackInfo& info) {
                const auto bufferId = info.Length() > 0 ? GetNativeHandleId(info[0], NativeResourceKind::Buffer) : 0;
                const auto offset = info.Length() > 1 && info[1].IsNumber()
                    ? static_cast<uint64_t>(std::max<int64_t>(0, info[1].As<Napi::Number>().Int64Value()))
                    : 0;
                state->Commands.emplace_back([bufferId, offset](uint64_t passId) {
                    babylon_wgpu_native_render_pass_draw_indexed_indirect(passId, bufferId, offset);
                });
                state->DrawCallCount += 1;
            }));

            encoder.Set("finish", Napi::Function::New(env, [state](const Napi::CallbackInfo& info) -> Napi::Value {
                auto bundle = Napi::Object::New(info.Env());
                bundle.Set("__drawCallCount", Napi::Number::From(info.Env(), static_cast<double>(state->DrawCallCount)));
                bundle.Set("__execute", Napi::Function::New(info.Env(), [state](const Napi::CallbackInfo& executeInfo) {
                    const auto passId = executeInfo.Length() > 0 && executeInfo[0].IsNumber()
                        ? static_cast<uint64_t>(std::max<int64_t>(0, executeInfo[0].As<Napi::Number>().Int64Value()))
                        : 0;
                    for (const auto& command : state->Commands)
                    {
                        command(passId);
                    }
                }));
                return bundle;
            }));

            return encoder;
        }

        Napi::Object CreateGpuComputePassEncoder(Napi::Env env, uint64_t nativePassId)
        {
            auto pass = Napi::Object::New(env);
            auto noOp = GetNoOpFunction(env);
            AttachNativeHandle(pass, NativeResourceKind::ComputePass, nativePassId);

            pass.Set("setPipeline", Napi::Function::New(env, [](const Napi::CallbackInfo& info) {
                const auto passId = GetNativeHandleId(info.This(), NativeResourceKind::ComputePass);
                const auto pipelineId = info.Length() > 0 ? GetNativeHandleId(info[0], NativeResourceKind::ComputePipeline) : 0;
                babylon_wgpu_native_compute_pass_set_pipeline(passId, pipelineId);
            }));
            pass.Set("setBindGroup", Napi::Function::New(env, [](const Napi::CallbackInfo& info) {
                const auto passId = GetNativeHandleId(info.This(), NativeResourceKind::ComputePass);
                const auto index = info.Length() > 0 ? ToUint32(info[0], 0) : 0;
                const auto bindGroupId = info.Length() > 1 ? GetNativeHandleId(info[1], NativeResourceKind::BindGroup) : 0;
                auto dynamicOffsets = GetDynamicOffsets(info);
                babylon_wgpu_native_compute_pass_set_bind_group(
                    passId,
                    index,
                    bindGroupId,
                    dynamicOffsets.empty() ? nullptr : dynamicOffsets.data(),
                    dynamicOffsets.size());
            }));
            pass.Set("dispatchWorkgroups", Napi::Function::New(env, [](const Napi::CallbackInfo& info) {
                babylon_wgpu_native_compute_pass_dispatch_workgroups(
                    GetNativeHandleId(info.This(), NativeResourceKind::ComputePass),
                    info.Length() > 0 ? ToUint32(info[0], 1) : 1,
                    info.Length() > 1 ? ToUint32(info[1], 1) : 1,
                    info.Length() > 2 ? ToUint32(info[2], 1) : 1);
            }));
            pass.Set("dispatchWorkgroupsIndirect", Napi::Function::New(env, [](const Napi::CallbackInfo& info) {
                const auto passId = GetNativeHandleId(info.This(), NativeResourceKind::ComputePass);
                const auto bufferId = info.Length() > 0 ? GetNativeHandleId(info[0], NativeResourceKind::Buffer) : 0;
                const auto offset = info.Length() > 1 && info[1].IsNumber()
                    ? static_cast<uint64_t>(std::max<int64_t>(0, info[1].As<Napi::Number>().Int64Value()))
                    : 0;
                babylon_wgpu_native_compute_pass_dispatch_workgroups_indirect(passId, bufferId, offset);
            }));
            pass.Set("end", Napi::Function::New(env, [](const Napi::CallbackInfo& info) {
                const auto passId = GetNativeHandleId(info.This(), NativeResourceKind::ComputePass);
                babylon_wgpu_native_compute_pass_end(passId);
                if (auto* state = GetNativeHandleState(info.This()))
                {
                    state->Id = 0;
                }
            }));
            pass.Set("pushDebugGroup", noOp);
            pass.Set("popDebugGroup", noOp);
            pass.Set("insertDebugMarker", noOp);

            return pass;
        }

        Napi::Object CreateGpuCommandEncoder(Napi::Env env)
        {
            auto encoder = Napi::Object::New(env);
            auto noOp = GetNoOpFunction(env);
            const auto nativeId = babylon_wgpu_native_create_command_encoder();
            if (nativeId == 0)
            {
                ThrowNativeWebGpuError(env, "NativeWebGPU failed to create GPUCommandEncoder.", "GPUDevice.createCommandEncoder");
            }
            AttachNativeHandle(encoder, NativeResourceKind::CommandEncoder, nativeId);

            encoder.Set("beginRenderPass", Napi::Function::New(env, [](const Napi::CallbackInfo& info) -> Napi::Value {
                g_renderPassBeginCount.fetch_add(1, std::memory_order_relaxed);
                const auto encoderId = GetNativeHandleId(info.This(), NativeResourceKind::CommandEncoder);
                const auto descriptorJson = info.Length() > 0 && info[0].IsObject() ? ToJson(info[0]) : std::string{"{}"};
                if (IsWebGpuTraceEnabled())
                {
                    std::fprintf(stderr, "NativeWebGPU trace beginRenderPass: %s\n", descriptorJson.c_str());
                }
                const auto passId = babylon_wgpu_native_command_encoder_begin_render_pass(encoderId, descriptorJson.c_str());
                if (passId == 0)
                {
                    ThrowNativeWebGpuError(info.Env(), "NativeWebGPU failed to begin GPURenderPassEncoder.", "GPUCommandEncoder.beginRenderPass");
                }
                return CreateGpuRenderPassEncoder(info.Env(), passId);
            }));
            encoder.Set("beginComputePass", Napi::Function::New(env, [](const Napi::CallbackInfo& info) -> Napi::Value {
                const auto encoderId = GetNativeHandleId(info.This(), NativeResourceKind::CommandEncoder);
                const auto descriptorJson = info.Length() > 0 && info[0].IsObject() ? ToJson(info[0]) : std::string{"{}"};
                const auto passId = babylon_wgpu_native_command_encoder_begin_compute_pass(encoderId, descriptorJson.c_str());
                if (passId == 0)
                {
                    ThrowNativeWebGpuError(info.Env(), "NativeWebGPU failed to begin GPUComputePassEncoder.", "GPUCommandEncoder.beginComputePass");
                }
                return CreateGpuComputePassEncoder(info.Env(), passId);
            }));

            encoder.Set("copyBufferToBuffer", Napi::Function::New(env, [](const Napi::CallbackInfo& info) {
                const auto encoderId = GetNativeHandleId(info.This(), NativeResourceKind::CommandEncoder);
                const auto sourceId = info.Length() > 0 ? GetNativeHandleId(info[0], NativeResourceKind::Buffer) : 0;
                const auto sourceOffset = info.Length() > 1 && info[1].IsNumber() ? static_cast<uint64_t>(std::max<int64_t>(0, info[1].As<Napi::Number>().Int64Value())) : 0;
                const auto destinationId = info.Length() > 2 ? GetNativeHandleId(info[2], NativeResourceKind::Buffer) : 0;
                const auto destinationOffset = info.Length() > 3 && info[3].IsNumber() ? static_cast<uint64_t>(std::max<int64_t>(0, info[3].As<Napi::Number>().Int64Value())) : 0;
                const auto size = info.Length() > 4 && info[4].IsNumber() ? static_cast<uint64_t>(std::max<int64_t>(0, info[4].As<Napi::Number>().Int64Value())) : 0;
                babylon_wgpu_native_command_encoder_copy_buffer_to_buffer(encoderId, sourceId, sourceOffset, destinationId, destinationOffset, size);
            }));
            encoder.Set("copyTextureToTexture", Napi::Function::New(env, [](const Napi::CallbackInfo& info) {
                const auto encoderId = GetNativeHandleId(info.This(), NativeResourceKind::CommandEncoder);
                const auto sourceJson = info.Length() > 0 ? ToJson(info[0]) : std::string{"{}"};
                const auto destinationJson = info.Length() > 1 ? ToJson(info[1]) : std::string{"{}"};
                const auto sizeJson = info.Length() > 2 ? ToJson(info[2]) : std::string{"{}"};
                babylon_wgpu_native_command_encoder_copy_texture_to_texture(encoderId, sourceJson.c_str(), destinationJson.c_str(), sizeJson.c_str());
            }));
            encoder.Set("copyTextureToBuffer", Napi::Function::New(env, [](const Napi::CallbackInfo& info) {
                const auto encoderId = GetNativeHandleId(info.This(), NativeResourceKind::CommandEncoder);
                const auto sourceJson = info.Length() > 0 ? ToJson(info[0]) : std::string{"{}"};
                const auto destinationJson = info.Length() > 1 ? ToJson(info[1]) : std::string{"{}"};
                const auto sizeJson = info.Length() > 2 ? ToJson(info[2]) : std::string{"{}"};
                babylon_wgpu_native_command_encoder_copy_texture_to_buffer(encoderId, sourceJson.c_str(), destinationJson.c_str(), sizeJson.c_str());
            }));
            encoder.Set("copyBufferToTexture", Napi::Function::New(env, [](const Napi::CallbackInfo& info) {
                const auto encoderId = GetNativeHandleId(info.This(), NativeResourceKind::CommandEncoder);
                const auto sourceJson = info.Length() > 0 ? ToJson(info[0]) : std::string{"{}"};
                const auto destinationJson = info.Length() > 1 ? ToJson(info[1]) : std::string{"{}"};
                const auto sizeJson = info.Length() > 2 ? ToJson(info[2]) : std::string{"{}"};
                babylon_wgpu_native_command_encoder_copy_buffer_to_texture(encoderId, sourceJson.c_str(), destinationJson.c_str(), sizeJson.c_str());
            }));
            encoder.Set("clearBuffer", Napi::Function::New(env, [](const Napi::CallbackInfo& info) {
                const auto encoderId = GetNativeHandleId(info.This(), NativeResourceKind::CommandEncoder);
                const auto bufferId = info.Length() > 0 ? GetNativeHandleId(info[0], NativeResourceKind::Buffer) : 0;
                const auto offset = info.Length() > 1 && info[1].IsNumber()
                    ? static_cast<uint64_t>(std::max<int64_t>(0, info[1].As<Napi::Number>().Int64Value()))
                    : 0;
                const auto size = info.Length() > 2 && info[2].IsNumber()
                    ? static_cast<uint64_t>(std::max<int64_t>(0, info[2].As<Napi::Number>().Int64Value()))
                    : UINT64_MAX;
                babylon_wgpu_native_command_encoder_clear_buffer(encoderId, bufferId, offset, size);
            }));
            encoder.Set("pushDebugGroup", noOp);
            encoder.Set("popDebugGroup", noOp);
            encoder.Set("insertDebugMarker", noOp);

            encoder.Set("finish", Napi::Function::New(env, [](const Napi::CallbackInfo& info) -> Napi::Value {
                const auto encoderId = GetNativeHandleId(info.This(), NativeResourceKind::CommandEncoder);
                const auto commandBufferId = babylon_wgpu_native_command_encoder_finish(encoderId);
                if (auto* state = GetNativeHandleState(info.This()))
                {
                    state->Id = 0;
                }
                if (commandBufferId == 0)
                {
                    ThrowNativeWebGpuError(info.Env(), "NativeWebGPU failed to finish GPUCommandEncoder.", "GPUCommandEncoder.finish");
                }
                auto commandBuffer = Napi::Object::New(info.Env());
                AttachNativeHandle(commandBuffer, NativeResourceKind::CommandBuffer, commandBufferId);
                return commandBuffer;
            }));

            return encoder;
        }

        Napi::Object CreateGpuShaderModule(Napi::Env env, std::string code)
        {
            auto shaderModule = Napi::Object::New(env);
            const auto nativeId = babylon_wgpu_native_create_shader_module(code.c_str());
            if (nativeId == 0)
            {
                ThrowNativeWebGpuError(env, "NativeWebGPU failed to create GPUShaderModule.", "GPUDevice.createShaderModule");
            }
            AttachNativeHandle(shaderModule, NativeResourceKind::ShaderModule, nativeId);
            shaderModule.Set("getCompilationInfo", Napi::Function::New(env, [](const Napi::CallbackInfo& info) -> Napi::Value {
                return ResolvePromiseAsync(info.Env(), [](Napi::Env callbackEnv) -> Napi::Value {
                    auto result = Napi::Object::New(callbackEnv);
                    result.Set("messages", Napi::Array::New(callbackEnv, 0));
                    return result;
                }, "GPUShaderModule.getCompilationInfo");
            }));
            return shaderModule;
        }

        Napi::Object CreateGpuRenderPipeline(Napi::Env env, const Napi::Value& descriptor)
        {
            auto pipeline = Napi::Object::New(env);
            const auto descriptorJson = descriptor.IsObject() ? ToJson(descriptor) : std::string{"{}"};
            const auto nativeId = babylon_wgpu_native_create_render_pipeline(descriptorJson.c_str());
            if (nativeId == 0)
            {
                ThrowNativeWebGpuError(env, "NativeWebGPU failed to create GPURenderPipeline.", "GPUDevice.createRenderPipeline");
            }
            AttachNativeHandle(pipeline, NativeResourceKind::RenderPipeline, nativeId);
            pipeline.Set("getBindGroupLayout", Napi::Function::New(env, [](const Napi::CallbackInfo& info) -> Napi::Value {
                const auto pipelineId = GetNativeHandleId(info.This(), NativeResourceKind::RenderPipeline);
                const auto index = info.Length() > 0 ? ToUint32(info[0], 0) : 0;
                const auto layoutId = babylon_wgpu_native_render_pipeline_get_bind_group_layout(pipelineId, index);
                auto layout = Napi::Object::New(info.Env());
                AttachNativeHandle(layout, NativeResourceKind::BindGroupLayout, layoutId);
                return layout;
            }));
            return pipeline;
        }

        Napi::Object CreateGpuComputePipeline(Napi::Env env, const Napi::Value& descriptor)
        {
            auto pipeline = Napi::Object::New(env);
            const auto descriptorJson = descriptor.IsObject() ? ToJson(descriptor) : std::string{"{}"};
            const auto nativeId = babylon_wgpu_native_create_compute_pipeline(descriptorJson.c_str());
            if (nativeId == 0)
            {
                ThrowNativeWebGpuError(env, "NativeWebGPU failed to create GPUComputePipeline.", "GPUDevice.createComputePipeline");
            }
            AttachNativeHandle(pipeline, NativeResourceKind::ComputePipeline, nativeId);
            pipeline.Set("getBindGroupLayout", Napi::Function::New(env, [](const Napi::CallbackInfo& info) -> Napi::Value {
                const auto pipelineId = GetNativeHandleId(info.This(), NativeResourceKind::ComputePipeline);
                const auto index = info.Length() > 0 ? ToUint32(info[0], 0) : 0;
                const auto layoutId = babylon_wgpu_native_compute_pipeline_get_bind_group_layout(pipelineId, index);
                auto layout = Napi::Object::New(info.Env());
                AttachNativeHandle(layout, NativeResourceKind::BindGroupLayout, layoutId);
                return layout;
            }));
            return pipeline;
        }

        Napi::Object CreateGpuBuffer(Napi::Env env, size_t size, uint32_t usage, bool mappedAtCreation)
        {
            auto buffer = Napi::Object::New(env);
            buffer.Set("size", Napi::Number::From(env, size));
            g_bufferCreateCount.fetch_add(1, std::memory_order_relaxed);
            g_bufferRequestedBytes.fetch_add(static_cast<uint64_t>(size), std::memory_order_relaxed);
            const auto nativeId = babylon_wgpu_native_create_buffer(static_cast<uint64_t>(size), usage, mappedAtCreation);
            if (nativeId == 0)
            {
                ThrowNativeWebGpuError(env, "NativeWebGPU failed to create GPUBuffer.", "GPUDevice.createBuffer");
            }
            AttachNativeHandle(buffer, NativeResourceKind::Buffer, nativeId, size);

            buffer.Set("mapAsync", Napi::Function::New(env, [](const Napi::CallbackInfo& info) -> Napi::Value {
                return GetCachedResolvedUndefinedPromise(info.Env());
            }));

            buffer.Set("getMappedRange", Napi::Function::New(env, [size](const Napi::CallbackInfo& info) -> Napi::Value {
                size_t offset{};
                size_t byteLength{size};

                if (info.Length() > 0 && info[0].IsNumber())
                {
                    offset = static_cast<size_t>(std::max<int64_t>(0, info[0].As<Napi::Number>().Int64Value()));
                }
                if (info.Length() > 1 && info[1].IsNumber())
                {
                    byteLength = static_cast<size_t>(std::max<int64_t>(0, info[1].As<Napi::Number>().Int64Value()));
                }
                else if (offset < size)
                {
                    byteLength = size - offset;
                }
                else
                {
                    byteLength = 0;
                }

                auto bufferObject = info.This().As<Napi::Object>();
                if (bufferObject.Has("__cachedMappedRange") &&
                    bufferObject.Has("__cachedMappedRangeOffset") &&
                    bufferObject.Has("__cachedMappedRangeLength"))
                {
                    const auto cachedOffsetValue = bufferObject.Get("__cachedMappedRangeOffset");
                    const auto cachedLengthValue = bufferObject.Get("__cachedMappedRangeLength");
                    const auto cachedRangeValue = bufferObject.Get("__cachedMappedRange");

                    if (cachedOffsetValue.IsNumber() &&
                        cachedLengthValue.IsNumber() &&
                        cachedRangeValue.IsArrayBuffer())
                    {
                        const auto cachedOffset = static_cast<size_t>(
                            std::max<int64_t>(0, cachedOffsetValue.As<Napi::Number>().Int64Value()));
                        const auto cachedLength = static_cast<size_t>(
                            std::max<int64_t>(0, cachedLengthValue.As<Napi::Number>().Int64Value()));

                        if (cachedOffset == offset && cachedLength == byteLength)
                        {
                            return cachedRangeValue;
                        }
                    }
                }

                auto mappedRange = Napi::ArrayBuffer::New(info.Env(), byteLength);
                // Hot-path optimization: Babylon can query mapped ranges every frame.
                // Reusing the same backing ArrayBuffer for identical range requests
                // avoids transient JS heap churn in simulator/device loops.
                // Non-CTS note: this intentionally keeps stable object identity.
                bufferObject.Set("__cachedMappedRange", mappedRange);
                bufferObject.Set("__cachedMappedRangeOffset",
                    Napi::Number::From(info.Env(), static_cast<double>(offset)));
                bufferObject.Set("__cachedMappedRangeLength",
                    Napi::Number::From(info.Env(), static_cast<double>(byteLength)));
                return mappedRange;
            }));

            buffer.Set("unmap", Napi::Function::New(env, [](const Napi::CallbackInfo& info) {
                auto bufferObject = info.This().As<Napi::Object>();
                auto* state = GetNativeHandleState(bufferObject);
                if (state == nullptr || state->Id == 0)
                {
                    return;
                }
                if (bufferObject.Has("__cachedMappedRange") && bufferObject.Get("__cachedMappedRange").IsArrayBuffer())
                {
                    auto arrayBuffer = bufferObject.Get("__cachedMappedRange").As<Napi::ArrayBuffer>();
                    size_t offset{};
                    if (bufferObject.Has("__cachedMappedRangeOffset") && bufferObject.Get("__cachedMappedRangeOffset").IsNumber())
                    {
                        offset = static_cast<size_t>(std::max<int64_t>(
                            0,
                            bufferObject.Get("__cachedMappedRangeOffset").As<Napi::Number>().Int64Value()));
                    }
                    const auto available = offset < state->Size ? state->Size - offset : 0;
                    const auto byteLength = std::min(arrayBuffer.ByteLength(), available);
                    babylon_wgpu_native_write_buffer(
                        state->Id,
                        offset,
                        static_cast<const uint8_t*>(arrayBuffer.Data()),
                        byteLength);
                }
                else
                {
                    babylon_wgpu_native_write_buffer(state->Id, 0, nullptr, 0);
                }
                state->Mapped = false;
            }));
            buffer.Set("destroy", Napi::Function::New(env, [](const Napi::CallbackInfo& info) {
                if (auto* state = GetNativeHandleState(info.This()))
                {
                    DestroyNativeHandleState(state);
                }
            }));

            return buffer;
        }

        Napi::Object CreateGpuQueue(Napi::Env env)
        {
            auto queue = Napi::Object::New(env);

            queue.Set("submit", Napi::Function::New(env, [](const Napi::CallbackInfo& info) {
                g_queueSubmitCount.fetch_add(1, std::memory_order_relaxed);
                std::vector<uint64_t> commandBufferIds;
                if (info.Length() > 0 && info[0].IsArray())
                {
                    auto array = info[0].As<Napi::Array>();
                    commandBufferIds.reserve(array.Length());
                    for (uint32_t i = 0; i < array.Length(); ++i)
                    {
                        const auto id = GetNativeHandleId(array.Get(i), NativeResourceKind::CommandBuffer);
                        if (id != 0)
                        {
                            commandBufferIds.push_back(id);
                            if (auto* state = GetNativeHandleState(array.Get(i)))
                            {
                                state->Id = 0;
                            }
                        }
                    }
                }
                if (IsWebGpuTraceEnabled())
                {
                    std::fprintf(stderr, "NativeWebGPU trace queue.submit: commandBuffers=%zu\n", commandBufferIds.size());
                }
                babylon_wgpu_native_queue_submit(
                    commandBufferIds.empty() ? nullptr : commandBufferIds.data(),
                    commandBufferIds.size());
                if (g_sawWebGpuDrawCall.load(std::memory_order_acquire))
                {
                    babylon_wgpu_mark_webgpu_draw_requested();
                }
            }));
            queue.Set("writeBuffer", Napi::Function::New(env, [](const Napi::CallbackInfo& info) {
                if (info.Length() < 3)
                {
                    return;
                }
                const auto bufferId = GetNativeHandleId(info[0], NativeResourceKind::Buffer);
                const auto bufferOffset = info[1].IsNumber() ? static_cast<uint64_t>(std::max<int64_t>(0, info[1].As<Napi::Number>().Int64Value())) : 0;
                const auto dataOffset = info.Length() > 3 && info[3].IsNumber() ? static_cast<size_t>(std::max<int64_t>(0, info[3].As<Napi::Number>().Int64Value())) : 0;
                const auto dataSize = info.Length() > 4 && info[4].IsNumber()
                    ? std::optional<size_t>{static_cast<size_t>(std::max<int64_t>(0, info[4].As<Napi::Number>().Int64Value()))}
                    : std::nullopt;
                auto bytes = GetByteSpan(info[2], dataOffset, dataSize);
                if (bytes.Data != nullptr && bytes.Size > 0)
                {
                    babylon_wgpu_native_write_buffer(bufferId, bufferOffset, bytes.Data, bytes.Size);
                }
            }));
            queue.Set("writeTexture", Napi::Function::New(env, [](const Napi::CallbackInfo& info) {
                if (info.Length() < 4)
                {
                    return;
                }
                const auto destinationJson = ToJson(info[0]);
                const auto layoutJson = ToJson(info[2]);
                const auto sizeJson = ToJson(info[3]);
                auto bytes = GetByteSpan(info[1]);
                if (bytes.Data != nullptr)
                {
                    if (IsWebGpuTraceEnabled())
                    {
                        std::fprintf(
                            stderr,
                            "NativeWebGPU trace writeTexture: dst=%s layout=%s size=%s bytes=%zu\n",
                            destinationJson.c_str(),
                            layoutJson.c_str(),
                            sizeJson.c_str(),
                            bytes.Size);
                    }
                    if (!babylon_wgpu_native_queue_write_texture(
                        destinationJson.c_str(),
                        bytes.Data,
                        bytes.Size,
                        layoutJson.c_str(),
                        sizeJson.c_str()))
                    {
                        ThrowNativeWebGpuError(info.Env(), "NativeWebGPU failed to write GPUTexture", "GPUQueue.writeTexture");
                    }
                }
            }));
            queue.Set("copyExternalImageToTexture", Napi::Function::New(env, [](const Napi::CallbackInfo& info) {
                if (info.Length() < 3)
                {
                    return;
                }

                // Standards-aligned bridge:
                // copyExternalImageToTexture({ source: canvasLike }, dst, size)
                // where `canvasLike` can expose getCanvasTexture() in this host.
                const auto destinationJson = ToJson(info[1]);
                const auto sizeJson = ToJson(info[2]);

                auto imageData = ExtractExternalImageData(info[0]);
                if (imageData.has_value())
                {
                    if (babylon_wgpu_native_queue_copy_external_image_rgba_to_texture(
                            imageData->Bytes.Data,
                            imageData->Bytes.Size,
                            imageData->Width,
                            imageData->Height,
                            imageData->OriginX,
                            imageData->OriginY,
                            imageData->FlipY ? 1u : 0u,
                            destinationJson.c_str(),
                            sizeJson.c_str()))
                    {
                        babylon_wgpu_mark_webgpu_draw_requested();
                        return;
                    }
                    ThrowNativeWebGpuError(info.Env(), "NativeWebGPU failed to copy external RGBA image to GPUTexture", "GPUQueue.copyExternalImageToTexture");
                    return;
                }

                auto payload = ExtractCanvasTexturePayload(info[0]);
                if (!payload.has_value())
                {
                    return;
                }

                const void* nativeTexture = nullptr;
                if (payload->Has("nativeTexture") && payload->Get("nativeTexture").IsExternal())
                {
                    nativeTexture = payload->Get("nativeTexture").As<Napi::External<void>>().Data();
                }
                const uint32_t width = payload->Has("width") ? ToUint32(payload->Get("width"), 1) : 1;
                const uint32_t height = payload->Has("height") ? ToUint32(payload->Get("height"), 1) : 1;

                if (nativeTexture != nullptr &&
                    babylon_wgpu_native_queue_copy_external_image_to_texture(
                        nativeTexture,
                        width,
                        height,
                        destinationJson.c_str(),
                        sizeJson.c_str()))
                {
                    babylon_wgpu_mark_webgpu_draw_requested();
                    return;
                }
                ThrowNativeWebGpuError(info.Env(), "NativeWebGPU failed to copy external native image to GPUTexture", "GPUQueue.copyExternalImageToTexture");
            }));
            queue.Set("onSubmittedWorkDone", Napi::Function::New(env, [](const Napi::CallbackInfo& info) -> Napi::Value {
                return ResolvePromiseAsync(info.Env(), [](Napi::Env callbackEnv) -> Napi::Value {
                    if (!babylon_wgpu_native_queue_wait_submitted_work())
                    {
                        throw std::runtime_error{"NativeWebGPU failed while waiting for submitted GPU work."};
                    }
                    return callbackEnv.Undefined();
                }, "GPUQueue.onSubmittedWorkDone");
            }));

            return queue;
        }

        Napi::Object CreateGpuDevice(Napi::Env env)
        {
            auto device = Napi::Object::New(env);
            auto noOp = GetNoOpFunction(env);

            device.Set("features", CreateSet(env));
            device.Set("limits", CreateLimits(env));
            device.Set("queue", CreateGpuQueue(env));
            // TODO(spec-compliance): device.lost is a never-resolving promise. The shim
            // does not model device loss. When the Rust backend detects device loss (e.g.
            // adapter removal), this should resolve with a GPUDeviceLostInfo.
            device.Set("lost", CreateNeverPromise(env));

            device.Set("addEventListener", noOp);
            device.Set("removeEventListener", noOp);
            device.Set("destroy", noOp);
            // TODO(spec-compliance): Error scopes are completely opaque -- pushErrorScope
            // is a no-op and popErrorScope always resolves with undefined. GPU validation
            // errors from the Rust backend are never surfaced to JS. This should forward
            // to the wgpu device's error callback once the FFI supports it.
            device.Set("pushErrorScope", noOp);

            device.Set("popErrorScope", Napi::Function::New(env, [](const Napi::CallbackInfo& info) -> Napi::Value {
                return GetCachedResolvedUndefinedPromise(info.Env());
            }));

            device.Set("createBuffer", Napi::Function::New(env, [](const Napi::CallbackInfo& info) -> Napi::Value {
                size_t size{};
                uint32_t usage{};
                bool mappedAtCreation{};
                if (info.Length() > 0 && info[0].IsObject())
                {
                    auto descriptor = info[0].As<Napi::Object>();
                    if (descriptor.Has("size") && descriptor.Get("size").IsNumber())
                    {
                        size = static_cast<size_t>(std::max<int64_t>(0, descriptor.Get("size").As<Napi::Number>().Int64Value()));
                    }
                    usage = GetUint32(descriptor, "usage", usage);
                    if (descriptor.Has("mappedAtCreation") && descriptor.Get("mappedAtCreation").IsBoolean())
                    {
                        mappedAtCreation = descriptor.Get("mappedAtCreation").As<Napi::Boolean>().Value();
                    }
                }
                return CreateGpuBuffer(info.Env(), size, usage, mappedAtCreation);
            }));

            device.Set("createTexture", Napi::Function::New(env, [](const Napi::CallbackInfo& info) -> Napi::Value {
                auto descriptor = ParseTextureDescriptor(info);
                return CreateGpuTexture(info, descriptor);
            }));

            device.Set("createSampler", Napi::Function::New(env, [](const Napi::CallbackInfo& info) -> Napi::Value {
                auto sampler = Napi::Object::New(info.Env());
                const auto descriptorJson = info.Length() > 0 && info[0].IsObject() ? ToJson(info[0]) : std::string{"{}"};
                if (IsWebGpuTraceEnabled())
                {
                    std::fprintf(stderr, "NativeWebGPU trace createSampler: %s\n", descriptorJson.c_str());
                }
                const auto nativeId = babylon_wgpu_native_create_sampler(descriptorJson.c_str());
                if (nativeId == 0)
                {
                    ThrowNativeWebGpuError(info.Env(), "NativeWebGPU failed to create GPUSampler.", "GPUDevice.createSampler");
                }
                AttachNativeHandle(sampler, NativeResourceKind::Sampler, nativeId);
                if (info.Length() > 0 && info[0].IsObject())
                {
                    sampler.Set("label", Napi::String::New(info.Env(), GetString(info[0].As<Napi::Object>(), "label", "")));
                }
                return sampler;
            }));

            device.Set("createShaderModule", Napi::Function::New(env, [](const Napi::CallbackInfo& info) -> Napi::Value {
                std::string code{};
                if (info.Length() > 0 && info[0].IsObject())
                {
                    const auto descriptor = info[0].As<Napi::Object>();
                    code = GetString(descriptor, "code", "");
                }

                auto module = CreateGpuShaderModule(info.Env(), std::move(code));
                if (IsWebGpuTraceEnabled() && module.Has(JS_NATIVE_HANDLE_ID_NAME))
                {
                    const auto nativeId = module.Get(JS_NATIVE_HANDLE_ID_NAME).As<Napi::Number>().Int64Value();
                    std::fprintf(
                        stderr,
                        "NativeWebGPU trace createShaderModule: id=%lld\n",
                        static_cast<long long>(nativeId));
                }
                return module;
            }));

            device.Set("createCommandEncoder", Napi::Function::New(env, [](const Napi::CallbackInfo& info) -> Napi::Value {
                g_commandEncoderCreateCount.fetch_add(1, std::memory_order_relaxed);
                return CreateGpuCommandEncoder(info.Env());
            }));

            device.Set("createBindGroupLayout", Napi::Function::New(env, [](const Napi::CallbackInfo& info) -> Napi::Value {
                auto layout = Napi::Object::New(info.Env());
                const auto descriptorJson = info.Length() > 0 && info[0].IsObject() ? ToJson(info[0]) : std::string{"{}"};
                if (IsWebGpuTraceEnabled())
                {
                    std::fprintf(stderr, "NativeWebGPU trace createBindGroupLayout: %s\n", descriptorJson.c_str());
                }
                const auto nativeId = babylon_wgpu_native_create_bind_group_layout(descriptorJson.c_str());
                if (nativeId == 0)
                {
                    ThrowNativeWebGpuError(info.Env(), "NativeWebGPU failed to create GPUBindGroupLayout.", "GPUDevice.createBindGroupLayout");
                }
                AttachNativeHandle(layout, NativeResourceKind::BindGroupLayout, nativeId);
                return layout;
            }));

            device.Set("createPipelineLayout", Napi::Function::New(env, [](const Napi::CallbackInfo& info) -> Napi::Value {
                auto layout = Napi::Object::New(info.Env());
                const auto descriptorJson = info.Length() > 0 && info[0].IsObject() ? ToJson(info[0]) : std::string{"{}"};
                const auto nativeId = babylon_wgpu_native_create_pipeline_layout(descriptorJson.c_str());
                if (nativeId == 0)
                {
                    ThrowNativeWebGpuError(info.Env(), "NativeWebGPU failed to create GPUPipelineLayout.", "GPUDevice.createPipelineLayout");
                }
                AttachNativeHandle(layout, NativeResourceKind::PipelineLayout, nativeId);
                return layout;
            }));

            device.Set("createBindGroup", Napi::Function::New(env, [](const Napi::CallbackInfo& info) -> Napi::Value {
                g_bindGroupCreateCount.fetch_add(1, std::memory_order_relaxed);
                auto bindGroup = Napi::Object::New(info.Env());
                const auto descriptorJson = info.Length() > 0 && info[0].IsObject() ? ToJson(info[0]) : std::string{"{}"};
                if (IsWebGpuTraceEnabled())
                {
                    std::fprintf(stderr, "NativeWebGPU trace createBindGroup: %s\n", descriptorJson.c_str());
                }
                const auto nativeId = babylon_wgpu_native_create_bind_group(descriptorJson.c_str());
                if (nativeId == 0)
                {
                    ThrowNativeWebGpuError(info.Env(), "NativeWebGPU failed to create GPUBindGroup.", "GPUDevice.createBindGroup");
                }
                AttachNativeHandle(bindGroup, NativeResourceKind::BindGroup, nativeId);
                return bindGroup;
            }));

            device.Set("createRenderPipeline", Napi::Function::New(env, [](const Napi::CallbackInfo& info) -> Napi::Value {
                g_renderPipelineCreateCount.fetch_add(1, std::memory_order_relaxed);
                if (IsWebGpuTraceEnabled())
                {
                    const auto descriptorJson = info.Length() > 0 && info[0].IsObject() ? ToJson(info[0]) : std::string{"{}"};
                    std::fprintf(stderr, "NativeWebGPU trace createRenderPipeline: %s\n", descriptorJson.c_str());
                }
                return CreateGpuRenderPipeline(info.Env(), info.Length() > 0 ? info[0] : info.Env().Undefined());
            }));

            device.Set("createRenderPipelineAsync", Napi::Function::New(env, [](const Napi::CallbackInfo& info) -> Napi::Value {
                g_renderPipelineCreateCount.fetch_add(1, std::memory_order_relaxed);
                const auto descriptorJson = info.Length() > 0 && info[0].IsObject() ? ToJson(info[0]) : std::string{};
                return ResolvePromiseAsync(info.Env(), [descriptorJson](Napi::Env callbackEnv) -> Napi::Value {
                    if (descriptorJson.empty())
                    {
                        throw std::runtime_error{"createRenderPipelineAsync requires a descriptor object."};
                    }
                    auto descriptorValue = Napi::String::New(callbackEnv, descriptorJson);
                    const auto nativeId = babylon_wgpu_native_create_render_pipeline(descriptorJson.c_str());
                    if (nativeId == 0)
                    {
                        throw std::runtime_error{NativeWebGpuErrorMessage("NativeWebGPU failed to create GPURenderPipeline.")};
                    }
                    auto pipeline = Napi::Object::New(callbackEnv);
                    AttachNativeHandle(pipeline, NativeResourceKind::RenderPipeline, nativeId);
                    pipeline.Set("getBindGroupLayout", Napi::Function::New(callbackEnv, [](const Napi::CallbackInfo& nestedInfo) -> Napi::Value {
                        const auto pipelineId = GetNativeHandleId(nestedInfo.This(), NativeResourceKind::RenderPipeline);
                        const auto index = nestedInfo.Length() > 0 ? ToUint32(nestedInfo[0], 0) : 0;
                        const auto layoutId = babylon_wgpu_native_render_pipeline_get_bind_group_layout(pipelineId, index);
                        auto layout = Napi::Object::New(nestedInfo.Env());
                        AttachNativeHandle(layout, NativeResourceKind::BindGroupLayout, layoutId);
                        return layout;
                    }));
                    (void)descriptorValue;
                    return pipeline;
                }, "GPUDevice.createRenderPipelineAsync");
            }));

            device.Set("createRenderBundleEncoder", Napi::Function::New(env, [](const Napi::CallbackInfo& info) -> Napi::Value {
                (void)info;
                return CreateGpuRenderBundleEncoder(info.Env());
            }));

            device.Set("createComputePipeline", Napi::Function::New(env, [](const Napi::CallbackInfo& info) -> Napi::Value {
                if (IsWebGpuTraceEnabled())
                {
                    const auto descriptorJson = info.Length() > 0 && info[0].IsObject() ? ToJson(info[0]) : std::string{"{}"};
                    std::fprintf(stderr, "NativeWebGPU trace createComputePipeline: %s\n", descriptorJson.c_str());
                }
                return CreateGpuComputePipeline(info.Env(), info.Length() > 0 ? info[0] : info.Env().Undefined());
            }));

            device.Set("createComputePipelineAsync", Napi::Function::New(env, [](const Napi::CallbackInfo& info) -> Napi::Value {
                const auto descriptorJson = info.Length() > 0 && info[0].IsObject() ? ToJson(info[0]) : std::string{};
                return ResolvePromiseAsync(info.Env(), [descriptorJson](Napi::Env callbackEnv) -> Napi::Value {
                    if (descriptorJson.empty())
                    {
                        throw std::runtime_error{"createComputePipelineAsync requires a descriptor object."};
                    }
                    const auto nativeId = babylon_wgpu_native_create_compute_pipeline(descriptorJson.c_str());
                    if (nativeId == 0)
                    {
                        throw std::runtime_error{NativeWebGpuErrorMessage("NativeWebGPU failed to create GPUComputePipeline.")};
                    }
                    auto pipeline = Napi::Object::New(callbackEnv);
                    AttachNativeHandle(pipeline, NativeResourceKind::ComputePipeline, nativeId);
                    pipeline.Set("getBindGroupLayout", Napi::Function::New(callbackEnv, [](const Napi::CallbackInfo& nestedInfo) -> Napi::Value {
                        const auto pipelineId = GetNativeHandleId(nestedInfo.This(), NativeResourceKind::ComputePipeline);
                        const auto index = nestedInfo.Length() > 0 ? ToUint32(nestedInfo[0], 0) : 0;
                        const auto layoutId = babylon_wgpu_native_compute_pipeline_get_bind_group_layout(pipelineId, index);
                        auto layout = Napi::Object::New(nestedInfo.Env());
                        AttachNativeHandle(layout, NativeResourceKind::BindGroupLayout, layoutId);
                        return layout;
                    }));
                    return pipeline;
                }, "GPUDevice.createComputePipelineAsync");
            }));

            device.Set("createQuerySet", Napi::Function::New(env, [](const Napi::CallbackInfo& info) -> Napi::Value {
                auto querySet = Napi::Object::New(info.Env());
                querySet.Set("destroy", Napi::Function::New(info.Env(), [](const Napi::CallbackInfo& nestedInfo) {
                    (void)nestedInfo;
                }));
                return querySet;
            }));

            return device;
        }

        Napi::Object CreateGpuCanvasContext(Napi::Env env)
        {
            auto context = Napi::Object::New(env);
            auto state = std::make_shared<CanvasContextState>();
            state->CanvasId = g_nextCanvasContextId.fetch_add(1, std::memory_order_relaxed);

            context.Set("configure", Napi::Function::New(env, [state](const Napi::CallbackInfo& info) {
                if (state->Destroyed)
                {
                    return;
                }

                if (info.Length() > 0 && info[0].IsObject())
                {
                    auto descriptor = info[0].As<Napi::Object>();
                    state->Format = GetString(descriptor, "format", state->Format);
                    state->Usage = GetUint32(descriptor, "usage", state->Usage);
                    state->Width = GetUint32(descriptor, "width", state->Width);
                    state->Height = GetUint32(descriptor, "height", state->Height);
                    if (descriptor.Has("size"))
                    {
                        auto sizeValue = descriptor.Get("size");
                        if (sizeValue.IsObject())
                        {
                            auto sizeObject = sizeValue.As<Napi::Object>();
                            state->Width = GetUint32(sizeObject, "width", state->Width);
                            state->Height = GetUint32(sizeObject, "height", state->Height);
                        }
                    }
                }

                state->Configured = true;
                state->CachedTexture.Reset();
            }));

            context.Set("unconfigure", Napi::Function::New(env, [state](const Napi::CallbackInfo& info) {
                (void)info;
                if (state->Destroyed)
                {
                    return;
                }

                state->Configured = false;
                state->CachedTexture.Reset();
                babylon_wgpu_native_canvas_destroy(state->CanvasId);
            }));

            context.Set("getCurrentTexture", Napi::Function::New(env, [state](const Napi::CallbackInfo& info) -> Napi::Value {
                if (state->Destroyed)
                {
                    return info.Env().Undefined();
                }

                TextureDescriptorData descriptor{};
                auto contextObject = info.This().As<Napi::Object>();
                if (contextObject.Has("canvas") && contextObject.Get("canvas").IsObject())
                {
                    auto canvas = contextObject.Get("canvas").As<Napi::Object>();
                    state->Width = GetUint32(canvas, "width", state->Width);
                    state->Height = GetUint32(canvas, "height", state->Height);
                }

                descriptor.Format = state->Format;
                descriptor.Width = state->Width;
                descriptor.Height = state->Height;
                descriptor.Usage = state->Usage;
                descriptor.Label = state->Configured ? "swapchain.current" : "swapchain.unconfigured";

                const auto nativeId = babylon_wgpu_native_canvas_get_current_texture(
                    state->CanvasId,
                    descriptor.Width,
                    descriptor.Height,
                    descriptor.Format.c_str(),
                    descriptor.Usage);
                if (nativeId == 0)
                {
                    ThrowNativeWebGpuError(info.Env(), "NativeWebGPU failed to acquire current canvas texture.", "GPUCanvasContext.getCurrentTexture");
                }
                if (!state->CachedTexture.IsEmpty())
                {
                    auto cached = state->CachedTexture.Value();
                    if (GetNativeHandleId(cached, NativeResourceKind::Texture) == nativeId)
                    {
                        return cached;
                    }
                    state->CachedTexture.Reset();
                }

                auto texture = CreateGpuTextureObject(info.Env(), descriptor, nativeId);
                state->CachedTexture = Napi::Persistent(texture);
                return texture;
            }));

            context.Set("label", Napi::String::New(env, ""));
            context.Set("canvas", env.Undefined());
            context.Set("destroy", Napi::Function::New(env, [state](const Napi::CallbackInfo& info) {
                (void)info;
                if (state->Destroyed)
                {
                    return;
                }

                state->Destroyed = true;
                state->Configured = false;
                state->CachedTexture.Reset();
                babylon_wgpu_native_canvas_destroy(state->CanvasId);
                state->Format = "bgra8unorm";
                state->Width = 1;
                state->Height = 1;
                state->Usage = 16;
            }));

            return context;
        }

        Napi::Object CreateGpuAdapter(Napi::Env env)
        {
            auto adapter = Napi::Object::New(env);

            adapter.Set("features", CreateSet(env));
            adapter.Set("limits", CreateLimits(env));
            adapter.Set("isFallbackAdapter", Napi::Boolean::New(env, false));

            auto info = Napi::Object::New(env);
            info.Set("vendor", Napi::String::New(env, "BabylonNative"));
            info.Set("architecture", Napi::String::New(env, "wgpu"));
            info.Set("description", Napi::String::New(env, "BabylonNative WGPU adapter"));
            info.Set("device", Napi::String::New(env, "0"));
            adapter.Set("info", info);

            adapter.Set("requestAdapterInfo", Napi::Function::New(env, [](const Napi::CallbackInfo& callbackInfo) -> Napi::Value {
                return ResolvePromiseAsync(callbackInfo.Env(), [](Napi::Env callbackEnv) {
                    auto adapterInfo = Napi::Object::New(callbackEnv);
                    adapterInfo.Set("vendor", Napi::String::New(callbackEnv, "BabylonNative"));
                    adapterInfo.Set("architecture", Napi::String::New(callbackEnv, "wgpu"));
                    adapterInfo.Set("description", Napi::String::New(callbackEnv, "BabylonNative WGPU adapter"));
                    adapterInfo.Set("device", Napi::String::New(callbackEnv, "0"));
                    return adapterInfo;
                }, "GPUAdapter.requestAdapterInfo");
            }));

            adapter.Set("requestDevice", Napi::Function::New(env, [](const Napi::CallbackInfo& info) -> Napi::Value {
                auto deferred = Napi::Promise::Deferred::New(info.Env());
                deferred.Resolve(CreateGpuDevice(info.Env()));
                return deferred.Promise();
            }));

            return adapter;
        }

        Napi::Object CreateConstantsObject(Napi::Env env, std::initializer_list<std::pair<const char*, uint32_t>> values)
        {
            auto object = Napi::Object::New(env);
            for (const auto& [name, value] : values)
            {
                object.Set(name, Napi::Number::From(env, value));
            }
            return object;
        }

        void InstallWebGpuConstants(Napi::Env env)
        {
            auto global = env.Global();
            if (!global.Has("GPUBufferUsage"))
            {
                global.Set("GPUBufferUsage", CreateConstantsObject(env, {
                    {"MAP_READ", 0x0001},
                    {"MAP_WRITE", 0x0002},
                    {"COPY_SRC", 0x0004},
                    {"COPY_DST", 0x0008},
                    {"INDEX", 0x0010},
                    {"VERTEX", 0x0020},
                    {"UNIFORM", 0x0040},
                    {"STORAGE", 0x0080},
                    {"INDIRECT", 0x0100},
                    {"QUERY_RESOLVE", 0x0200},
                }));
            }

            if (!global.Has("GPUTextureUsage"))
            {
                global.Set("GPUTextureUsage", CreateConstantsObject(env, {
                    {"COPY_SRC", 0x01},
                    {"COPY_DST", 0x02},
                    {"TEXTURE_BINDING", 0x04},
                    {"STORAGE_BINDING", 0x08},
                    {"RENDER_ATTACHMENT", 0x10},
                }));
            }

            if (!global.Has("GPUShaderStage"))
            {
                global.Set("GPUShaderStage", CreateConstantsObject(env, {
                    {"VERTEX", 0x1},
                    {"FRAGMENT", 0x2},
                    {"COMPUTE", 0x4},
                }));
            }

            if (!global.Has("GPUColorWrite"))
            {
                global.Set("GPUColorWrite", CreateConstantsObject(env, {
                    {"RED", 0x1},
                    {"GREEN", 0x2},
                    {"BLUE", 0x4},
                    {"ALPHA", 0x8},
                    {"ALL", 0xf},
                }));
            }

            if (!global.Has("GPUMapMode"))
            {
                global.Set("GPUMapMode", CreateConstantsObject(env, {
                    {"READ", 0x1},
                    {"WRITE", 0x2},
                }));
            }
        }

        Napi::Value ImportCanvasTextureFromNative(const Napi::CallbackInfo& info)
        {
            if (info.Length() == 0)
            {
                return Napi::Boolean::New(info.Env(), false);
            }

            auto payload = ExtractCanvasTexturePayload(info[0]);
            if (!payload.has_value())
            {
                return Napi::Boolean::New(info.Env(), false);
            }

            return Napi::Boolean::New(info.Env(), ImportCanvasTexturePayload(*payload));
        }

        Napi::Object CreateGpu(Napi::Env env, bool developerFeaturesEnabled, bool unsafeWebGpuEnabled)
        {
            auto gpu = Napi::Object::New(env);

            gpu.Set("wgslLanguageFeatures", CreateSet(env));
            gpu.Set("requestAdapter", Napi::Function::New(env, [](const Napi::CallbackInfo& info) -> Napi::Value {
                auto deferred = Napi::Promise::Deferred::New(info.Env());
                deferred.Resolve(CreateGpuAdapter(info.Env()));
                return deferred.Promise();
            }));

            gpu.Set("getPreferredCanvasFormat", Napi::Function::New(env, [](const Napi::CallbackInfo& info) -> Napi::Value {
                auto options = info.Env().Global().Get("_playgroundOptions");
                if (options.IsObject() && options.As<Napi::Object>().Get("hdr10").ToBoolean())
                {
                    return Napi::String::New(info.Env(), "rgba16float");
                }
                return Napi::String::New(info.Env(), "bgra8unorm");
            }));

            // Non-standard helper for native-hosted canvases until HTMLCanvasElement
            // integration is implemented for WGPU mode.
            gpu.Set("_createCanvasContext", Napi::Function::New(env, [](const Napi::CallbackInfo& info) -> Napi::Value {
                return CreateGpuCanvasContext(info.Env());
            }));

            if (developerFeaturesEnabled)
            {
                // Non-standard helper for native validation to execute a WGSL compute shader
                // through the native Rust wgpu backend.
                gpu.Set("_dispatchCompute", Napi::Function::New(env, [](const Napi::CallbackInfo& info) -> Napi::Value {
                    if (info.Length() == 0 || !info[0].IsString())
                    {
                        return Napi::Boolean::New(info.Env(), false);
                    }

                    const auto shaderCode = info[0].As<Napi::String>().Utf8Value();
                    const auto entryPoint = info.Length() > 1 && info[1].IsString() ? info[1].As<Napi::String>().Utf8Value() : std::string{"main"};
                    const auto x = info.Length() > 2 ? ToUint32(info[2], 1) : 1;
                    const auto y = info.Length() > 3 ? ToUint32(info[3], 1) : 1;
                    const auto z = info.Length() > 4 ? ToUint32(info[4], 1) : 1;

                    const bool ok = babylon_wgpu_dispatch_compute_global(shaderCode.c_str(), entryPoint.c_str(), x, y, z);
                    return Napi::Boolean::New(info.Env(), ok);
                }));

                gpu.Set("_markWebGpuDrawRequested", Napi::Function::New(env, [](const Napi::CallbackInfo& info) -> Napi::Value {
                    (void)info;
                    babylon_wgpu_mark_webgpu_draw_requested();
                    return info.Env().Undefined();
                }));

                gpu.Set("_isDrawPathActive", Napi::Function::New(env, [](const Napi::CallbackInfo& info) -> Napi::Value {
                    (void)info;
                    return Napi::Boolean::New(info.Env(), babylon_wgpu_is_webgpu_draw_enabled());
                }));

                auto backendStats = Napi::Function::New(env, [](const Napi::CallbackInfo& info) -> Napi::Value {
                    auto env = info.Env();
                    auto stats = Napi::Object::New(env);
                    stats.Set("renderPipelineCreateCount", Napi::Number::From(env, static_cast<double>(g_renderPipelineCreateCount.load(std::memory_order_relaxed))));
                    stats.Set("commandEncoderCreateCount", Napi::Number::From(env, static_cast<double>(g_commandEncoderCreateCount.load(std::memory_order_relaxed))));
                    stats.Set("renderPassBeginCount", Napi::Number::From(env, static_cast<double>(g_renderPassBeginCount.load(std::memory_order_relaxed))));
                    stats.Set("queueSubmitCount", Napi::Number::From(env, static_cast<double>(g_queueSubmitCount.load(std::memory_order_relaxed))));
                    stats.Set("drawCallCount", Napi::Number::From(env, static_cast<double>(g_drawCallCount.load(std::memory_order_relaxed))));
                    stats.Set("textureCreateCount", Napi::Number::From(env, static_cast<double>(g_textureCreateCount.load(std::memory_order_relaxed))));
                    stats.Set("textureViewCreateCount", Napi::Number::From(env, static_cast<double>(g_textureViewCreateCount.load(std::memory_order_relaxed))));
                    stats.Set("bindGroupCreateCount", Napi::Number::From(env, static_cast<double>(g_bindGroupCreateCount.load(std::memory_order_relaxed))));
                    stats.Set("bufferCreateCount", Napi::Number::From(env, static_cast<double>(g_bufferCreateCount.load(std::memory_order_relaxed))));
                    stats.Set("bufferRequestedBytes", Napi::Number::From(env, static_cast<double>(g_bufferRequestedBytes.load(std::memory_order_relaxed))));
                    stats.Set("drawPathActive", Napi::Boolean::New(env, babylon_wgpu_is_webgpu_draw_enabled()));
                    stats.Set("nativeRenderFrameCount", Napi::Number::From(env, static_cast<double>(babylon_wgpu_get_render_frame_count())));
                    stats.Set("canvasTextureHash", Napi::Number::From(env, static_cast<double>(babylon_wgpu_get_canvas_texture_hash())));
                    stats.Set("canvasTextureWidth", Napi::Number::From(env, static_cast<double>(babylon_wgpu_get_canvas_texture_width())));
                    stats.Set("canvasTextureHeight", Napi::Number::From(env, static_cast<double>(babylon_wgpu_get_canvas_texture_height())));
                    // Legacy stat keys kept for compatibility with older scripts.
                    stats.Set("debugTextureHash", Napi::Number::From(env, static_cast<double>(babylon_wgpu_get_canvas_texture_hash())));
                    stats.Set("debugTextureWidth", Napi::Number::From(env, static_cast<double>(babylon_wgpu_get_canvas_texture_width())));
                    stats.Set("debugTextureHeight", Napi::Number::From(env, static_cast<double>(babylon_wgpu_get_canvas_texture_height())));
                    stats.Set("estimatedGpuMemoryBytes", Napi::Number::From(env, static_cast<double>(babylon_wgpu_get_estimated_gpu_memory_bytes())));
                    stats.Set("canvasTextureImportSkipCount", Napi::Number::From(env, static_cast<double>(babylon_wgpu_get_canvas_texture_import_skip_count())));
                    stats.Set("debugTextureImportSkipCount", Napi::Number::From(env, static_cast<double>(babylon_wgpu_get_canvas_texture_import_skip_count())));
                    stats.Set("externalImageUploadBorrowedCount", Napi::Number::From(env, static_cast<double>(babylon_wgpu_native_get_external_image_upload_borrowed_count())));
                    stats.Set("externalImageUploadBorrowedBytes", Napi::Number::From(env, static_cast<double>(babylon_wgpu_native_get_external_image_upload_borrowed_bytes())));
                    stats.Set("externalImageUploadOwnedCount", Napi::Number::From(env, static_cast<double>(babylon_wgpu_native_get_external_image_upload_owned_count())));
                    stats.Set("externalImageUploadOwnedBytes", Napi::Number::From(env, static_cast<double>(babylon_wgpu_native_get_external_image_upload_owned_bytes())));
                    stats.Set("backendMode", Napi::String::New(env, kBackendMode));
                    stats.Set("presentationPath", Napi::String::New(env, "webgpu-offscreen-present"));
                    stats.Set("placeholderRendererActive", Napi::Boolean::New(env, false));
                    stats.Set("developerFeaturesMode", Napi::String::New(env, kWebGpuDeveloperFeaturesMode));
                    stats.Set("lastError", Napi::String::New(env, GetLastWgpuError()));
                    return stats;
                });
                gpu.Set("_backendStats", backendStats);
                // Back-compat alias kept for existing tests/scripts.
                gpu.Set("_debugStats", backendStats);
            }

            if (unsafeWebGpuEnabled)
            {
                // Non-standard helper used to import a CanvasWgpu native interop
                // handle into the GraphicsWgpu-presented texture path.
                gpu.Set("_importCanvasTextureFromNative", Napi::Function::New(env, &ImportCanvasTextureFromNative));
                // Back-compat alias kept for existing Playground/test scripts.
                gpu.Set("_setDebugTextureFromNative", Napi::Function::New(env, &ImportCanvasTextureFromNative));
                gpu.Set("_unsafeMode", Napi::String::New(env, kUnsafeWebGpuMode));
            }

#ifdef BABYLON_NATIVE_WEBGPU_TEST_HOOKS
            // Non-standard helpers used only by native unit tests to validate
            // async std::future -> JS Promise semantics and rejection propagation.
            gpu.Set("_testResetDebugStats", Napi::Function::New(env, [](const Napi::CallbackInfo& info) -> Napi::Value {
                (void)info;
                g_sawWebGpuDrawCall.store(false, std::memory_order_release);
                g_renderPipelineCreateCount.store(0, std::memory_order_relaxed);
                g_commandEncoderCreateCount.store(0, std::memory_order_relaxed);
                g_renderPassBeginCount.store(0, std::memory_order_relaxed);
                g_queueSubmitCount.store(0, std::memory_order_relaxed);
                g_drawCallCount.store(0, std::memory_order_relaxed);
                g_textureCreateCount.store(0, std::memory_order_relaxed);
                g_textureViewCreateCount.store(0, std::memory_order_relaxed);
                g_bindGroupCreateCount.store(0, std::memory_order_relaxed);
                g_bufferCreateCount.store(0, std::memory_order_relaxed);
                g_bufferRequestedBytes.store(0, std::memory_order_relaxed);
                babylon_wgpu_native_reset_external_image_upload_stats();
                babylon_wgpu_reset_webgpu_draw_requested();
                return info.Env().Undefined();
            }));

            gpu.Set("_testAsyncResolve", Napi::Function::New(env, [](const Napi::CallbackInfo& info) -> Napi::Value {
                const auto value = info.Length() > 0 && info[0].IsString() ? info[0].As<Napi::String>().Utf8Value() : std::string{"ok"};
                auto future = std::async(std::launch::async, []() -> std::string {
                    return {};
                });

                return ResolvePromiseFromFuture(info.Env(), std::move(future), [value = std::move(value)](Napi::Env callbackEnv) {
                    return Napi::String::New(callbackEnv, value);
                }, "NativeWebGPU._testAsyncResolve");
            }));

            gpu.Set("_testAsyncReject", Napi::Function::New(env, [](const Napi::CallbackInfo& info) -> Napi::Value {
                const auto message = info.Length() > 0 && info[0].IsString() ? info[0].As<Napi::String>().Utf8Value() : std::string{"NativeWebGPU async rejection"};
                auto future = std::async(std::launch::async, [message]() -> std::string {
                    throw std::runtime_error{message};
                });

                return ResolvePromiseFromFuture(info.Env(), std::move(future), [](Napi::Env callbackEnv) {
                    return callbackEnv.Undefined();
                }, "NativeWebGPU._testAsyncReject");
            }));

            gpu.Set("_testAsyncResolveFactoryThrows", Napi::Function::New(env, [](const Napi::CallbackInfo& info) -> Napi::Value {
                const auto message = info.Length() > 0 && info[0].IsString() ? info[0].As<Napi::String>().Utf8Value() : std::string{"NativeWebGPU resolve factory rejection"};
                auto future = std::async(std::launch::async, []() -> std::string {
                    return {};
                });

                return ResolvePromiseFromFuture(info.Env(), std::move(future), [message](Napi::Env callbackEnv) -> Napi::Value {
                    (void)callbackEnv;
                    throw std::runtime_error{message};
                }, "NativeWebGPU._testAsyncResolveFactoryThrows");
            }));

            gpu.Set("_testThrowRustError", Napi::Function::New(env, [](const Napi::CallbackInfo& info) -> Napi::Value {
                static constexpr const char* kOperation = "NativeWebGPU._testThrowRustError";
                if (!babylon_wgpu_native_queue_copy_external_image_rgba_to_texture(
                        nullptr,
                        0,
                        0,
                        0,
                        0,
                        0,
                        0,
                        "{}",
                        "{}"))
                {
                    ThrowNativeWebGpuError(info.Env(), "NativeWebGPU._testThrowRustError observed a Rust backend failure", kOperation);
                    return info.Env().Undefined();
                }

                ThrowNativeOperationError(info.Env(), kOperation, "NativeWebGPU._testThrowRustError unexpectedly succeeded.");
                return info.Env().Undefined();
            }));

            gpu.Set("_testThrowCppException", Napi::Function::New(env, [](const Napi::CallbackInfo& info) -> Napi::Value {
                static constexpr const char* kOperation = "NativeWebGPU._testThrowCppException";
                try
                {
                    throw std::runtime_error{"NativeWebGPU._testThrowCppException simulated C++ exception with operation context"};
                }
                catch (const std::exception& exception)
                {
                    ThrowNativeOperationError(info.Env(), kOperation, exception.what());
                }

                return info.Env().Undefined();
            }));

            gpu.Set("_testReadTexturePixel", Napi::Function::New(env, [](const Napi::CallbackInfo& info) -> Napi::Value {
                static constexpr const char* kOperation = "NativeWebGPU._testReadTexturePixel";
                if (info.Length() == 0)
                {
                    ThrowNativeOperationError(info.Env(), kOperation, "NativeWebGPU._testReadTexturePixel requires a GPUTexture.");
                    return info.Env().Undefined();
                }

                const auto textureId = GetNativeHandleId(info[0], NativeResourceKind::Texture);
                if (textureId == 0)
                {
                    ThrowNativeOperationError(info.Env(), kOperation, "NativeWebGPU._testReadTexturePixel received an invalid or destroyed GPUTexture.");
                    return info.Env().Undefined();
                }

                const auto x = info.Length() > 1 ? ToUint32(info[1], 0) : 0;
                const auto y = info.Length() > 2 ? ToUint32(info[2], 0) : 0;
                std::array<uint8_t, 4> rgba{};
                if (!babylon_wgpu_native_test_read_texture_pixel(textureId, x, y, rgba.data(), rgba.size()))
                {
                    ThrowNativeWebGpuError(info.Env(), "NativeWebGPU failed to read GPUTexture pixel", kOperation);
                    return info.Env().Undefined();
                }

                auto result = Napi::Array::New(info.Env(), rgba.size());
                for (size_t i = 0; i < rgba.size(); ++i)
                {
                    result.Set(static_cast<uint32_t>(i), Napi::Number::From(info.Env(), rgba[i]));
                }
                return result;
            }));
#endif

            return gpu;
        }
    }

    Napi::Object CreateTextureFromNativeId(
        Napi::Env env,
        uint64_t nativeId,
        const char* label,
        const char* format,
        uint32_t width,
        uint32_t height,
        uint32_t depthOrArrayLayers,
        uint32_t usage)
    {
        TextureDescriptorData descriptor{};
        descriptor.Label = label != nullptr ? label : "";
        descriptor.Format = format != nullptr ? format : "bgra8unorm";
        descriptor.Width = std::max<uint32_t>(1, width);
        descriptor.Height = std::max<uint32_t>(1, height);
        descriptor.DepthOrArrayLayers = std::max<uint32_t>(1, depthOrArrayLayers);
        descriptor.Usage = usage;
        return CreateGpuTextureObject(env, descriptor, nativeId);
    }

    // Initialization contract: this function must be called from an
    // AppRuntime::Dispatch callback BEFORE any user JavaScript executes.
    // The AppRuntime WorkQueue is FIFO, and ScriptLoader dispatches through
    // the same queue, so navigator.gpu is guaranteed to be synchronously
    // available by the time any script runs. Embedders do NOT need to poll
    // for navigator.gpu or use a readiness promise — just call Initialize()
    // in the Dispatch callback and load scripts via ScriptLoader afterward.
    //
    // This matches the W3C WebGPU spec where navigator.gpu is a synchronous
    // [SameObject] attribute, always present when WebGPU is enabled.
    void Initialize(Napi::Env env)
    {
        Napi::HandleScope scope{env};

        auto global = env.Global();
        Napi::Object navigator;

        if (global.Has(JS_NAVIGATOR_NAME) && global.Get(JS_NAVIGATOR_NAME).IsObject())
        {
            navigator = global.Get(JS_NAVIGATOR_NAME).As<Napi::Object>();
        }
        else
        {
            navigator = Napi::Object::New(env);
            global.Set(JS_NAVIGATOR_NAME, navigator);
        }

        if (navigator.Has(JS_GPU_NAME))
        {
            auto existingGpu = navigator.Get(JS_GPU_NAME);
            if (existingGpu.IsObject())
            {
                // navigator.gpu already exists (e.g. re-initialization after
                // Android surface recreation). Per W3C spec, navigator.gpu is
                // a [SameObject] attribute — nothing else to do.
                return;
            }
        }

        const bool developerFeaturesEnabled = IsWebGpuDeveloperFeaturesEnabled(env);
        const bool unsafeWebGpuEnabled = developerFeaturesEnabled || IsUnsafeWebGpuEnabled(env);
        InstallWebGpuConstants(env);
        auto gpu = CreateGpu(env, developerFeaturesEnabled, unsafeWebGpuEnabled);
        navigator.Set(JS_GPU_NAME, gpu);
    }
}
