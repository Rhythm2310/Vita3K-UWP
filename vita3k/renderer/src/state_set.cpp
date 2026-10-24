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
    if (!flat) {
        float factor = 1.0f;
        const bool has_msaa = (render_context->current_render_target && render_context->current_render_target->multisample_mode);
        const bool has_downscale = render_context->record.color_surface.downscale;
        if (has_msaa && !has_downscale)
            factor = 2.0f;
        else if (!has_msaa && has_downscale)
            factor = 1.0f;

        const float xOffset = helper.pop<float>() * factor;
        const float yOffset = helper.pop<float>() * factor;
        const float zOffset = helper.pop<float>();
        const float xScale = helper.pop<float>() * factor;
        const float yScale = helper.pop<float>() * factor;
        const float zScale = helper.pop<float>();

        const float ymin = yOffset + yScale;
        const float ymax = yOffset - yScale - 1;

        render_context->record.viewport_flip[0] = 1.0f;
        render_context->record.viewport_flip[1] = (ymin < ymax) ? -1.0f : 1.0f;
        render_context->record.viewport_flip[2] = 1.0f;
        render_context->record.viewport_flip[3] = 1.0f;
        render_context->record.z_offset = zOffset;
        render_context->record.z_scale = zScale;

        if (renderer.current_backend == Backend::D3D12) {
            d3d12::sync_viewport_real(*reinterpret_cast<d3d12::D3D12Context *>(render_context), xOffset, yOffset, zOffset, xScale, yScale, zScale);
        }
    } else {
        render_context->record.viewport_flip[0] = 1.0f;
        render_context->record.viewport_flip[1] = -1.0f;
        render_context->record.viewport_flip[2] = 1.0f;
        render_context->record.viewport_flip[3] = 1.0f;
        render_context->record.z_offset = 0.0f;
        render_context->record.z_scale = 1.0f;

        if (renderer.current_backend == Backend::D3D12) {
            d3d12::sync_viewport_flat(*reinterpret_cast<d3d12::D3D12Context *>(render_context));
        }
    }

    if (previous_flip_y != render_context->record.viewport_flip[1]) {
        if (renderer.current_backend == Backend::D3D12) {
            d3d12::sync_clipping(*reinterpret_cast<d3d12::D3D12Context *>(render_context));
        }
    }
}

COMMAND_SET_STATE(depth_bias) {
    TRACY_FUNC_COMMANDS_SET_STATE(depth_bias);
    const bool is_front = helper.pop<bool>();
    const int factor = helper.pop<int>();
    const int unit = helper.pop<int>();

    render_context->record.depth_bias_unit = unit;
    render_context->record.depth_bias_slope = factor;

    if (renderer.current_backend == Backend::D3D12) {
        if (is_front)
            d3d12::sync_depth_bias(*reinterpret_cast<d3d12::D3D12Context *>(render_context));
    }
}

COMMAND_SET_STATE(depth_func) {
    TRACY_FUNC_COMMANDS_SET_STATE(depth_func);
    const bool is_front = helper.pop<bool>();
    const SceGxmDepthFunc depth_func = helper.pop<SceGxmDepthFunc>();

    if (is_front) {
        render_context->record.front_depth_func = depth_func;
    } else {
        render_context->record.back_depth_func = depth_func;
    }

    if (renderer.current_backend == Backend::D3D12) {
        d3d12::sync_depth_data(*reinterpret_cast<d3d12::D3D12Context *>(render_context));
    }
}

COMMAND_SET_STATE(depth_write_enable) {
    TRACY_FUNC_COMMANDS_SET_STATE(depth_write_enable);
    const bool is_front = helper.pop<bool>();
    const SceGxmDepthWriteMode mode = helper.pop<SceGxmDepthWriteMode>();

    if (is_front)
        render_context->record.front_depth_write_mode = mode;
    else
        render_context->record.back_depth_write_mode = mode;

    if (renderer.current_backend == Backend::D3D12) {
        d3d12::sync_depth_data(*reinterpret_cast<d3d12::D3D12Context *>(render_context));
    }
}

