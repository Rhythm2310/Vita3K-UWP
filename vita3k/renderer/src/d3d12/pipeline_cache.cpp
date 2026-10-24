// Vita3K emulator project - Xbox Series S UWP D3D12 Port
#include <renderer/d3d12/pipeline_cache.h>
#include <util/log.h>

namespace renderer::d3d12 {

ComPtr<ID3D12PipelineState> PipelineCache::get_or_create_pipeline(
    ID3D12Device *device,
    ID3D12RootSignature *root_signature,
    const PipelineKey &key) {

    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_pipelines.find(key);
    if (it != m_pipelines.end()) {
        return it->second;
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {};
    pso_desc.pRootSignature = root_signature;

    if (key.vertex_program && !key.vertex_program->dxil_bytecode.empty()) {
        pso_desc.VS = {
            key.vertex_program->dxil_bytecode.data(),
            key.vertex_program->dxil_bytecode.size()
        };
    }

    if (key.fragment_program && !key.fragment_program->dxil_bytecode.empty()) {
        pso_desc.PS = {
            key.fragment_program->dxil_bytecode.data(),
            key.fragment_program->dxil_bytecode.size()
        };
    }

    // Standard blend state
    D3D12_BLEND_DESC blend_desc = {};
    blend_desc.AlphaToCoverageEnable = FALSE;
    blend_desc.IndependentBlendEnable = FALSE;
    blend_desc.RenderTarget[0].BlendEnable = TRUE;
    blend_desc.RenderTarget[0].LogicOpEnable = FALSE;
    blend_desc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blend_desc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    blend_desc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blend_desc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    blend_desc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    blend_desc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend_desc.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
    blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pso_desc.BlendState = blend_desc;

    pso_desc.SampleMask = UINT_MAX;

    // Rasterizer state
    D3D12_RASTERIZER_DESC rast_desc = {};
    rast_desc.FillMode = D3D12_FILL_MODE_SOLID;
    rast_desc.CullMode = D3D12_CULL_MODE_NONE;
    rast_desc.FrontCounterClockwise = FALSE;
    rast_desc.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    rast_desc.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    rast_desc.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    rast_desc.DepthClipEnable = TRUE;
    rast_desc.MultisampleEnable = FALSE;
    rast_desc.AntialiasedLineEnable = FALSE;
    rast_desc.ForcedSampleCount = 0;
    rast_desc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
    pso_desc.RasterizerState = rast_desc;

    // Depth/Stencil state
    D3D12_DEPTH_STENCIL_DESC ds_desc = {};
    ds_desc.DepthEnable = (key.dsv_format != DXGI_FORMAT_UNKNOWN) ? TRUE : FALSE;
    ds_desc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    ds_desc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    ds_desc.StencilEnable = FALSE;
    pso_desc.DepthStencilState = ds_desc;

    pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso_desc.NumRenderTargets = 1;
    pso_desc.RTVFormats[0] = key.rtv_format;
    pso_desc.DSVFormat = key.dsv_format;
    pso_desc.SampleDesc.Count = 1;
    pso_desc.SampleDesc.Quality = 0;

    ComPtr<ID3D12PipelineState> pso;
    HRESULT hr = device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&pso));
    if (FAILED(hr)) {
        LOG_ERROR("Failed to create graphics pipeline state: 0x{:08X}", static_cast<uint32_t>(hr));
        return nullptr;
    }

    m_pipelines[key] = pso;
    return pso;
}

void PipelineCache::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_pipelines.clear();
}

} // namespace renderer::d3d12
