// Vita3K emulator project - Xbox Series S UWP D3D12 Port
#include <gxm/types.h>
#include <renderer/commands.h>
#include <renderer/driver_functions.h>
#include <renderer/state.h>
#include <renderer/types.h>

#include <renderer/d3d12/functions.h>
#include <renderer/d3d12/state.h>

#include <gxm/functions.h>
#include <renderer/functions.h>
#include <util/align.h>
#include <util/log.h>
#include <util/tracy.h>

namespace renderer {

static void layout_ssbo_offset_from_uniform_buffer_sizes(UniformBufferSizes &sizes, UniformBufferSizes &offsets, std::size_t &total_hold) {
    std::uint32_t last_offset = 0;

    for (std::size_t i = 0; i < sizes.size(); i++) {
        if (sizes[i] != 0) {
            offsets[i] = last_offset;
            last_offset += align(sizes[i], 4);
        } else {
            offsets[i] = static_cast<std::uint32_t>(-1);
        }
    }

    total_hold = static_cast<std::size_t>(last_offset);
}

COMMAND(handle_create_context) {
    TRACY_FUNC_COMMANDS(handle_create_context);
    std::unique_ptr<Context> *ctx = helper.pop<std::unique_ptr<Context> *>();
    bool result = false;

    if (renderer.current_backend == Backend::D3D12) {
        result = d3d12::create(dynamic_cast<d3d12::D3D12State &>(renderer), *ctx, mem);
    } else {
        REPORT_MISSING(renderer.current_backend);
    }

    renderer.context = ctx->get();

    renderer.context->shader_hints = {
        .attributes = nullptr,
        .color_format = SCE_GXM_COLOR_FORMAT_U8U8U8U8_ABGR
    };
    std::fill_n(renderer.context->shader_hints.vertex_textures, SCE_GXM_MAX_TEXTURE_UNITS, SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR);
    std::fill_n(renderer.context->shader_hints.fragment_textures, SCE_GXM_MAX_TEXTURE_UNITS, SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR);

    complete_command(renderer, helper, result);
}

COMMAND(handle_destroy_context) {
    TRACY_FUNC_COMMANDS(handle_destroy_context);
    std::unique_ptr<Context> *ctx = helper.pop<std::unique_ptr<Context> *>();

    if (renderer.context == ctx->get())
        renderer.context = nullptr;
    ctx->reset();

    complete_command(renderer, helper, 0);
}

COMMAND(handle_create_render_target) {
    TRACY_FUNC_COMMANDS(handle_create_render_target);
    std::unique_ptr<RenderTarget> *render_target = helper.pop<std::unique_ptr<RenderTarget> *>();
    SceGxmRenderTargetParams *params = helper.pop<SceGxmRenderTargetParams *>();

    bool result = false;

    if (renderer.current_backend == Backend::D3D12) {
        result = d3d12::create(dynamic_cast<d3d12::D3D12State &>(renderer), *render_target, *params, features);
    } else {
        REPORT_MISSING(renderer.current_backend);
    }
    (*render_target)->multisample_mode = params->multisampleMode;
    (*render_target)->has_macroblock_sync = (params->flags & SCE_GXM_RENDER_TARGET_MACROTILE_SYNC);
    if ((*render_target)->has_macroblock_sync) {
        uint16_t nb_macroblocks_x = (params->flags >> 8) & 0b111;
        uint16_t nb_macroblocks_y = (params->flags >> 12) & 0b111;

        (*render_target)->macroblock_width = static_cast<uint16_t>((params->width / nb_macroblocks_x) * renderer.res_multiplier);
        (*render_target)->macroblock_height = static_cast<uint16_t>((params->height / nb_macroblocks_y) * renderer.res_multiplier);
    }

    complete_command(renderer, helper, result);
}

COMMAND(handle_destroy_render_target) {
    TRACY_FUNC_COMMANDS(handle_destroy_render_target);
    std::unique_ptr<RenderTarget> *render_target = helper.pop<std::unique_ptr<RenderTarget> *>();

    if (renderer.current_backend == Backend::D3D12) {
        d3d12::destroy(dynamic_cast<d3d12::D3D12State &>(renderer), *render_target);
    } else {
        REPORT_MISSING(renderer.current_backend);
    }

    render_target->reset();
    complete_command(renderer, helper, 0);
}

COMMAND(handle_memory_map) {
    TRACY_FUNC_COMMANDS(handle_memory_map);
    const Ptr<void> addr = helper.pop<Ptr<void>>();
    const uint32_t size = helper.pop<uint32_t>();

    if (renderer.current_backend == Backend::D3D12) {
        dynamic_cast<d3d12::D3D12State &>(renderer).map_memory(mem, addr, size);
    }

    complete_command(renderer, helper, 0);
}

COMMAND(handle_memory_unmap) {
    TRACY_FUNC_COMMANDS(handle_memory_unmap);
    const Ptr<void> addr = helper.pop<Ptr<void>>();

    if (renderer.current_backend == Backend::D3D12) {
        dynamic_cast<d3d12::D3D12State &>(renderer).unmap_memory(mem, addr);
    }

    complete_command(renderer, helper, 0);
}

bool create(std::unique_ptr<FragmentProgram> &fp, State &state, const SceGxmProgram &program, const SceGxmBlendInfo *blend, GXPPtrMap &gxp_ptr_map) {
    if (state.current_backend == Backend::D3D12) {
        d3d12::create(fp, dynamic_cast<d3d12::D3D12State &>(state), program, blend);
    } else {
        REPORT_MISSING(state.current_backend);
        return false;
    }

    fp->hash = sha256(&program, program.size);
    gxp_ptr_map.emplace(fp->hash, &program);

    fp->buffer_count = shader::usse::get_uniform_buffer_sizes(program, fp->uniform_buffer_sizes);
    layout_ssbo_offset_from_uniform_buffer_sizes(fp->uniform_buffer_sizes, fp->uniform_buffer_data_offsets, fp->max_total_uniform_buffer_storage);
    fp->textures_used = gxp::get_textures_used(program);
    fp->texture_count = std::bit_width(fp->textures_used.to_ulong());

    return true;
}

bool create(std::unique_ptr<VertexProgram> &vp, State &state, const SceGxmProgram &program, GXPPtrMap &gxp_ptr_map, const std::vector<SceGxmVertexAttribute> &attributes) {
    if (state.current_backend == Backend::D3D12) {
        d3d12::create(vp, dynamic_cast<d3d12::D3D12State &>(state), program);
    } else {
        REPORT_MISSING(state.current_backend);
        return false;
    }

    vp->hash = sha256(&program, program.size);
    gxp_ptr_map.emplace(vp->hash, &program);

    vp->buffer_count = shader::usse::get_uniform_buffer_sizes(program, vp->uniform_buffer_sizes);
    shader::usse::get_attribute_informations(program, vp->attribute_infos);
    layout_ssbo_offset_from_uniform_buffer_sizes(vp->uniform_buffer_sizes, vp->uniform_buffer_data_offsets, vp->max_total_uniform_buffer_storage);
    vp->textures_used = gxp::get_textures_used(program);
    vp->texture_count = std::bit_width(vp->textures_used.to_ulong());

    if (vp->attribute_infos.empty()) {
        if (program.primary_reg_count != 0) {
            for (size_t i = 0; i < attributes.size(); i++) {
                vp->attribute_infos.emplace(attributes[i].regIndex, shader::usse::AttributeInformation(static_cast<uint16_t>(i), SCE_GXM_PARAMETER_TYPE_F32, 1, false, false, false));
            }
        }
    }

    return true;
}

void create(SceGxmSyncObject *sync, State &state) {
    sync->last_display = 0;
    sync->timestamp_current = 0;
    sync->timestamp_ahead = 0;
    sync->being_deleted = false;
}

void destroy(SceGxmSyncObject *sync, State &state) {
}

bool init(FrameHost &frame, std::unique_ptr<State> &state, Backend backend, const Config &config, const Root &root_paths) {
    state = std::make_unique<d3d12::D3D12State>();
    state->frame = &frame;
    state->init_paths(root_paths);
    if (!d3d12::create(state, config))
        return false;

    state->current_backend = Backend::D3D12;
    state->command_buffer_queue.maxPendingCount_ = 30;

    return true;
}
} // namespace renderer
