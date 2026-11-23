// Headless Vulkan control: verify full-rate center, reduced-rate periphery and GPU timestamps.
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#define CK(x)                                        \
    do                                               \
    {                                                \
        VkResult result = (x);                       \
        if (result != VK_SUCCESS)                    \
        {                                            \
            fprintf(stderr, "%s: %d\n", #x, result); \
            exit(1);                                 \
        }                                            \
    }                                                \
    while (0)
enum
{
    W = 2048,
    H = 2263,
    OUTW = 1216,
    OUTH = 1344,
    N = 48
};
static VkDevice d;
static VkPhysicalDevice gpu;
static VkQueue queue;
static VkCommandBuffer cmd;
static VkPhysicalDeviceMemoryProperties mem;
static uint32_t mem_type(uint32_t mask, VkMemoryPropertyFlags flags)
{
    for (unsigned i = 0; i < mem.memoryTypeCount; i++)
        if ((mask & (1u << i)) && (mem.memoryTypes[i].propertyFlags & flags) == flags)
            return i;
    fprintf(stderr, "Missing compatible memory type\n");
    exit(1);
}
typedef struct
{
    VkImage handle;
    VkImageView view;
    VkDeviceMemory memory;
} Image;
static Image image(uint32_t w, uint32_t h, VkFormat fmt, VkImageUsageFlags use, VkImageCreateFlags flags)
{
    Image a = {0};
    VkImageCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D, .format = fmt, .extent = {w, h, 1}, .mipLevels = 1, .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL, .usage = use};
    ci.flags = flags;
    CK(vkCreateImage(d, &ci, 0, &a.handle));
    VkMemoryRequirements r;
    vkGetImageMemoryRequirements(d, a.handle, &r);
    VkMemoryAllocateInfo alloc = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = r.size, .memoryTypeIndex = mem_type(r.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)};
    CK(vkAllocateMemory(d, &alloc, 0, &a.memory));
    CK(vkBindImageMemory(d, a.handle, a.memory, 0));
    return a;
}
static void view(Image* im, VkFormat fmt)
{
    VkImageViewCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = im->handle, .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = fmt, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    CK(vkCreateImageView(d, &ci, 0, &im->view));
}
static void begin(void)
{
    CK(vkResetCommandBuffer(cmd, 0));
    VkCommandBufferBeginInfo bi = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    CK(vkBeginCommandBuffer(cmd, &bi));
}
static void submit(void)
{
    CK(vkEndCommandBuffer(cmd));
    VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd};
    VkFenceCreateInfo fi = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence f;
    CK(vkCreateFence(d, &fi, 0, &f));
    CK(vkQueueSubmit(queue, 1, &si, f));
    CK(vkWaitForFences(d, 1, &f, VK_TRUE, 10000000000ull));
    vkDestroyFence(d, f, 0);
}
static void barrier(VkImage im, VkImageLayout old, VkImageLayout dest, VkPipelineStageFlags src, VkPipelineStageFlags dst, VkAccessFlags a, VkAccessFlags b)
{
    VkImageMemoryBarrier br = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, .srcAccessMask = a, .dstAccessMask = b, .oldLayout = old, .newLayout = dest, .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = im, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    vkCmdPipelineBarrier(cmd, src, dst, 0, 0, 0, 0, 0, 1, &br);
}
static VkShaderModule load(const char* p)
{
    FILE* f = fopen(p, "rb");
    if (!f)
        exit(1);
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    uint32_t* data = malloc(n);
    if (fread(data, 1, n, f) != (size_t)n)
        exit(1);
    fclose(f);
    VkShaderModuleCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = n, .pCode = data};
    VkShaderModule s;
    CK(vkCreateShaderModule(d, &ci, 0, &s));
    free(data);
    return s;
}
static int cmp(const void* a, const void* b)
{
    double x = *(const double*)a, y = *(const double*)b;
    return (x > y) - (x < y);
}
static double transitions(const uint8_t* img, int x, int y)
{
    unsigned n = 0;
    for (int j = y; j < y + 128; j++)
        for (int i = x; i < x + 128; i++)
            n += img[4 * (j * OUTW + i)] != img[4 * (j * OUTW + i + 1)];
    return n / 16384.;
}
static long mhz(void)
{
    FILE* f = fopen("/sys/class/kgsl/kgsl-3d0/gpuclk", "r");
    long hz = 0;
    if (f)
    {
        if (fscanf(f, "%ld", &hz) != 1)
            hz = 0;
        fclose(f);
    }
    return hz / 1000000;
}
int main(int argc, char** argv)
{
    if (argc != 4)
        return 2;
    VkApplicationInfo ai = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_1};
    VkInstanceCreateInfo ii = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &ai};
    VkInstance inst;
    CK(vkCreateInstance(&ii, 0, &inst));
    uint32_t ct = 1;
    CK(vkEnumeratePhysicalDevices(inst, &ct, &gpu));
    if (!ct)
        return 2;
    VkPhysicalDeviceFragmentDensityMapFeaturesEXT features = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_FEATURES_EXT};
    VkPhysicalDeviceFeatures2 f2 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &features};
    vkGetPhysicalDeviceFeatures2(gpu, &f2);
    if (!features.fragmentDensityMap)
        return 3;
    VkPhysicalDeviceFragmentDensityMapPropertiesEXT props = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_PROPERTIES_EXT};
    VkPhysicalDeviceProperties2 p2 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &props};
    vkGetPhysicalDeviceProperties2(gpu, &p2);
    const uint32_t dw = (W + props.minFragmentDensityTexelSize.width - 1) / props.minFragmentDensityTexelSize.width, dh = (H + props.minFragmentDensityTexelSize.height - 1) / props.minFragmentDensityTexelSize.height;
    uint32_t nq = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &nq, 0);
    VkQueueFamilyProperties* qp = calloc(nq, sizeof(*qp));
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &nq, qp);
    unsigned fam = 0;
    while (fam < nq && (!(qp[fam].queueFlags & VK_QUEUE_GRAPHICS_BIT) || !qp[fam].timestampValidBits))
        fam++;
    if (fam == nq)
        return 3;
    const uint64_t mask = UINT64_MAX >> (64 - qp[fam].timestampValidBits);
    free(qp);
    float priority = 1;
    VkDeviceQueueCreateInfo qi = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = fam, .queueCount = 1, .pQueuePriorities = &priority};
    const char* ext = VK_EXT_FRAGMENT_DENSITY_MAP_EXTENSION_NAME;
    features.fragmentDensityMapDynamic = 0;
    features.fragmentDensityMapNonSubsampledImages = 0;
    VkDeviceCreateInfo di = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .pNext = &features, .queueCreateInfoCount = 1, .pQueueCreateInfos = &qi, .enabledExtensionCount = 1, .ppEnabledExtensionNames = &ext};
    CK(vkCreateDevice(gpu, &di, 0, &d));
    vkGetDeviceQueue(d, fam, 0, &queue);
    vkGetPhysicalDeviceMemoryProperties(gpu, &mem);
    VkCommandPoolCreateInfo pci = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, .queueFamilyIndex = fam};
    VkCommandPool pool;
    CK(vkCreateCommandPool(d, &pci, 0, &pool));
    VkCommandBufferAllocateInfo ca = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = pool, .commandBufferCount = 1};
    CK(vkAllocateCommandBuffers(d, &ca, &cmd));
    VkBufferCreateInfo bci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = W * H * 4, .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT};
    VkBuffer host;
    CK(vkCreateBuffer(d, &bci, 0, &host));
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(d, host, &req);
    VkMemoryAllocateInfo ma = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = req.size, .memoryTypeIndex = mem_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
    VkDeviceMemory hm;
    CK(vkAllocateMemory(d, &ma, 0, &hm));
    CK(vkBindBufferMemory(d, host, hm, 0));
    uint8_t* mapped;
    CK(vkMapMemory(d, hm, 0, VK_WHOLE_SIZE, 0, (void**)&mapped));
    const VkImageUsageFlags eyeUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    Image eyes[] = {image(W, H, VK_FORMAT_R8G8B8A8_UNORM, eyeUsage, 0), image(W, H, VK_FORMAT_R8G8B8A8_UNORM, eyeUsage, VK_IMAGE_CREATE_SUBSAMPLED_BIT_EXT)};
    Image output = image(OUTW, OUTH, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, 0);
    Image map = image(dw, dh, VK_FORMAT_R8G8_UNORM, VK_IMAGE_USAGE_FRAGMENT_DENSITY_MAP_BIT_EXT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, 0);
    for (unsigned y = 0; y < dh; y++)
        for (unsigned x = 0; x < dw; x++)
        {
            float u = (x + 0.5f) / dw * 2 - 1, v = (y + 0.5f) / dh * 2 - 1, r = u * u + v * v;
            uint8_t density = r < 0.36f ? 255 : r < 0.74f ? 128
                                                          : 64;
            mapped[2 * (y * dw + x)] = mapped[2 * (y * dw + x) + 1] = density;
        }
    begin();
    barrier(map.handle, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, VK_ACCESS_TRANSFER_WRITE_BIT);
    VkBufferImageCopy cp = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .imageExtent = {dw, dh, 1}};
    vkCmdCopyBufferToImage(cmd, host, map.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &cp);
    barrier(map.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_FRAGMENT_DENSITY_MAP_OPTIMAL_EXT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_DENSITY_PROCESS_BIT_EXT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_FRAGMENT_DENSITY_MAP_READ_BIT_EXT);
    submit();
    view(&map, VK_FORMAT_R8G8_UNORM);
    view(&output, VK_FORMAT_R8G8B8A8_UNORM);
    for (unsigned i = 0; i < 2; i++)
        view(&eyes[i], VK_FORMAT_R8G8B8A8_UNORM);
    VkShaderModule shaders[] = {load(argv[1]), load(argv[2])};
    VkPipelineShaderStageCreateInfo stages[2] = {{.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = shaders[0], .pName = "main"}, {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = shaders[1], .pName = "main"}};
    VkPipelineLayoutCreateInfo lci = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    VkPipelineLayout layout;
    CK(vkCreatePipelineLayout(d, &lci, 0, &layout));
    VkShaderModule reconstruction = load(argv[3]);
    VkQueryPoolCreateInfo qci = {.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO, .queryType = VK_QUERY_TYPE_TIMESTAMP, .queryCount = 3 * N};
    VkQueryPool qs;
    CK(vkCreateQueryPool(d, &qci, 0, &qs));
    int ok = 1;
    for (int block = 0; block < 4; block++)
    {
        int on = block == 1 || block == 2;
        unsigned ac = on ? 2 : 1;
        VkAttachmentDescription at[2] = {
            {.format = VK_FORMAT_R8G8B8A8_UNORM, .samples = VK_SAMPLE_COUNT_1_BIT, .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, .storeOp = VK_ATTACHMENT_STORE_OP_STORE, .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, .finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL},
            {.format = VK_FORMAT_R8G8_UNORM, .samples = VK_SAMPLE_COUNT_1_BIT, .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD, .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE, .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE, .initialLayout = VK_IMAGE_LAYOUT_FRAGMENT_DENSITY_MAP_OPTIMAL_EXT, .finalLayout = VK_IMAGE_LAYOUT_FRAGMENT_DENSITY_MAP_OPTIMAL_EXT}};
        VkAttachmentReference col = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkRenderPassFragmentDensityMapCreateInfoEXT fdm = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_FRAGMENT_DENSITY_MAP_CREATE_INFO_EXT, .fragmentDensityMapAttachment = {1, VK_IMAGE_LAYOUT_FRAGMENT_DENSITY_MAP_OPTIMAL_EXT}};
        VkSubpassDescription sp = {.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS, .colorAttachmentCount = 1, .pColorAttachments = &col};
        VkSubpassDependency dep = {.srcSubpass = 0, .dstSubpass = VK_SUBPASS_EXTERNAL, .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, .dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT, .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT};
        VkRenderPassCreateInfo rpci = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, .attachmentCount = 1, .pAttachments = at, .subpassCount = 1, .pSubpasses = &sp, .dependencyCount = 1, .pDependencies = &dep};
        VkRenderPass resolved;
        CK(vkCreateRenderPass(d, &rpci, 0, &resolved));
        at[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        dep.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dep.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        rpci.pNext = on ? &fdm : 0;
        rpci.attachmentCount = ac;
        VkRenderPass pass;
        CK(vkCreateRenderPass(d, &rpci, 0, &pass));
        VkFramebufferCreateInfo fci = {.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, .renderPass = resolved, .attachmentCount = 1, .pAttachments = &output.view, .width = OUTW, .height = OUTH, .layers = 1};
        VkFramebuffer ofb;
        CK(vkCreateFramebuffer(d, &fci, 0, &ofb));
        VkImageView views[] = {eyes[on].view, map.view};
        fci.renderPass = pass;
        fci.attachmentCount = ac;
        fci.pAttachments = views;
        fci.width = W;
        fci.height = H;
        VkFramebuffer fb;
        CK(vkCreateFramebuffer(d, &fci, 0, &fb));
        VkViewport vp = {0, 0, W, H, 0, 1};
        VkRect2D rc = {{0, 0}, {W, H}};
        VkPipelineViewportStateCreateInfo vps = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .pViewports = &vp, .scissorCount = 1, .pScissors = &rc};
        VkPipelineVertexInputStateCreateInfo vis = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo ia = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
        VkPipelineRasterizationStateCreateInfo rs = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, .polygonMode = VK_POLYGON_MODE_FILL, .lineWidth = 1};
        VkPipelineMultisampleStateCreateInfo ms = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
        VkPipelineColorBlendAttachmentState cb = {.colorWriteMask = 15};
        VkPipelineColorBlendStateCreateInfo bs = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, .attachmentCount = 1, .pAttachments = &cb};
        VkGraphicsPipelineCreateInfo gp = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .stageCount = 2, .pStages = stages, .pVertexInputState = &vis, .pInputAssemblyState = &ia, .pViewportState = &vps, .pRasterizationState = &rs, .pMultisampleState = &ms, .pColorBlendState = &bs, .layout = layout, .renderPass = pass};
        VkPipeline pipe;
        CK(vkCreateGraphicsPipelines(d, 0, 1, &gp, 0, &pipe));
        VkRenderPassBeginInfo rbi = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, .renderPass = pass, .framebuffer = fb, .renderArea = rc};
        VkSamplerCreateInfo sci = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, .flags = on ? VK_SAMPLER_CREATE_SUBSAMPLED_BIT_EXT : 0, .magFilter = VK_FILTER_NEAREST, .minFilter = VK_FILTER_NEAREST, .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST, .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
        VkSampler sampler;
        CK(vkCreateSampler(d, &sci, 0, &sampler));
        VkDescriptorSetLayoutBinding binding = {.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT, .pImmutableSamplers = &sampler};
        VkDescriptorSetLayoutCreateInfo slci = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 1, .pBindings = &binding};
        VkDescriptorSetLayout sl;
        CK(vkCreateDescriptorSetLayout(d, &slci, 0, &sl));
        VkPipelineLayoutCreateInfo plci = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1, .pSetLayouts = &sl};
        VkPipelineLayout ol;
        CK(vkCreatePipelineLayout(d, &plci, 0, &ol));
        VkDescriptorPoolSize ps = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
        VkDescriptorPoolCreateInfo dpci = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = &ps};
        VkDescriptorPool dp;
        CK(vkCreateDescriptorPool(d, &dpci, 0, &dp));
        VkDescriptorSetAllocateInfo dsai = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = dp, .descriptorSetCount = 1, .pSetLayouts = &sl};
        VkDescriptorSet ds;
        CK(vkAllocateDescriptorSets(d, &dsai, &ds));
        VkDescriptorImageInfo si = {sampler, eyes[on].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet update = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = ds, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &si};
        vkUpdateDescriptorSets(d, 1, &update, 0, 0);
        vp.width = OUTW;
        vp.height = OUTH;
        rc.extent = (VkExtent2D){OUTW, OUTH};
        gp.layout = ol;
        gp.renderPass = resolved;
        stages[1].module = reconstruction;
        VkPipeline op;
        CK(vkCreateGraphicsPipelines(d, 0, 1, &gp, 0, &op));
        stages[1].module = shaders[1];
        VkRenderPassBeginInfo obi = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, .renderPass = resolved, .framebuffer = ofb, .renderArea = rc};
        long clocks[N];
        for (int i = 0; i < N; i++)
        {
            begin();
            vkCmdResetQueryPool(cmd, qs, 3 * i, 3);
            vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, qs, 3 * i);
            vkCmdBeginRenderPass(cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
            vkCmdDraw(cmd, 3, 1, 0, 0);
            vkCmdEndRenderPass(cmd);
            vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, qs, 3 * i + 1);
            vkCmdBeginRenderPass(cmd, &obi, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, op);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ol, 0, 1, &ds, 0, 0);
            vkCmdDraw(cmd, 3, 1, 0, 0);
            vkCmdEndRenderPass(cmd);
            vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, qs, 3 * i + 2);
            if (i == N - 1)
            {
                cp.imageExtent = (VkExtent3D){OUTW, OUTH, 1};
                vkCmdCopyImageToBuffer(cmd, output.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, host, 1, &cp);
            }
            submit();
            clocks[i] = mhz();
        }
        uint64_t ticks[3 * N];
        double eye[N - 16], times[N - 16];
        CK(vkGetQueryPoolResults(d, qs, 0, 3 * N, sizeof(ticks), ticks, 8, VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));
        long min = 100000, max = 0;
        for (int i = 16; i < N; i++)
        {
            eye[i - 16] = ((ticks[3 * i + 1] - ticks[3 * i]) & mask) * p2.properties.limits.timestampPeriod / 1e6;
            times[i - 16] = ((ticks[3 * i + 2] - ticks[3 * i]) & mask) * p2.properties.limits.timestampPeriod / 1e6;
            if (clocks[i] < min)
                min = clocks[i];
            if (clocks[i] > max)
                max = clocks[i];
        }
        qsort(eye, N - 16, sizeof(double), cmp);
        qsort(times, N - 16, sizeof(double), cmp);
        double center = transitions(mapped, OUTW / 2 - 64, OUTH / 2 - 64), edge = transitions(mapped, 64, 64);
        printf("block=%d subsampled=%d map=%ux%u eyeMs=%.3f eyePlusResolveMs=%.3f gpuMHz=%ld..%ld centerTransitions=%.3f edgeTransitions=%.3f\n", block, on, dw, dh, (eye[15] + eye[16]) / 2, (times[15] + times[16]) / 2, min, max, center, edge);
        fflush(stdout);
        unsigned opaque = 0, gmin = 255, gmax = 0;
        for (unsigned y = 64; y < 192; y++)
            for (unsigned x = 64; x < 192; x++)
            {
                const uint8_t* pixel = mapped + 4 * (OUTW * y + x);
                opaque += pixel[3] == 255;
                if (pixel[1] < gmin)
                    gmin = pixel[1];
                if (pixel[1] > gmax)
                    gmax = pixel[1];
            }
        printf("edgeOpaque=%u/16384 edgeGreenRange=%u..%u\n", opaque, gmin, gmax);
        ok &= center > .27 && center < .36 && (on ? edge < .05 : edge > .27) && opaque == 16384 && gmax - gmin >= 100;
        vkDestroyPipeline(d, op, 0);
        vkDestroyFramebuffer(d, ofb, 0);
        vkDestroyRenderPass(d, resolved, 0);
        vkDestroyDescriptorPool(d, dp, 0);
        vkDestroyPipelineLayout(d, ol, 0);
        vkDestroyDescriptorSetLayout(d, sl, 0);
        vkDestroySampler(d, sampler, 0);
        vkDestroyPipeline(d, pipe, 0);
        vkDestroyFramebuffer(d, fb, 0);
        vkDestroyRenderPass(d, pass, 0);
    }
    vkDestroyQueryPool(d, qs, 0);
    vkDestroyPipelineLayout(d, layout, 0);
    for (int i = 0; i < 2; i++)
        vkDestroyShaderModule(d, shaders[i], 0);
    vkUnmapMemory(d, hm);
    vkDestroyBuffer(d, host, 0);
    vkFreeMemory(d, hm, 0);
    vkDestroyShaderModule(d, reconstruction, 0);
    Image ims[] = {eyes[0], eyes[1], map, output};
    for (int i = 0; i < 4; i++)
    {
        vkDestroyImageView(d, ims[i].view, 0);
        vkDestroyImage(d, ims[i].handle, 0);
        vkFreeMemory(d, ims[i].memory, 0);
    }
    vkDestroyCommandPool(d, pool, 0);
    vkDestroyDevice(d, 0);
    vkDestroyInstance(inst, 0);
    return !ok;
}
