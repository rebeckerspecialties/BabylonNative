#pragma once

#include <cstddef>
#include <cstdint>

// Shared C ABI declarations for the GraphicsWgpu Rust backend.
// Keep this header as the single declaration source used by both
// Core/GraphicsWgpu and Plugins/NativeWebGPU.

struct BabylonWgpuConfig final
{
    uint32_t width{};
    uint32_t height{};
    void* surface_layer{};
    uint8_t prefer_low_power{};
    uint8_t enable_validation{};
    uint8_t hdr10{};
    uint8_t reserved1{};
};

struct BabylonWgpuInfo final
{
    uint32_t backend{};
    uint32_t vendor_id{};
    uint32_t device_id{};
    char adapter_name[128]{};
};

struct BabylonWgpuFeatureInfo final
{
    uint32_t shader_f16{};
    uint32_t indirect_first_instance{};
    uint32_t subgroup{};
    uint32_t subgroup_barrier{};
    uint32_t multi_draw_indirect_count{};
    uint32_t min_subgroup_size{};
    uint32_t max_subgroup_size{};
};

extern "C"
{
    void* babylon_wgpu_create(const BabylonWgpuConfig* config);
    void babylon_wgpu_destroy(void* context);
    bool babylon_wgpu_resize(void* context, uint32_t width, uint32_t height);
    bool babylon_wgpu_render(void* context);
    bool babylon_wgpu_request_screenshot(void* context);
    bool babylon_wgpu_get_screenshot_info(const void* context, uint32_t* width, uint32_t* height, size_t* byte_len);
    bool babylon_wgpu_copy_screenshot(const void* context, uint8_t* output, size_t output_len);
    bool babylon_wgpu_get_info(const void* context, BabylonWgpuInfo* output_info);
    bool babylon_wgpu_get_feature_info(BabylonWgpuFeatureInfo* output_info);
    bool babylon_wgpu_get_last_error(char* output, size_t output_len);

    bool babylon_wgpu_dispatch_compute_global(
        const char* shader_source,
        const char* entry_point,
        uint32_t x,
        uint32_t y,
        uint32_t z);
    void babylon_wgpu_mark_webgpu_draw_requested();
    bool babylon_wgpu_is_webgpu_draw_enabled();
    uint64_t babylon_wgpu_get_render_frame_count();
    uint64_t babylon_wgpu_get_canvas_texture_hash();
    uint32_t babylon_wgpu_get_canvas_texture_width();
    uint32_t babylon_wgpu_get_canvas_texture_height();
    // TODO(bgfx-removal): Remove these legacy debug_texture aliases once all call
    // sites have migrated to the canvas-prefixed names above.
    uint64_t babylon_wgpu_get_debug_texture_hash();
    uint32_t babylon_wgpu_get_debug_texture_width();
    uint32_t babylon_wgpu_get_debug_texture_height();
    uint64_t babylon_wgpu_get_estimated_gpu_memory_bytes();
    uint64_t babylon_wgpu_refresh_estimated_gpu_memory_bytes();
    uint64_t babylon_wgpu_get_canvas_texture_import_skip_count();
    // TODO(bgfx-removal): Remove this legacy alias.
    uint64_t babylon_wgpu_get_debug_texture_import_skip_count();
    void babylon_wgpu_reset_webgpu_draw_requested();
    bool babylon_wgpu_import_canvas_texture_from_native(const void* native_texture, uint32_t width, uint32_t height);
    // TODO(bgfx-removal): Remove this legacy alias.
    bool babylon_wgpu_set_debug_texture_from_native(const void* native_texture, uint32_t width, uint32_t height);

    uint64_t babylon_wgpu_native_create_buffer(uint64_t size, uint32_t usage, bool mapped_at_creation);
    bool babylon_wgpu_native_write_buffer(uint64_t buffer_id, uint64_t offset, const uint8_t* data, size_t data_len);
    uint64_t babylon_wgpu_native_create_texture(const char* descriptor_json);
    uint64_t babylon_wgpu_native_import_metal_texture(const void* native_texture, const char* descriptor_json);
    void* babylon_wgpu_native_get_metal_device();
    uint64_t babylon_wgpu_native_create_texture_view(uint64_t texture_id, const char* descriptor_json);
    uint64_t babylon_wgpu_native_create_sampler(const char* descriptor_json);
    uint64_t babylon_wgpu_native_create_shader_module(const char* code);
    uint64_t babylon_wgpu_native_create_bind_group_layout(const char* descriptor_json);
    uint64_t babylon_wgpu_native_create_pipeline_layout(const char* descriptor_json);
    uint64_t babylon_wgpu_native_create_bind_group(const char* descriptor_json);
    uint64_t babylon_wgpu_native_create_render_pipeline(const char* descriptor_json);
    uint64_t babylon_wgpu_native_render_pipeline_get_bind_group_layout(uint64_t pipeline_id, uint32_t index);
    uint64_t babylon_wgpu_native_create_compute_pipeline(const char* descriptor_json);
    uint64_t babylon_wgpu_native_compute_pipeline_get_bind_group_layout(uint64_t pipeline_id, uint32_t index);
    uint64_t babylon_wgpu_native_create_command_encoder();
    uint64_t babylon_wgpu_native_command_encoder_begin_render_pass(uint64_t encoder_id, const char* descriptor_json);
    uint64_t babylon_wgpu_native_command_encoder_begin_compute_pass(uint64_t encoder_id, const char* descriptor_json);
    bool babylon_wgpu_native_command_encoder_copy_buffer_to_buffer(
        uint64_t encoder_id,
        uint64_t source_id,
        uint64_t source_offset,
        uint64_t destination_id,
        uint64_t destination_offset,
        uint64_t size);
    bool babylon_wgpu_native_command_encoder_copy_buffer_to_texture(
        uint64_t encoder_id,
        const char* source_json,
        const char* destination_json,
        const char* size_json);
    bool babylon_wgpu_native_command_encoder_copy_texture_to_buffer(
        uint64_t encoder_id,
        const char* source_json,
        const char* destination_json,
        const char* size_json);
    bool babylon_wgpu_native_command_encoder_copy_texture_to_texture(
        uint64_t encoder_id,
        const char* source_json,
        const char* destination_json,
        const char* size_json);
    bool babylon_wgpu_native_command_encoder_clear_buffer(
        uint64_t encoder_id,
        uint64_t buffer_id,
        uint64_t offset,
        uint64_t size);
    uint64_t babylon_wgpu_native_command_encoder_finish(uint64_t encoder_id);
    bool babylon_wgpu_native_render_pass_set_pipeline(uint64_t pass_id, uint64_t pipeline_id);
    bool babylon_wgpu_native_render_pass_set_bind_group(
        uint64_t pass_id,
        uint32_t index,
        uint64_t bind_group_id,
        const uint32_t* dynamic_offsets,
        size_t dynamic_offset_count);
    bool babylon_wgpu_native_render_pass_set_vertex_buffer(
        uint64_t pass_id,
        uint32_t slot,
        uint64_t buffer_id,
        uint64_t offset,
        uint64_t size);
    bool babylon_wgpu_native_render_pass_set_index_buffer(
        uint64_t pass_id,
        uint64_t buffer_id,
        const char* format,
        uint64_t offset,
        uint64_t size);
    bool babylon_wgpu_native_render_pass_set_viewport(
        uint64_t pass_id,
        float x,
        float y,
        float width,
        float height,
        float min_depth,
        float max_depth);
    bool babylon_wgpu_native_render_pass_set_scissor_rect(
        uint64_t pass_id,
        uint32_t x,
        uint32_t y,
        uint32_t width,
        uint32_t height);
    bool babylon_wgpu_native_render_pass_set_blend_constant(
        uint64_t pass_id,
        double r,
        double g,
        double b,
        double a);
    bool babylon_wgpu_native_render_pass_set_stencil_reference(uint64_t pass_id, uint32_t reference);
    bool babylon_wgpu_native_render_pass_draw(
        uint64_t pass_id,
        uint32_t vertex_count,
        uint32_t instance_count,
        uint32_t first_vertex,
        uint32_t first_instance);
    bool babylon_wgpu_native_render_pass_draw_indexed(
        uint64_t pass_id,
        uint32_t index_count,
        uint32_t instance_count,
        uint32_t first_index,
        int32_t base_vertex,
        uint32_t first_instance);
    bool babylon_wgpu_native_render_pass_draw_indirect(
        uint64_t pass_id,
        uint64_t buffer_id,
        uint64_t offset);
    bool babylon_wgpu_native_render_pass_draw_indexed_indirect(
        uint64_t pass_id,
        uint64_t buffer_id,
        uint64_t offset);
    bool babylon_wgpu_native_render_pass_multi_draw_indirect(
        uint64_t pass_id,
        uint64_t buffer_id,
        uint64_t offset,
        uint32_t count);
    bool babylon_wgpu_native_render_pass_multi_draw_indexed_indirect(
        uint64_t pass_id,
        uint64_t buffer_id,
        uint64_t offset,
        uint32_t count);
    bool babylon_wgpu_native_render_pass_record_commands(
        uint64_t pass_id,
        const uint32_t* commands,
        size_t command_word_count);
    bool babylon_wgpu_native_render_pass_end(uint64_t pass_id);
    bool babylon_wgpu_native_compute_pass_set_pipeline(uint64_t pass_id, uint64_t pipeline_id);
    bool babylon_wgpu_native_compute_pass_set_bind_group(
        uint64_t pass_id,
        uint32_t index,
        uint64_t bind_group_id,
        const uint32_t* dynamic_offsets,
        size_t dynamic_offset_count);
    bool babylon_wgpu_native_compute_pass_dispatch_workgroups(
        uint64_t pass_id,
        uint32_t x,
        uint32_t y,
        uint32_t z);
    bool babylon_wgpu_native_compute_pass_dispatch_workgroups_indirect(
        uint64_t pass_id,
        uint64_t buffer_id,
        uint64_t offset);
    bool babylon_wgpu_native_compute_pass_end(uint64_t pass_id);
    bool babylon_wgpu_native_queue_submit(const uint64_t* command_buffer_ids, size_t command_buffer_count);
    bool babylon_wgpu_native_queue_wait_submitted_work();
    bool babylon_wgpu_native_queue_write_texture(
        const char* destination_json,
        const uint8_t* data,
        size_t data_len,
        const char* layout_json,
        const char* size_json);
    bool babylon_wgpu_native_queue_copy_external_image_to_texture(
        const void* native_texture,
        uint32_t source_width,
        uint32_t source_height,
        const char* destination_json,
        const char* size_json);
    bool babylon_wgpu_native_queue_copy_external_image_rgba_to_texture(
        const uint8_t* rgba,
        size_t rgba_len,
        uint32_t source_width,
        uint32_t source_height,
        uint32_t source_origin_x,
        uint32_t source_origin_y,
        uint32_t flip_y,
        const char* destination_json,
        const char* size_json);
    uint64_t babylon_wgpu_native_canvas_get_current_texture(
        uint64_t canvas_id,
        uint32_t width,
        uint32_t height,
        const char* format,
        uint32_t usage);
    bool babylon_wgpu_native_canvas_destroy(uint64_t canvas_id);
    bool babylon_wgpu_native_test_read_texture_pixel(
        uint64_t texture_id,
        uint32_t x,
        uint32_t y,
        uint8_t* out_rgba,
        size_t out_rgba_len);
    uint64_t babylon_wgpu_native_get_external_image_upload_borrowed_count();
    uint64_t babylon_wgpu_native_get_external_image_upload_borrowed_bytes();
    uint64_t babylon_wgpu_native_get_external_image_upload_owned_count();
    uint64_t babylon_wgpu_native_get_external_image_upload_owned_bytes();
    void babylon_wgpu_native_reset_external_image_upload_stats();
    bool babylon_wgpu_native_destroy_resource(uint32_t kind, uint64_t resource_id);
}
