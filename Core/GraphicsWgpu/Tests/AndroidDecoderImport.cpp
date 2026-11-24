// Standalone NDK device probe. Use the packaged WebM, not a synthetic RGBA allocation.
#define VK_USE_PLATFORM_ANDROID_KHR
#include <vulkan/vulkan.h>
#include <android/hardware_buffer.h>
#include <media/NdkImageReader.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaExtractor.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <vector>

namespace
{
    void Check(int status, const char* operation)
    {
        if (status != 0)
        {
            std::fprintf(stderr, "%s: %d\n", operation, status);
            std::exit(1);
        }
    }

    // No GPU work is submitted: the decoder retains ownership throughout this import check.
    void CheckImport(VkDevice device, AHardwareBuffer* buffer)
    {
        AHardwareBuffer_Desc desc{};
        AHardwareBuffer_describe(buffer, &desc);
        VkAndroidHardwareBufferFormatPropertiesANDROID format{VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID};
        VkAndroidHardwareBufferPropertiesANDROID props{VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID, &format};
        auto getProps = reinterpret_cast<PFN_vkGetAndroidHardwareBufferPropertiesANDROID>(vkGetDeviceProcAddr(device, "vkGetAndroidHardwareBufferPropertiesANDROID"));
        if (!getProps) Check(-1, "Missing vkGetAndroidHardwareBufferPropertiesANDROID");
        Check(getProps(device, buffer, &props), "query decoder buffer");
        std::printf("decoder %ux%u usage=%llx format=%u external=%llu memory=%llu memoryTypes=%x formatFeatures=%x\n", desc.width, desc.height,
            static_cast<unsigned long long>(desc.usage), format.format, static_cast<unsigned long long>(format.externalFormat),
            static_cast<unsigned long long>(props.allocationSize), props.memoryTypeBits, format.formatFeatures);
        if (!props.memoryTypeBits) Check(-1, "No importable Vulkan memory type");
        if (!(format.formatFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))
            std::fprintf(stderr, "Missing mandatory SAMPLED_IMAGE feature; checking binding only, no GPU sampling\n");
        VkExternalFormatANDROID external{VK_STRUCTURE_TYPE_EXTERNAL_FORMAT_ANDROID, nullptr, format.externalFormat};
        const auto* formatChain = format.format == VK_FORMAT_UNDEFINED ? &external : nullptr;
        VkExternalMemoryImageCreateInfo handle{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO, formatChain,
            VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID};
        VkImageCreateInfo create{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, &handle};
        create.imageType = VK_IMAGE_TYPE_2D;
        create.format = format.format;
        create.extent = {desc.width, desc.height, 1};
        create.mipLevels = 1;
        create.arrayLayers = desc.layers;
        create.samples = VK_SAMPLE_COUNT_1_BIT;
        create.tiling = VK_IMAGE_TILING_OPTIMAL;
        create.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
        VkImage image{};
        Check(vkCreateImage(device, &create, nullptr, &image), "create external image");
        VkMemoryDedicatedAllocateInfo dedicated{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO, nullptr, image, VK_NULL_HANDLE};
        VkImportAndroidHardwareBufferInfoANDROID import{VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID, &dedicated, buffer};
        VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, &import, props.allocationSize,
            static_cast<uint32_t>(__builtin_ctz(props.memoryTypeBits))};
        VkDeviceMemory memory{};
        Check(vkAllocateMemory(device, &alloc, nullptr, &memory), "import decoder memory");
        Check(vkBindImageMemory(device, image, memory, 0), "bind decoder image");
        VkSamplerYcbcrConversionCreateInfo conv{VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_CREATE_INFO, formatChain};
        conv.format = format.format;
        conv.ycbcrModel = format.suggestedYcbcrModel;
        conv.ycbcrRange = format.suggestedYcbcrRange;
        conv.components = format.samplerYcbcrConversionComponents;
        conv.xChromaOffset = format.suggestedXChromaOffset;
        conv.yChromaOffset = format.suggestedYChromaOffset;
        conv.chromaFilter = VK_FILTER_NEAREST;
        VkSamplerYcbcrConversion conversion{};
        Check(vkCreateSamplerYcbcrConversion(device, &conv, nullptr, &conversion), "create decoder conversion");
        VkSamplerYcbcrConversionInfo conversionInfo{VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO, nullptr, conversion};
        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, &conversionInfo};
        viewInfo.image = image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format.format;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkImageView view{};
        Check(vkCreateImageView(device, &viewInfo, nullptr, &view), "create converted view");
        vkDestroyImageView(device, view, nullptr);
        vkDestroySamplerYcbcrConversion(device, conversion, nullptr);
        vkDestroyImage(device, image, nullptr);
        vkFreeMemory(device, memory, nullptr);
    }
}

