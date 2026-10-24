// Vita3K emulator project - Xbox Series S UWP D3D12 Port
#pragma once

#include "renderer/d3d12/state.h"

struct Config;
struct MemState;

namespace renderer::d3d12 {

bool create(std::unique_ptr<renderer::State> &state, const Config &config);

bool create(D3D12State &state, std::unique_ptr<Context> &context, MemState &mem);
bool create(D3D12State &state, std::unique_ptr<RenderTarget> &rt, const SceGxmRenderTargetParams &params, const FeatureState &features);
void destroy(D3D12State &state, std::unique_ptr<RenderTarget> &rt);
bool create(std::unique_ptr<VertexProgram> &vp, D3D12State &state, const SceGxmProgram &program);
bool create(std::unique_ptr<FragmentProgram> &fp, D3D12State &state, const SceGxmProgram &program, const SceGxmBlendInfo *blend);

void draw(D3D12Context &context, SceGxmPrimitiveType type, SceGxmIndexFormat format,
    Ptr<void> indices, size_t count, uint32_t instance_count, MemState &mem, const Config &config);

void mid_scene_flush(D3D12Context &context, const SceGxmNotification notification);
void new_frame(D3D12Context &context);
void signal_sync_object(D3D12State &state, SceGxmSyncObject *sync_object, uint32_t timestamp);

void set_context(D3D12Context &context, MemState &mem, D3D12RenderTarget *rt, const FeatureState &features);
void set_uniform_buffer(D3D12Context &context, MemState &mem, const ShaderProgram *program, const bool vertex_shader, const int block_num, const int size, Ptr<uint8_t> data);

void sync_clipping(D3D12Context &context);
void sync_stencil_func(D3D12Context &context, const bool is_back);
void sync_depth_bias(D3D12Context &context);
void sync_depth_data(D3D12Context &context);
void sync_stencil_data(D3D12Context &context, const MemState &mem);
void sync_point_line_width(D3D12Context &context, const bool is_front);
void sync_texture(D3D12Context &context, MemState &mem, std::size_t index, SceGxmTexture texture, const Config &config);
void sync_viewport_flat(D3D12Context &context);
void sync_viewport_real(D3D12Context &context, const float xOffset, const float yOffset, const float zOffset,
    const float xScale, const float yScale, const float zScale);
void sync_visibility_buffer(D3D12Context &context, Ptr<uint32_t> buffer, uint32_t stride);
void sync_visibility_index(D3D12Context &context, bool enable, uint32_t index, bool is_increment);

void refresh_pipeline(D3D12Context &context);

} // namespace renderer::d3d12
