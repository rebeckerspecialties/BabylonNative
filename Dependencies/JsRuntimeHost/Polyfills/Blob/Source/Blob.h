#pragma once

#include <napi/napi.h>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace Babylon::Polyfills::Internal
{
    class Blob : public Napi::ObjectWrap<Blob>
    {
    public:
        static void Initialize(Napi::Env env);

        explicit Blob(const Napi::CallbackInfo& info);

        // Synchronous accessors for other polyfills in this repo (URL.createObjectURL). A Blob is
        // immutable, so the flattened copy stays valid however long the caller keeps it.
        std::shared_ptr<const std::vector<std::byte>> CopyBytes() const;
        const std::string& Type() const { return m_type; }

    private:
        Napi::Value GetSize(const Napi::CallbackInfo& info);
        Napi::Value GetType(const Napi::CallbackInfo& info);
        Napi::Value Text(const Napi::CallbackInfo& info);
        Napi::Value ArrayBuffer(const Napi::CallbackInfo& info);
        Napi::Value Bytes(const Napi::CallbackInfo& info);
        Napi::Value Slice(const Napi::CallbackInfo& info);
        Napi::Value Stream(const Napi::CallbackInfo& info);

        struct Segment;
        struct Data;
        struct StreamState;

        bool AppendBlobPart(Data& data, const Napi::Value& blobPart);
        Napi::ArrayBuffer CreateArrayBuffer() const;

        static std::string NormalizeType(std::string type);
        static std::string NormalizeLineEndings(std::string value);
        static size_t NormalizeSliceIndex(double value, size_t size);

        std::shared_ptr<const Data> m_data;
        std::string m_type;
    };
}
