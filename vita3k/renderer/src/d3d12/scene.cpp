// Vita3K emulator project - Xbox Series S UWP D3D12 Port
#include <renderer/d3d12/functions.h>
#include <renderer/d3d12/state.h>
#include <renderer/d3d12/types.h>

#include <config/state.h>
#include <mem/state.h>
#include <mem/functions.h>
#include <util/log.h>

namespace renderer::d3d12 {

static D3D12_PRIMITIVE_TOPOLOGY gxm_to_d3d_topology(SceGxmPrimitiveType type) {
    switch (type) {
    case SCE_GXM_PRIMITIVE_TRIANGLES:
        return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    case SCE_GXM_PRIMITIVE_TRIANGLE_STRIP:
        return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
    case SCE_GXM_PRIMITIVE_LINES:
        return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
    case SCE_GXM_PRIMITIVE_POINTS:
        return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
    default:
        return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    }
}

void draw(D3D12Context &context, SceGxmPrimitiveType type, SceGxmIndexFormat format,
    Ptr<void> indices, size_t count, uint32_t instance_count, MemState &mem, const Config &config) {
    if (!context.command_list || count == 0) return;

    context.command_list->IASetPrimitiveTopology(gxm_to_d3d_topology(type));

    if (indices) {
        // Indexed draw call
        context.command_list->DrawIndexedInstanced(
            static_cast<UINT>(count),
            instance_count > 0 ? instance_count : 1,
            0,
            0,
            0);
    } else {
        // Non-indexed draw call
        context.command_list->DrawInstanced(
            static_cast<UINT>(count),
            instance_count > 0 ? instance_count : 1,
            0,
            0);
    }
}

void mid_scene_flush(D3D12Context &context, const SceGxmNotification notification) {
}

void new_frame(D3D12Context &context) {
}

void sync_clipping(D3D12Context &context) {
}

void sync_stencil_func(D3D12Context &context, const bool is_back) {
}

void sync_depth_bias(D3D12Context &context) {
}

void sync_depth_data(D3D12Context &context) {
}

void sync_stencil_data(D3D12Context &context, const MemState &mem) {
}

void sync_point_line_width(D3D12Context &context, const bool is_front) {
}

void sync_texture(D3D12Context &context, MemState &mem, std::size_t index, SceGxmTexture texture, const Config &config) {
}

void sync_viewport_flat(D3D12Context &context) {
    if (!context.command_list || !context.current_rt) return;

    D3D12_VIEWPORT viewport = {};
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = static_cast<float>(context.current_rt->width);
    viewport.Height = static_cast<float>(context.current_rt->height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    context.command_list->RSSetViewports(1, &viewport);
}

void sync_viewport_real(D3D12Context &context, const float xOffset, const float yOffset, const float zOffset,
    const float xScale, const float yScale, const float zScale) {
    if (!context.command_list) return;

    D3D12_VIEWPORT viewport = {};
    viewport.TopLeftX = xOffset;
    viewport.TopLeftY = yOffset;
    viewport.Width = xScale * 2.0f;
    viewport.Height = yScale * 2.0f;
    viewport.MinDepth = zOffset;
    viewport.MaxDepth = zOffset + zScale;
    context.command_list->RSSetViewports(1, &viewport);
}

void sync_visibility_buffer(D3D12Context &context, Ptr<uint32_t> buffer, uint32_t stride) {
}

void sync_visibility_index(D3D12Context &context, bool enable, uint32_t index, bool is_increment) {
}

void refresh_pipeline(D3D12Context &context) {
}

} // namespace renderer::d3d12