COMMAND_SET_STATE(polygon_mode) {
    TRACY_FUNC_COMMANDS_SET_STATE(polygon_mode);
    const bool is_front = helper.pop<bool>();
    const SceGxmPolygonMode mode = helper.pop<SceGxmPolygonMode>();

    if (is_front)
        render_context->record.front_polygon_mode = mode;
    else
        render_context->record.back_polygon_mode = mode;

    if (renderer.current_backend == Backend::D3D12) {
        d3d12::refresh_pipeline(*reinterpret_cast<d3d12::D3D12Context *>(render_context));
    }
}

COMMAND_SET_STATE(point_line_width) {
    TRACY_FUNC_COMMANDS_SET_STATE(point_line_width);
    const bool is_front = helper.pop<bool>();
    const uint32_t width = helper.pop<uint32_t>();

    if (is_front)
        render_context->record.line_width = width;

    if (renderer.current_backend == Backend::D3D12) {
        d3d12::sync_point_line_width(*reinterpret_cast<d3d12::D3D12Context *>(render_context), is_front);
    }
}

COMMAND_SET_STATE(stencil_func) {
    TRACY_FUNC_COMMANDS_SET_STATE(stencil_func);
    const bool is_front = helper.pop<bool>();
    GxmStencilStateOp &stencil_state_op = is_front ? render_context->record.front_stencil_state_op : render_context->record.back_stencil_state_op;
    GxmStencilStateValues &stencil_state_vals = is_front ? render_context->record.front_stencil_state_values : render_context->record.back_stencil_state_values;

    stencil_state_op.func = helper.pop<SceGxmStencilFunc>();
    stencil_state_op.stencil_fail = helper.pop<SceGxmStencilOp>();
    stencil_state_op.depth_fail = helper.pop<SceGxmStencilOp>();
    stencil_state_op.depth_pass = helper.pop<SceGxmStencilOp>();
    stencil_state_vals.compare_mask = helper.pop<std::uint8_t>();
    stencil_state_vals.write_mask = helper.pop<std::uint8_t>();

    if (render_context->record.is_maskupdate) {
        if (stencil_state_op.func == SCE_GXM_STENCIL_FUNC_NEVER) {
            render_context->record.writing_mask = 0.0f;
        } else if (stencil_state_op.func == SCE_GXM_STENCIL_FUNC_ALWAYS) {
            render_context->record.writing_mask = 1.0f;
        }
        return;
    }

    if (renderer.current_backend == Backend::D3D12) {
        d3d12::refresh_pipeline(*reinterpret_cast<d3d12::D3D12Context *>(render_context));
        d3d12::sync_stencil_func(*reinterpret_cast<d3d12::D3D12Context *>(render_context), !is_front);
    }
}

COMMAND_SET_STATE(stencil_ref) {
    TRACY_FUNC_COMMANDS_SET_STATE(stencil_ref);
    const bool is_front = helper.pop<bool>();
    const uint8_t sref = helper.pop<const unsigned char>();

    GxmStencilStateValues &stencil_state_vals = is_front ? render_context->record.front_stencil_state_values : render_context->record.back_stencil_state_values;
    stencil_state_vals.ref = sref;

    if (renderer.current_backend == Backend::D3D12) {
        d3d12::sync_stencil_func(*reinterpret_cast<d3d12::D3D12Context *>(render_context), !is_front);
    }
}

COMMAND_SET_STATE(texture) {
    TRACY_FUNC_COMMANDS_SET_STATE(texture);
    const uint32_t index = helper.pop<uint32_t>();
    const SceGxmTexture texture = helper.pop<SceGxmTexture>();

    if (renderer.current_backend == Backend::D3D12) {
        d3d12::sync_texture(*reinterpret_cast<d3d12::D3D12Context *>(render_context), mem, index, texture, config);
    }
}

COMMAND_SET_STATE(two_sided) {
    TRACY_FUNC_COMMANDS_SET_STATE(two_sided);
    const SceGxmTwoSidedMode two_sided = helper.pop<SceGxmTwoSidedMode>();
    render_context->record.two_sided = two_sided;

    if (renderer.current_backend == Backend::D3D12) {
        d3d12::refresh_pipeline(*reinterpret_cast<d3d12::D3D12Context *>(render_context));
        d3d12::sync_stencil_func(*reinterpret_cast<d3d12::D3D12Context *>(render_context), false);
        d3d12::sync_stencil_func(*reinterpret_cast<d3d12::D3D12Context *>(render_context), true);
    }
}