int main(int argc, char** argv)
{
    if (argc != 2 && (argc != 3 || std::strcmp(argv[2], "yuv420") != 0)) return 2;
    const auto readerFormat = argc == 3 ? AIMAGE_FORMAT_YUV_420_888 : AIMAGE_FORMAT_PRIVATE;
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instanceInfo.pApplicationInfo = &app;
    VkInstance instance{};
    Check(vkCreateInstance(&instanceInfo, nullptr, &instance), "create instance");
    VkPhysicalDevice physical{};
    uint32_t count = 1;
    Check(vkEnumeratePhysicalDevices(instance, &count, &physical), "enumerate device");
    if (!count) return 1;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, families.data());
    uint32_t family = 0;
    while (family < count && !(families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT)) ++family;
    if (family == count) return 1;
    const float priority = 1.f;
    VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, nullptr, 0, family, 1, &priority};
    const char* extensions[]{VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME, VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME};
    VkPhysicalDeviceSamplerYcbcrConversionFeatures feature{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES, nullptr, VK_TRUE};
    VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &feature};
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &q;
    deviceInfo.enabledExtensionCount = 2;
    deviceInfo.ppEnabledExtensionNames = extensions;
    VkDevice device{};
    Check(vkCreateDevice(physical, &deviceInfo, nullptr, &device), "create device");

    int fd = open(argv[1], O_RDONLY);
    if (fd < 0) return 1;
    auto* ex = AMediaExtractor_new();
    Check(AMediaExtractor_setDataSourceFd(ex, fd, 0, lseek(fd, 0, SEEK_END)), "open movie");
    close(fd);
    AMediaCodec* codec{};
    AImageReader* reader{};
    for (size_t t = 0; t < AMediaExtractor_getTrackCount(ex); ++t)
    {
        auto* fmt = AMediaExtractor_getTrackFormat(ex, t);
        const char* mime{};
        if (AMediaFormat_getString(fmt, AMEDIAFORMAT_KEY_MIME, &mime) && !std::strncmp(mime, "video/", 6))
        {
            Check(AMediaExtractor_selectTrack(ex, t), "select video");
            int32_t w{}, h{};
            AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_WIDTH, &w);
            AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_HEIGHT, &h);
            Check(AImageReader_newWithUsage(w, h, readerFormat, AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE, 3, &reader), "create image reader");
            ANativeWindow* window{};
            Check(AImageReader_getWindow(reader, &window), "get decoder surface");
            codec = AMediaCodec_createDecoderByType(mime);
            if (!codec) return 1;
            Check(AMediaCodec_configure(codec, fmt, window, nullptr, 0), "configure decoder");
            AMediaFormat_delete(fmt);
            break;
        }
        AMediaFormat_delete(fmt);
    }
    if (!codec) return 1;
    Check(AMediaCodec_start(codec), "start decoder");
    int imported = 0;
    bool eos = false;
    for (int attempt = 0; attempt < 500 && imported < 4; ++attempt)
    {
        const auto input = eos ? -1 : AMediaCodec_dequeueInputBuffer(codec, 10000);
        if (input >= 0)
        {
            size_t size{};
            auto* data = AMediaCodec_getInputBuffer(codec, input, &size);
            const auto n = AMediaExtractor_readSampleData(ex, data, size);
            eos = n < 0;
            Check(AMediaCodec_queueInputBuffer(codec, input, 0, eos ? 0 : n, eos ? 0 : AMediaExtractor_getSampleTime(ex),
                eos ? AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM : 0), "queue compressed frame");
            if (!eos) AMediaExtractor_advance(ex);
        }
        AMediaCodecBufferInfo info{};
        const auto out = AMediaCodec_dequeueOutputBuffer(codec, &info, 10000);
        if (out >= 0) Check(AMediaCodec_releaseOutputBuffer(codec, out, info.size != 0), "render decoder frame");
        AImage* acquired{};
        const auto status = AImageReader_acquireNextImage(reader, &acquired);
        if (status == AMEDIA_IMGREADER_NO_BUFFER_AVAILABLE) continue;
        Check(status, "acquire decoder frame");
        AHardwareBuffer* buffer{};
        Check(AImage_getHardwareBuffer(acquired, &buffer), "get decoder hardware buffer");
        CheckImport(device, buffer);
        AImage_delete(acquired);
        ++imported;
    }
    Check(AMediaCodec_stop(codec), "stop decoder");
    AMediaCodec_delete(codec);
    AMediaExtractor_delete(ex);
    AImageReader_delete(reader);
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
    std::printf("decoder images imported and bound with converted views: %d\n", imported);
    return imported == 4 ? 0 : 1;
}
