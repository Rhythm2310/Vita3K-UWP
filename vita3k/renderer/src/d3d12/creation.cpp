// Vita3K emulator project - Xbox Series S UWP D3D12 Port
#include <renderer/d3d12/functions.h>
#include <renderer/d3d12/state.h>
#include <renderer/d3d12/types.h>
#include <renderer/shaders.h>

#include <config/state.h>
#include <mem/mem.h>
#include <util/log.h>

#include <d3dcompiler.h>

namespace renderer::d3d12 {

bool create(std::unique_ptr<renderer::State> &state, const Config &config) {
    auto d3d12_state = std::make_unique<D3D12State>();
    if (!d3d12_state->create(state, config)) {
        LOG_ERROR("Failed to initialize D3D12State");
        return false;
    }
    state = std::move(d3d12_state);
    return true;
}

bool create(D3D12State &state, std::unique_ptr<Context> &context, MemState &mem) {
    auto ctx = std::make_unique<D3D12Context>();
    ctx->mem = &mem;
    
    HRESULT hr = state.device->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        state.frame_resources[state.frame_index].command_allocator.Get(),
        nullptr,
        IID_PPV_ARGS(&ctx->command_list));

    if (FAILED(hr)) {
        LOG_ERROR("Failed to create D3D12 context command list: 0x{:08X}", static_cast<uint32_t>(hr));
        return false;
    }

    ctx->command_list->Close();
    context = std::move(ctx);
    return true;
}

bool create(D3D12State &state, std::unique_ptr<RenderTarget> &rt, const SceGxmRenderTargetParams &params, const FeatureState &features) {
    auto d3d_rt = std::make_unique<D3D12RenderTarget>();
    d3d_rt->width = params.width;
    d3d_rt->height = params.height;

    // Default to RGBA8 format for standard Vita display surfaces
    d3d_rt->color_format = DXGI_FORMAT_R8G8B8A8_UNORM;
    d3d_rt->depth_format = DXGI_FORMAT_D24_UNORM_S8_UINT;

    // Allocate color buffer texture
    D3D12_RESOURCE_DESC color_desc = {};
    color_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    color_desc.Alignment = 0;
    color_desc.Width = params.width;
    color_desc.Height = params.height;
    color_desc.DepthOrArraySize = 1;
    color_desc.MipLevels = 1;
    color_desc.Format = d3d_rt->color_format;
    color_desc.SampleDesc.Count = 1;
    color_desc.SampleDesc.Quality = 0;
    color_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    color_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_CLEAR_VALUE color_clear = {};
    color_clear.Format = d3d_rt->color_format;
    color_clear.Color[0] = 0.0f;
    color_clear.Color[1] = 0.0f;
    color_clear.Color[2] = 0.0f;
    color_clear.Color[3] = 1.0f;

    D3D12MA::Allocation *color_alloc = nullptr;
    HRESULT hr = state.allocator.create_texture(
        &color_desc,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        &color_clear,
        &color_alloc,
        d3d_rt->color_buffer.ReleaseAndGetAddressOf());

    if (FAILED(hr)) {
        LOG_ERROR("Failed to create D3D12 render target color buffer: 0x{:08X}", static_cast<uint32_t>(hr));
        return false;
    }

    // Allocate depth/stencil buffer texture
    D3D12_RESOURCE_DESC depth_desc = color_desc;
    depth_desc.Format = DXGI_FORMAT_R24G8_TYPELESS;
    depth_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE depth_clear = {};
    depth_clear.Format = d3d_rt->depth_format;
    depth_clear.DepthStencil.Depth = 1.0f;
    depth_clear.DepthStencil.Stencil = 0;

    D3D12MA::Allocation *depth_alloc = nullptr;
    hr = state.allocator.create_texture(
        &depth_desc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &depth_clear,
        &depth_alloc,
        d3d_rt->depth_stencil_buffer.ReleaseAndGetAddressOf());

    if (FAILED(hr)) {
        LOG_ERROR("Failed to create D3D12 render target depth buffer: 0x{:08X}", static_cast<uint32_t>(hr));
        return false;
    }

    rt = std::move(d3d_rt);
    return true;
}

void destroy(D3D12State &state, std::unique_ptr<RenderTarget> &rt) {
    if (!rt) return;
    auto *d3d_rt = static_cast<D3D12RenderTarget *>(rt.get());
    d3d_rt->color_buffer.Reset();
    d3d_rt->depth_stencil_buffer.Reset();
    rt.reset();
}

bool create(std::unique_ptr<VertexProgram> &vp, D3D12State &state, const SceGxmProgram &program) {
    auto d3d_vp = std::make_unique<D3D12VertexProgram>();
    d3d_vp->uniform_size = program.size;

    FeatureState features{};
    shader::Hints hints{};
    d3d_vp->dxil_bytecode = load_dxil_shader(
        program,
        features,
        hints,
        false,
        state.shaders_path,
        state.shaders_log_path,
        state.shaders_version,
        true);

    vp = std::move(d3d_vp);
    return true;
}

bool create(std::unique_ptr<FragmentProgram> &fp, D3D12State &state, const SceGxmProgram &program, const SceGxmBlendInfo *blend) {
    auto d3d_fp = std::make_unique<D3D12FragmentProgram>();
    d3d_fp->uniform_size = program.size;

    FeatureState features{};
    shader::Hints hints{};
    d3d_fp->dxil_bytecode = load_dxil_shader(
        program,
        features,
        hints,
        false,
        state.shaders_path,
        state.shaders_log_path,
        state.shaders_version,
        true);

    fp = std::move(d3d_fp);
    return true;
}

} // namespace renderer::d3d12
