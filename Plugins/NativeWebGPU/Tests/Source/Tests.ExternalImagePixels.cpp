#include <Babylon/Graphics/ExternalImagePixels.h>
#include <gtest/gtest.h>

namespace
{
    class Image final : public Babylon::Graphics::ExternalImagePixels
    {
    public:
        bool ReadCurrentPixels(const Reader& reader) override
        {
            reader(Bytes, 1, 1);
            return true;
        }
        uint8_t Bytes[4]{1, 2, 3, 255};
    };
}

TEST(ExternalImagePixels, RequiresLiveRegistrationInSameRealm)
{
    using Babylon::Graphics::ExternalImageRegistration;
    int env{}, other{}, wrapper{};
    auto source = std::make_shared<Image>();
    EXPECT_FALSE(ExternalImageRegistration::Find(&env, &wrapper));
    {
        ExternalImageRegistration registration{&env, &wrapper, source};
        EXPECT_FALSE(ExternalImageRegistration::Find(&other, &wrapper));
        auto found = ExternalImageRegistration::Find(&env, &wrapper);
        ASSERT_TRUE(found);
        EXPECT_TRUE(found->ReadCurrentPixels([&](auto bytes, auto width, auto height) {
            EXPECT_EQ(bytes.data(), source->Bytes);
            EXPECT_EQ(bytes.size(), 4u);
            EXPECT_EQ(width, 1u);
            EXPECT_EQ(height, 1u);
        }));
    }
    EXPECT_FALSE(ExternalImageRegistration::Find(&env, &wrapper));
    ExternalImageRegistration reused{&env, &wrapper, source};
    source.reset();
    EXPECT_FALSE(ExternalImageRegistration::Find(&env, &wrapper));
}
