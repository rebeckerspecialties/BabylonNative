#pragma once

#include <webgpu/webgpu.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace Babylon::Plugins::NativeDawn::CommandStream
{
    // Version of the render-pass command-stream encoding this decoder understands. It must
    // match WebGPURenderPassCommandStreamProtocolVersion in Babylon.js
    // (webgpuRenderPassCommandStream.ts); on a mismatch the engine keeps using direct
    // encoder calls.
    constexpr uint32_t ProtocolVersion = 1;

    enum class ResourceKind : uint8_t
    {
        RenderPipeline,
        Buffer,
        BindGroup,
    };

    // Maps the numeric ids the engine encodes into command words back to WebGPU handles.
    // Ids are never reused, and an entry lives exactly as long as the JS wrapper that owns
    // the handle, so a stale id can never resolve to another object.
    class ResourceRegistry
    {
    public:
        uint64_t Register(ResourceKind kind, void* handle);
        void Unregister(uint64_t id);
        // Returns nullptr when the id is unknown or was registered under another kind.
        void* Find(uint64_t id, ResourceKind kind) const;
        size_t Size() const;

    private:
        struct Entry
        {
            ResourceKind Kind;
            void* Handle;
        };

        std::unordered_map<uint64_t, Entry> m_entries{};
        uint64_t m_nextId{1};
    };

    // Decodes a batch encoded by WebGPURenderPassCommandStream and records it into `pass`.
    //
    // Recording is all-or-nothing, as the engine contract requires: the whole batch is
    // validated (command layout, bounds, resource ids and kinds) before the first encoder
    // call, so a false return means nothing was recorded and the pass is untouched. `error`
    // then describes the rejection for diagnostics.
    bool RecordRenderPassCommands(WGPURenderPassEncoder pass, const ResourceRegistry& registry, const uint32_t* words, size_t wordCount, std::string& error);
}
