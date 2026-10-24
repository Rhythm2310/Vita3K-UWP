// Vita3K emulator project - Xbox Series S UWP D3D12 Port
#include <gxm/types.h>
#include <renderer/commands.h>
#include <renderer/driver_functions.h>
#include <renderer/state.h>
#include <renderer/types.h>

#include <renderer/d3d12/functions.h>
#include <renderer/d3d12/state.h>
#include <renderer/d3d12/types.h>

#include <util/align.h>
#include <util/log.h>
#include <util/tracy.h>

#include <config/state.h>

namespace renderer {

COMMAND_SET_STATE(region_clip) {
    TRACY_FUNC_COMMANDS_SET_STATE(region_clip);
    render_context->record.region_clip_mode = helper.pop<SceGxmRegionClipMode>();

    uint32_t factor = 1;
    const bool has_msaa = (render_context->current_render_target && render_context->current_render_target->multisample_mode);
    const bool has_downscale = render_context->record.color_surface.downscale;
    if (has_msaa && !has_downscale)
        factor = 2;
    else if (!has_msaa && has_downscale)
        factor = 1;

    const uint32_t xMin = helper.pop<uint32_t>() * factor;
    const uint32_t xMax = helper.pop<uint32_t>() * factor;
    const uint32_t yMin = helper.pop<uint32_t>() * factor;
    const uint32_t yMax = helper.pop<uint32_t>() * factor;

    render_context->record.region_clip_min.x = static_cast<SceInt>(align_down(xMin, SCE_GXM_TILE_SIZEX));
    render_context->record.region_clip_min.y = static_cast<SceInt>(align_down(yMin, SCE_GXM_TILE_SIZEY));
    render_context->record.region_clip_max.x = static_cast<SceInt>(align(xMax, SCE_GXM_TILE_SIZEX)) - 1;
    render_context->record.region_clip_max.y = static_cast<SceInt>(align(yMax, SCE_GXM_TILE_SIZEY)) - 1;

    if (renderer.current_backend == Backend::D3D12) {
        d3d12::sync_clipping(*reinterpret_cast<d3d12::D3D12Context *>(render_context));
    }
}

COMMAND_SET_STATE(program) {
    TRACY_FUNC_COMMANDS_SET_STATE(program);
    const Ptr<void> program = helper.pop<Ptr<void>>();
    const bool is_fragment = helper.pop<bool>();

    if (is_fragment) {
        render_context->record.fragment_program = program.cast<SceGxmFragmentProgram>();
        const SceGxmFragmentProgram *gxm_program = render_context->record.fragment_program.get(mem);
        render_context->record.fragment_program_hash = gxm_program->renderer_data->hash;
        render_context->record.is_maskupdate = gxm_program->is_maskupdate;
    } else {
        render_context->record.vertex_program = program.cast<SceGxmVertexProgram>();
        const SceGxmVertexProgram *gxm_program = render_context->record.vertex_program.get(mem);
        render_context->record.vertex_program_hash = gxm_program->renderer_data->hash;
    }

    if (renderer.current_backend == Backend::D3D12) {
        d3d12::refresh_pipeline(*reinterpret_cast<d3d12::D3D12Context *>(render_context));
    }
}

COMMAND_SET_STATE(uniform_buffer) {
    TRACY_FUNC_COMMANDS_SET_STATE(uniform_buffer);
    const Ptr<uint8_t> data = helper.pop<const Ptr<uint8_t>>();
    const bool is_vertex = helper.pop<bool>();
    const int block_num = helper.pop<int>();
    const std::uint32_t size = helper.pop<std::uint32_t>();

    renderer::ShaderProgram *program = is_vertex ? reinterpret_cast<ShaderProgram *>(render_context->record.vertex_program.get(mem)->renderer_data.get())
                                                 : reinterpret_cast<ShaderProgram *>(render_context->record.fragment_program.get(mem)->renderer_data.get());

    if (renderer.current_backend == Backend::D3D12) {
        d3d12::set_uniform_buffer(*reinterpret_cast<d3d12::D3D12Context *>(render_context), mem, program, is_vertex, block_num, size, data);
    }

    const auto offset = program->uniform_buffer_data_offsets.at(block_num);
    if (offset != static_cast<uint32_t>(-1) && config.log_active_shaders) {
        const int base_binding_ubo_relative = is_vertex ? 0 : (SCE_GXM_REAL_MAX_UNIFORM_BUFFER + 1);
        std::vector<uint8_t> my_data(data.get(mem), data.get(mem) + size);
        render_context->ubo_data[base_binding_ubo_relative + block_num] = std::move(my_data);
    }
}

COMMAND_SET_STATE(viewport) {
    TRACY_FUNC_COMMANDS_SET_STATE(viewport);
    const bool flat = helper.pop<bool>();
    render_context->record.viewport_flat = flat;

    const float previous_flip_y = render_context->record.viewport_flip[1];
    if (flat) {
        const float xOffset = helper.pop<float>();
        const float yOffset = helper.pop<float>();
        const float zOffset = helper.pop<float>();
        const float xScale = helper.pop<float>();
        const float yScale = helper.pop<float>();
        const float zScale = helper.pop<float>();

        render_context->record.viewport_real[0] = xOffset;
        render_context->record.viewport_real[1] = yOffset;
        render_context->record.viewport_real[2] = zOffset;
        render_context->record.viewport_real[3] = xScale;
        render_context->record.viewport_real[4] = yScale;
        render_context->record.viewport_real[5] = zScale;

        render_context->record.viewport_flip[0] = (xScale < 0.0f) ? -1.0f : 1.0f;
        render_context->record.viewport_flip[1] = (yScale < 0.0f) ? -1.0f : 1.0f;

        if (renderer.current_backend == Backend::D3D12) {
            d3d12::sync_viewport_real(*reinterpret_cast<d3d12::D3D12Context *>(render_context), xOffset, yOffset, zOffset, xScale, yScale, zScale);
        }
    } else {
        if (renderer.current_backend == Backend::D3D12) {
            d3d12::sync_viewport_flat(*reinterpret_cast<d3d12::D3D12Context *>(render_context));
        }
    }

    if (previous_flip_y != render_context->record.viewport_flip[1]) {
        if (renderer.current_backend == Backend::D3D12) {
            d3d12::sync_point_line_width(*reinterpret_cast<d3d12::D3D12Context *>(render_context), true);
            d3d12::sync_point_line_width(*reinterpret_cast<d3d12::D3D12Context *>(render_context), false);
        }
    }
}

COMMAND_SET_STATE(depth_bias) {
    TRACY_FUNC_COMMANDS_SET_STATE(depth_bias);
    const int factor = helper.pop<int>();
    const int units = helper.pop<int>();

    render_context->record.depth_bias_factor = factor;
    render_context->record.depth_bias_units = units;

    if (renderer.current_backend == Backend::D3D12) {
        d3d12::sync_depth_bias(*reinterpret_cast<d3d12::D3D12Context *>(render_context));
    }
}

COMMAND_SET_STATE(depth_func) {
    TRACY_FUNC_COMMANDS_SET_STATE(depth_func);
    const SceGxmDepthFunc depth_func = helper.pop<SceGxmDepthFunc>();
    render_context->record.depth_func = depth_func;

    if (renderer.current_backend == Backend::D3D12) {
        d3d12::sync_depth_data(*reinterpret_cast<d3d12::D3D12Context *>(render_context));
    }
}

COMMAND_SET_STATE(depth_write_enable) {
    TRACY_FUNC_COMMANDS_SET_STATE(depth_write_enable);
    const SceGxmDepthWriteMode mode = helper.pop<SceGxmDepthWriteMode>();
    render_context->record.depth_write_mode = mode;

    if (renderer.current_backend == Backend::D3D12) {
        d3d12::sync_depth_data(*reinterpret_cast<d3d12::D3D12Context *>(render_context));
    }
}

COMMAND_SET_STATE(polygon_mode) {
    TRACY_FUNC_COMMANDS_SET_STATE(polygon_mode);
    const SceGxmPolygonMode polygon_mode = helper.pop<SceGxmPolygonMode>();
    render_context->record.polygon_mode = polygon_mode;

    if (renderer.current_backend == Backend::D3D12) {
        d3d12::refresh_pipeline(*reinterpret_cast<d3d12::D3D12Context *>(render_context));
    }
}

COMMAND_SET_STATE(point_line_width) {
    TRACY_FUNC_COMMANDS_SET_STATE(point_line_width);
    const bool is_front = helper.pop<bool>();
    const uint32_t width = helper.pop<uint32_t>();

    if (is_front)
        render_context->record.point_line_width_front = width;
    else
        render_context->record.point_line_width_back = width;

    if (renderer.current_backend == Backend::D3D12) {
        d3d12::sync_point_line_width(*reinterpret_cast<d3d12::D3D12Context *>(render_context), is_front);
    }
}

COMMAND_SET_STATE(stencil_func) {
    TRACY_FUNC_COMMANDS_SET_STATE(stencil_func);
    const bool is_back = helper.pop<bool>();

    if (is_back) {
        render_context->record.stencil_func_back = helper.pop<SceGxmStencilFunc>();
        render_context->record.stencil_fail_back = helper.pop<SceGxmStencilOp>();
        render_context->record.stencil_depth_fail_back = helper.pop<SceGxmStencilOp>();
        render_context->record.stencil_depth_pass_back = helper.pop<SceGxmStencilOp>();
    } else {
        render_context->record.stencil_func_front = helper.pop<SceGxmStencilFunc>();
        render_context->record.stencil_fail_front = helper.pop<SceGxmStencilOp>();
        render_context->record.stencil_depth_fail_front = helper.pop<SceGxmStencilOp>();
        render_context->record.stencil_depth_pass_front = helper.pop<SceGxmStencilOp>();
    }

    if (renderer.current_backend == Backend::D3D12) {
        d3d12::sync_stencil_func(*reinterpret_cast<d3d12::D3D12Context *>(render_context), is_back);
    }
}

COMMAND_SET_STATE(fragment_texture) {
    TRACY_FUNC_COMMANDS_SET_STATE(fragment_texture);
    const uint32_t index = helper.pop<uint32_t>();
    const SceGxmTexture texture = helper.pop<SceGxmTexture>();

    render_context->record.fragment_textures[index] = texture;

    if (renderer.current_backend == Backend::D3D12) {
        d3d12::sync_texture(*reinterpret_cast<d3d12::D3D12Context *>(render_context), mem, index, texture, config);
    }
}

COMMAND(handle_set_state) {
    TRACY_FUNC_COMMANDS(handle_set_state);
    const GXMState state = helper.pop<GXMState>();

    switch (state) {
    case GXMState::RegionClip:
        cmd_set_state_region_clip(renderer, mem, config, helper, render_context);
        break;
    case GXMState::Program:
        cmd_set_state_program(renderer, mem, config, helper, render_context);
        break;
    case GXMState::Viewport:
        cmd_set_state_viewport(renderer, mem, config, helper, render_context);
        break;
    case GXMState::DepthBias:
        cmd_set_state_depth_bias(renderer, mem, config, helper, render_context);
        break;
    case GXMState::DepthFunc:
        cmd_set_state_depth_func(renderer, mem, config, helper, render_context);
        break;
    case GXMState::DepthWriteEnable:
        cmd_set_state_depth_write_enable(renderer, mem, config, helper, render_context);
        break;
    case GXMState::PolygonMode:
        cmd_set_state_polygon_mode(renderer, mem, config, helper, render_context);
        break;
    case GXMState::PointLineWidth:
        cmd_set_state_point_line_width(renderer, mem, config, helper, render_context);
        break;
    case GXMState::StencilFunc:
        cmd_set_state_stencil_func(renderer, mem, config, helper, render_context);
        break;
    case GXMState::Texture:
        cmd_set_state_fragment_texture(renderer, mem, config, helper, render_context);
        break;
    case GXMState::UniformBuffer:
        cmd_set_state_uniform_buffer(renderer, mem, config, helper, render_context);
        break;
    default:
        break;
    }
}

} // namespace renderer
