#include <Babylon/Polyfills/TextDecoder.h>

#include <napi/napi.h>
#include <cstring>
#include <string>
#include <vector>

namespace
{
    enum class Encoding
    {
        Utf8,
        Utf16Le,
        Utf16Be,
    };

    // Normalize an encoding label per the WHATWG Encoding Standard "get an encoding"
    // algorithm: strip leading/trailing ASCII whitespace and ASCII-lowercase the result.
    std::string NormalizeEncodingLabel(const std::string& encoding)
    {
        const auto isAsciiWhitespace = [](char c) {
            return c == '\t' || c == '\n' || c == '\f' || c == '\r' || c == ' ';
        };

        size_t begin = 0;
        size_t end = encoding.size();
        while (begin < end && isAsciiWhitespace(encoding[begin]))
        {
            ++begin;
        }
        while (end > begin && isAsciiWhitespace(encoding[end - 1]))
        {
            --end;
        }

        std::string label = encoding.substr(begin, end - begin);
        for (auto& c : label)
        {
            if (c >= 'A' && c <= 'Z')
            {
                c = static_cast<char>(c - 'A' + 'a');
            }
        }
        return label;
    }

    Encoding ParseEncoding(const std::string& encoding, Napi::Env env)
    {
        const auto label = NormalizeEncodingLabel(encoding);
        if (label == "utf-8" || label == "utf8" || label == "unicode-1-1-utf-8" ||
            label == "unicode11utf8" || label == "unicode20utf8" || label == "x-unicode20utf8")
        {
            return Encoding::Utf8;
        }
        if (label == "csunicode" || label == "iso-10646-ucs-2" || label == "ucs-2" ||
            label == "unicode" || label == "unicodefeff" || label == "utf-16" || label == "utf-16le")
        {
            return Encoding::Utf16Le;
        }
        if (label == "unicodefffe" || label == "utf-16be")
        {
            return Encoding::Utf16Be;
        }

        throw Napi::RangeError::New(env, "TextDecoder: unsupported encoding '" + encoding + "'");
    }

    class TextDecoder final : public Napi::ObjectWrap<TextDecoder>
    {
    public:
        static void Initialize(Napi::Env env)
        {
            Napi::HandleScope scope{env};

            static constexpr auto JS_TEXTDECODER_CONSTRUCTOR_NAME = "TextDecoder";
            if (env.Global().Get(JS_TEXTDECODER_CONSTRUCTOR_NAME).IsUndefined())
            {
                Napi::Function func = DefineClass(
                    env,
                    JS_TEXTDECODER_CONSTRUCTOR_NAME,
                    {
                        InstanceAccessor("encoding", &TextDecoder::EncodingName, nullptr),
                        InstanceMethod("decode", &TextDecoder::Decode),
                    });

                env.Global().Set(JS_TEXTDECODER_CONSTRUCTOR_NAME, func);
            }
        }

        explicit TextDecoder(const Napi::CallbackInfo& info)
            : Napi::ObjectWrap<TextDecoder>{info}
        {
            if (info.Length() > 0 && info[0].IsString())
            {
                m_encoding = ParseEncoding(info[0].As<Napi::String>().Utf8Value(), Env());
            }
        }

    private:
        Napi::Value EncodingName(const Napi::CallbackInfo& info)
        {
            switch (m_encoding)
            {
                case Encoding::Utf16Le:
                    return Napi::String::New(info.Env(), "utf-16le");
                case Encoding::Utf16Be:
                    return Napi::String::New(info.Env(), "utf-16be");
                default:
                    return Napi::String::New(info.Env(), "utf-8");
            }
        }

        Napi::Value Decode(const Napi::CallbackInfo& info)
        {
            if (info.Length() < 1 || info[0].IsUndefined())
            {
                return Napi::String::New(info.Env(), "");
            }

            std::vector<uint8_t> data;

            if (info[0].IsTypedArray())
            {
                auto typedArray = info[0].As<Napi::TypedArray>();
                auto arrayBuffer = typedArray.ArrayBuffer();
                auto byteOffset = typedArray.ByteOffset();
                auto byteLength = typedArray.ByteLength();
                data.resize(byteLength);
                if (byteLength > 0)
                {
                    std::memcpy(data.data(), static_cast<uint8_t*>(arrayBuffer.Data()) + byteOffset, byteLength);
                }
            }
            else if (info[0].IsArrayBuffer())
            {
                auto arrayBuffer = info[0].As<Napi::ArrayBuffer>();
                auto byteLength = arrayBuffer.ByteLength();
                data.resize(byteLength);
                if (byteLength > 0)
                {
                    std::memcpy(data.data(), arrayBuffer.Data(), byteLength);
                }
            }
            else
            {
                throw Napi::TypeError::New(Env(), "TextDecoder.decode: input must be a BufferSource (ArrayBuffer or TypedArray)");
            }

            if (m_encoding == Encoding::Utf8)
            {
                const size_t offset = data.size() >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF ? 3 : 0;
                const char* bytes = data.empty() ? "" : reinterpret_cast<const char*>(data.data() + offset);
                return Napi::String::New(info.Env(), bytes, data.size() - offset);
            }

            const auto readCodeUnit = [this, &data](size_t offset) {
                if (m_encoding == Encoding::Utf16Be)
                {
                    return static_cast<char16_t>((static_cast<uint16_t>(data[offset]) << 8) | data[offset + 1]);
                }
                return static_cast<char16_t>(data[offset] | (static_cast<uint16_t>(data[offset + 1]) << 8));
            };

            std::u16string decoded;
            decoded.reserve((data.size() + 1) / 2);
            for (size_t index = 0; index < data.size();)
            {
                if (index + 1 >= data.size())
                {
                    decoded.push_back(static_cast<char16_t>(0xFFFD));
                    break;
                }

                const auto codeUnit = readCodeUnit(index);
                index += 2;
                if (decoded.empty() && codeUnit == static_cast<char16_t>(0xFEFF))
                {
                    continue;
                }
                if (codeUnit >= static_cast<char16_t>(0xD800) && codeUnit <= static_cast<char16_t>(0xDBFF))
                {
                    if (index + 1 < data.size())
                    {
                        const auto trailing = readCodeUnit(index);
                        if (trailing >= static_cast<char16_t>(0xDC00) && trailing <= static_cast<char16_t>(0xDFFF))
                        {
                            decoded.push_back(codeUnit);
                            decoded.push_back(trailing);
                            index += 2;
                            continue;
                        }
                    }
                    decoded.push_back(static_cast<char16_t>(0xFFFD));
                    if (index < data.size() && index + 1 >= data.size())
                    {
                        // WHATWG end-of-queue: an unpaired lead surrogate and a leftover odd byte
                        // are a single error, not one each.
                        break;
                    }
                    continue;
                }
                if (codeUnit >= static_cast<char16_t>(0xDC00) && codeUnit <= static_cast<char16_t>(0xDFFF))
                {
                    decoded.push_back(static_cast<char16_t>(0xFFFD));
                    continue;
                }
                decoded.push_back(codeUnit);
            }

            return Napi::String::New(info.Env(), decoded);
        }

        Encoding m_encoding{Encoding::Utf8};
    };
}

namespace Babylon::Polyfills::TextDecoder
{
    void BABYLON_API Initialize(Napi::Env env)
    {
        ::TextDecoder::Initialize(env);
    }
}