COMMAND_SET_STATE(cull_mode) {
    TRACY_FUNC_COMMANDS_SET_STATE(cull_mode);
    render_context->record.cull_mode = helper.pop<SceGxmCullMode>();

    if (renderer.current_backend == Backend::D3D12) {
        d3d12::refresh_pipeline(*reinterpret_cast<d3d12::D3D12Context *>(render_context));
    }
}

COMMAND_SET_STATE(vertex_stream) {
    TRACY_FUNC_COMMANDS_SET_STATE(vertex_stream);
    const Ptr<const uint8_t> stream_data = helper.pop<Ptr<const uint8_t>>();
    const std::size_t stream_index = helper.pop<std::size_t>();
    const std::size_t stream_data_length = helper.pop<std::size_t>();

    renderer::GXMStreamInfo &info = render_context->record.vertex_streams[stream_index];
    info.data = stream_data;
    info.size = stream_data_length;
}

COMMAND_SET_STATE(fragment_program_enable) {
    TRACY_FUNC_COMMANDS_SET_STATE(fragment_program_enable);
    const bool is_front = helper.pop<bool>();
    const SceGxmFragmentProgramMode mode = helper.pop<SceGxmFragmentProgramMode>();

    if (is_front)
        render_context->record.front_side_fragment_program_mode = mode;
    else
        render_context->record.back_side_fragment_program_mode = mode;

    if (renderer.current_backend == Backend::D3D12) {
        d3d12::refresh_pipeline(*reinterpret_cast<d3d12::D3D12Context *>(render_context));
    }
}

COMMAND_SET_STATE(visibility_buffer) {
    TRACY_FUNC_COMMANDS_SET_STATE(visibility_buffer);
    const Ptr<uint32_t> buffer = helper.pop<Ptr<uint32_t>>();
    const uint32_t stride = helper.pop<uint32_t>();

    if (renderer.current_backend == Backend::D3D12) {
        d3d12::sync_visibility_buffer(*reinterpret_cast<d3d12::D3D12Context *>(render_context), buffer, stride);
    }
}

COMMAND_SET_STATE(visibility_index) {
    TRACY_FUNC_COMMANDS_SET_STATE(visibility_index);
    const uint32_t index = helper.pop<uint32_t>();
    const bool enable = helper.pop<bool>();
    const bool is_increment = helper.pop<bool>();

    if (renderer.current_backend == Backend::D3D12) {
        d3d12::sync_visibility_index(*reinterpret_cast<d3d12::D3D12Context *>(render_context), enable, index, is_increment);
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
    case GXMState::StencilRef:
        cmd_set_state_stencil_ref(renderer, mem, config, helper, render_context);
        break;
    case GXMState::Texture:
        cmd_set_state_texture(renderer, mem, config, helper, render_context);
        break;
    case GXMState::TwoSided:
        cmd_set_state_two_sided(renderer, mem, config, helper, render_context);
        break;
    case GXMState::CullMode:
        cmd_set_state_cull_mode(renderer, mem, config, helper, render_context);
        break;
    case GXMState::VertexStream:
        cmd_set_state_vertex_stream(renderer, mem, config, helper, render_context);
        break;
    case GXMState::UniformBuffer:
        cmd_set_state_uniform_buffer(renderer, mem, config, helper, render_context);
        break;
    case GXMState::FragmentProgramEnable:
        cmd_set_state_fragment_program_enable(renderer, mem, config, helper, render_context);
        break;
    case GXMState::VisibilityBuffer:
        cmd_set_state_visibility_buffer(renderer, mem, config, helper, render_context);
        break;
    case GXMState::VisibilityIndex:
        cmd_set_state_visibility_index(renderer, mem, config, helper, render_context);
        break;
    default:
        break;
    }
}

} // namespace renderer
