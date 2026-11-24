#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <unordered_map>

namespace Babylon::Graphics
{
    class ExternalImagePixels
    {
    public:
        using Reader = std::function<void(std::span<const uint8_t>, uint32_t, uint32_t)>;
        virtual ~ExternalImagePixels() = default;
        // The span is valid only inside reader. No JS may run while it is borrowed.
        virtual bool ReadCurrentPixels(const Reader& reader) = 0;
    };

    // Only a live host wrapper registered by its producer can lend pixels.
    // Registration never roots either the JS wrapper or the producer.
    class ExternalImageRegistration
    {
        struct Entry { const void* Env; std::weak_ptr<ExternalImagePixels> Source; };
        static inline std::mutex s_mutex;
        static inline std::unordered_map<const void*, Entry> s_sources;
        const void* m_wrapper;

    public:
        ExternalImageRegistration(const void* env, const void* wrapper, const std::shared_ptr<ExternalImagePixels>& source)
            : m_wrapper{wrapper}
        {
            std::lock_guard lock{s_mutex};
            s_sources.emplace(wrapper, Entry{env, source});
        }
        ExternalImageRegistration(const ExternalImageRegistration&) = delete;
        ExternalImageRegistration& operator=(const ExternalImageRegistration&) = delete;
        ~ExternalImageRegistration()
        {
            std::lock_guard lock{s_mutex};
            s_sources.erase(m_wrapper);
        }
        static std::shared_ptr<ExternalImagePixels> Find(const void* env, const void* wrapper)
        {
            std::lock_guard lock{s_mutex};
            auto it = s_sources.find(wrapper);
            return it != s_sources.end() && it->second.Env == env ? it->second.Source.lock() : nullptr;
        }
    };
}
