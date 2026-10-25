// Vita3K emulator project - Xbox Series S UWP D3D12 Port
#include <renderer/d3d12/screen_renderer.h>
#include "screen_shaders_bytecode.h"
#include <util/log.h>

namespace renderer::d3d12 {

static ComPtr<ID3D12RootSignature> s_screen_root_sig;
static ComPtr<ID3D12PipelineState> s_screen_pso;

bool ScreenRenderer::init(ID3D12Device *device, DXGI_FORMAT target_format) {
    if (!device) return false;

    // Create root signature: 1 descriptor table (t0) and 1 static sampler
    D3D12_DESCRIPTOR_RANGE range = {};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1;
    range.BaseShaderRegister = 0;
    range.RegisterSpace = 0;
    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER root_param = {};
    root_param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_param.DescriptorTable.NumDescriptorRanges = 1;
    root_param.DescriptorTable.pDescriptorRanges = &range;
    root_param.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ShaderRegister = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC sig_desc = {};
    sig_desc.NumParameters = 1;
    sig_desc.pParameters = &root_param;
    sig_desc.NumStaticSamplers = 1;
    sig_desc.pStaticSamplers = &sampler;
    sig_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ComPtr<ID3DBlob> serialized_sig;
    HRESULT hr = D3D12SerializeRootSignature(&sig_desc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized_sig, nullptr);
    if (FAILED(hr)) return false;

    hr = device->CreateRootSignature(0, serialized_sig->GetBufferPointer(), serialized_sig->GetBufferSize(), IID_PPV_ARGS(&s_screen_root_sig));
    if (FAILED(hr)) return false;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {};
    pso_desc.pRootSignature = s_screen_root_sig.Get();
    pso_desc.VS = { g_screen_vs_bytecode, g_screen_vs_bytecode_size };
    pso_desc.PS = { g_screen_cas_ps_bytecode, g_screen_cas_ps_bytecode_size };

    D3D12_BLEND_DESC blend_desc = {};
    blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pso_desc.BlendState = blend_desc;
    pso_desc.SampleMask = UINT_MAX;

    D3D12_RASTERIZER_DESC rast_desc = {};
    rast_desc.FillMode = D3D12_FILL_MODE_SOLID;
    rast_desc.CullMode = D3D12_CULL_MODE_NONE;
    pso_desc.RasterizerState = rast_desc;

    pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso_desc.NumRenderTargets = 1;
    pso_desc.RTVFormats[0] = target_format;
    pso_desc.SampleDesc.Count = 1;

    hr = device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&s_screen_pso));
    if (FAILED(hr)) {
        LOG_ERROR("Failed to create screen PSO: 0x{:08X}", static_cast<uint32_t>(hr));
        return false;
    }

    LOG_INFO("ScreenRenderer initialized with FidelityFX CAS upscaling (Precompiled DXBC Bytecode)");
    return true;
}

void ScreenRenderer::render(ID3D12GraphicsCommandList *command_list, ID3D12Resource *src_texture, D3D12_CPU_DESCRIPTOR_HANDLE rtv_target) {
    if (!command_list || !src_texture || !s_screen_pso || !s_screen_root_sig) return;

    command_list->SetGraphicsRootSignature(s_screen_root_sig.Get());
    command_list->SetPipelineState(s_screen_pso.Get());
    command_list->OMSetRenderTargets(1, &rtv_target, FALSE, nullptr);
    command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Render fullscreen triangle
    command_list->DrawInstanced(3, 1, 0, 0);
}

void ScreenRenderer::cleanup() {
    s_screen_pso.Reset();
    s_screen_root_sig.Reset();
}

} // namespace renderer::d3d12
