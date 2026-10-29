#include "RenderPassCommandStream.h"

#include <vector>

namespace Babylon::Plugins::NativeDawn::CommandStream
{
    namespace
    {
        // Opcodes and layouts of WebGPURenderPassCommandOpcode (webgpuRenderPassCommandStream.ts).
        // 64-bit values are two words, low word first.
        enum Opcode : uint32_t
        {
            SetPipeline = 1,         // id:u64
            SetBindGroup = 2,        // slot:u32 id:u64 dynamicOffsetCount:u32 offsets:u32[count]
            SetVertexBuffer = 3,     // slot:u32 id:u64 offset:u64 size:u64
            SetIndexBuffer = 4,      // id:u64 format:u32 offset:u64 size:u64
            Draw = 9,                // vertexCount instanceCount firstVertex firstInstance
            DrawIndexed = 10,        // indexCount instanceCount firstIndex baseVertex firstInstance
            DrawIndirect = 11,       // id:u64 offset:u64
            DrawIndexedIndirect = 12 // id:u64 offset:u64
        };

        constexpr uint64_t WholeSizeMarker = UINT64_MAX;

        struct Cursor
        {
            const uint32_t* Words;
            size_t Count;
            size_t Index{0};

            bool Has(size_t words) const
            {
                return Index + words <= Count;
            }

            uint32_t U32()
            {
                return Words[Index++];
            }

            uint64_t U64()
            {
                const uint64_t low = Words[Index++];
                const uint64_t high = Words[Index++];
                return low | (high << 32);
            }
        };

        std::string Describe(const char* what, size_t commandStart)
        {
            return std::string{"render pass command stream: "} + what + " at word " + std::to_string(commandStart);
        }

        // Validates the whole batch and resolves every resource id, in command order, before
        // anything touches the encoder.
        bool Validate(const ResourceRegistry& registry, const uint32_t* words, size_t wordCount, std::vector<void*>& handles, std::string& error)
        {
            Cursor cursor{words, wordCount};
            const auto resolve = [&](ResourceKind kind, const char* what, size_t commandStart) {
                if (!cursor.Has(2))
                {
                    error = Describe("truncated resource id", commandStart);
                    return false;
                }
                const uint64_t id = cursor.U64();
                void* handle = registry.Find(id, kind);
                if (handle == nullptr)
                {
                    error = Describe(what, commandStart) + " (id " + std::to_string(id) + ")";
                    return false;
                }
                handles.push_back(handle);
                return true;
            };

            while (cursor.Index < cursor.Count)
            {
                const size_t commandStart = cursor.Index;
                const uint32_t opcode = cursor.U32();
                switch (opcode)
                {
                    case SetPipeline:
                        if (!resolve(ResourceKind::RenderPipeline, "unknown GPURenderPipeline", commandStart))
                        {
                            return false;
                        }
                        break;
                    case SetBindGroup:
                    {
                        if (!cursor.Has(1))
                        {
                            error = Describe("truncated setBindGroup", commandStart);
                            return false;
                        }
                        cursor.U32(); // slot
                        if (!resolve(ResourceKind::BindGroup, "unknown GPUBindGroup", commandStart))
                        {
                            return false;
                        }
                        if (!cursor.Has(1))
                        {
                            error = Describe("truncated setBindGroup", commandStart);
                            return false;
                        }
                        const uint32_t dynamicOffsetCount = cursor.U32();
                        if (!cursor.Has(dynamicOffsetCount))
                        {
                            error = Describe("dynamic offsets run past the end of the stream", commandStart);
                            return false;
                        }
                        cursor.Index += dynamicOffsetCount;
                        break;
                    }
                    case SetVertexBuffer:
                        if (!cursor.Has(1))
                        {
                            error = Describe("truncated setVertexBuffer", commandStart);
                            return false;
                        }
                        cursor.U32(); // slot
                        if (!resolve(ResourceKind::Buffer, "unknown vertex GPUBuffer", commandStart))
                        {
                            return false;
                        }
                        if (!cursor.Has(4))
                        {
                            error = Describe("truncated setVertexBuffer", commandStart);
                            return false;
                        }
                        cursor.Index += 4; // offset, size
                        break;
                    case SetIndexBuffer:
                        if (!resolve(ResourceKind::Buffer, "unknown index GPUBuffer", commandStart))
                        {
                            return false;
                        }
                        if (!cursor.Has(5))
                        {
                            error = Describe("truncated setIndexBuffer", commandStart);
                            return false;
                        }
                        cursor.Index += 5; // format, offset, size
                        break;
                    case Draw:
                        if (!cursor.Has(4))
                        {
                            error = Describe("truncated draw", commandStart);
                            return false;
                        }
                        cursor.Index += 4;
                        break;
                    case DrawIndexed:
                        if (!cursor.Has(5))
                        {
                            error = Describe("truncated drawIndexed", commandStart);
                            return false;
                        }
                        cursor.Index += 5;
                        break;
                    case DrawIndirect:
                    case DrawIndexedIndirect:
                        if (!resolve(ResourceKind::Buffer, "unknown indirect GPUBuffer", commandStart))
                        {
                            return false;
                        }
                        if (!cursor.Has(2))
                        {
                            error = Describe("truncated indirect draw", commandStart);
                            return false;
                        }
                        cursor.Index += 2; // offset
                        break;
                    default:
                        error = Describe("unsupported opcode", commandStart) + " (" + std::to_string(opcode) + ")";
                        return false;
                }
            }
            return true;
        }

        void Apply(WGPURenderPassEncoder pass, const uint32_t* words, size_t wordCount, const std::vector<void*>& handles)
        {
            Cursor cursor{words, wordCount};
            size_t handleIndex = 0;
            while (cursor.Index < cursor.Count)
            {
                switch (cursor.U32())
                {
                    case SetPipeline:
                        cursor.U64();
                        wgpuRenderPassEncoderSetPipeline(pass, static_cast<WGPURenderPipeline>(handles[handleIndex++]));
                        break;
                    case SetBindGroup:
                    {
                        const uint32_t slot = cursor.U32();
                        cursor.U64();
                        const uint32_t dynamicOffsetCount = cursor.U32();
                        const uint32_t* dynamicOffsets = dynamicOffsetCount == 0 ? nullptr : cursor.Words + cursor.Index;
                        cursor.Index += dynamicOffsetCount;
                        wgpuRenderPassEncoderSetBindGroup(pass, slot, static_cast<WGPUBindGroup>(handles[handleIndex++]), dynamicOffsetCount, dynamicOffsets);
                        break;
                    }
                    case SetVertexBuffer:
                    {
                        const uint32_t slot = cursor.U32();
                        cursor.U64();
                        const uint64_t offset = cursor.U64();
                        const uint64_t size = cursor.U64();
                        wgpuRenderPassEncoderSetVertexBuffer(pass, slot, static_cast<WGPUBuffer>(handles[handleIndex++]), offset, size == WholeSizeMarker ? WGPU_WHOLE_SIZE : size);
                        break;
                    }
                    case SetIndexBuffer:
                    {
                        cursor.U64();
                        const WGPUIndexFormat format = cursor.U32() == 1 ? WGPUIndexFormat_Uint32 : WGPUIndexFormat_Uint16;
                        const uint64_t offset = cursor.U64();
                        const uint64_t size = cursor.U64();
                        wgpuRenderPassEncoderSetIndexBuffer(pass, static_cast<WGPUBuffer>(handles[handleIndex++]), format, offset, size == WholeSizeMarker ? WGPU_WHOLE_SIZE : size);
                        break;
                    }
                    case Draw:
                    {
                        const uint32_t vertexCount = cursor.U32();
                        const uint32_t instanceCount = cursor.U32();
                        const uint32_t firstVertex = cursor.U32();
                        const uint32_t firstInstance = cursor.U32();
                        wgpuRenderPassEncoderDraw(pass, vertexCount, instanceCount, firstVertex, firstInstance);
                        break;
                    }
                    case DrawIndexed:
                    {
                        const uint32_t indexCount = cursor.U32();
                        const uint32_t instanceCount = cursor.U32();
                        const uint32_t firstIndex = cursor.U32();
                        const int32_t baseVertex = static_cast<int32_t>(cursor.U32());
                        const uint32_t firstInstance = cursor.U32();
                        wgpuRenderPassEncoderDrawIndexed(pass, indexCount, instanceCount, firstIndex, baseVertex, firstInstance);
                        break;
                    }
                    case DrawIndirect:
                    {
                        cursor.U64();
                        const uint64_t offset = cursor.U64();
                        wgpuRenderPassEncoderDrawIndirect(pass, static_cast<WGPUBuffer>(handles[handleIndex++]), offset);
                        break;
                    }
                    case DrawIndexedIndirect:
                    {
                        cursor.U64();
                        const uint64_t offset = cursor.U64();
                        wgpuRenderPassEncoderDrawIndexedIndirect(pass, static_cast<WGPUBuffer>(handles[handleIndex++]), offset);
                        break;
                    }
                    default:
                        // Validate() accepted the stream, so every opcode is one of the above.
                        return;
                }
            }
        }
    }

    uint64_t ResourceRegistry::Register(ResourceKind kind, void* handle)
    {
        const uint64_t id = m_nextId++;
        m_entries.emplace(id, Entry{kind, handle});
        return id;
    }

    void ResourceRegistry::Unregister(uint64_t id)
    {
        m_entries.erase(id);
    }

    void* ResourceRegistry::Find(uint64_t id, ResourceKind kind) const
    {
        const auto it = m_entries.find(id);
        return it != m_entries.end() && it->second.Kind == kind ? it->second.Handle : nullptr;
    }

    size_t ResourceRegistry::Size() const
    {
        return m_entries.size();
    }

    bool RecordRenderPassCommands(WGPURenderPassEncoder pass, const ResourceRegistry& registry, const uint32_t* words, size_t wordCount, std::string& error)
    {
        if (pass == nullptr)
        {
            error = "render pass command stream: the render pass has ended";
            return false;
        }
        if (wordCount == 0)
        {
            return true;
        }
        if (words == nullptr)
        {
            error = "render pass command stream: null command words";
            return false;
        }

        std::vector<void*> handles{};
        if (!Validate(registry, words, wordCount, handles, error))
        {
            return false;
        }
        Apply(pass, words, wordCount, handles);
        return true;
    }
}
