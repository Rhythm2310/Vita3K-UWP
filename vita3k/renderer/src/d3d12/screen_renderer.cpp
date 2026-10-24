// Vita3K emulator project - Xbox Series S UWP D3D12 Port
#include <renderer/d3d12/screen_renderer.h>
#include <util/log.h>

#include <d3dcompiler.h>

namespace renderer::d3d12 {

// HLSL Shaders for fullscreen blit and FidelityFX CAS sharpening
static const char *s_screen_vs = R"(
struct VSOutput {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

VSOutput main(uint id : SV_VertexID) {
    VSOutput output;
    output.uv = float2((id == 2) ? 2.0 : 0.0, (id == 1) ? 2.0 : 0.0);
    output.pos = float4(output.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return output;
}
)";

static const char *s_screen_cas_ps = R"(
Texture2D srcTex : register(t0);
SamplerState samLinear : register(s0);

struct PSInput {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

// FidelityFX CAS (Contrast Adaptive Sharpening) approximation
float4 main(PSInput input) : SV_TARGET {
    float2 texCoord = input.uv;
    
    // Sample center and 4 cross neighbors
    float3 c = srcTex.Sample(samLinear, texCoord).rgb;
    float3 a = srcTex.Sample(samLinear, texCoord + float2(0.0, -1.0 / 544.0)).rgb;
    float3 b = srcTex.Sample(samLinear, texCoord + float2(-1.0 / 960.0, 0.0)).rgb;
    float3 d = srcTex.Sample(samLinear, texCoord + float2(1.0 / 960.0, 0.0)).rgb;
    float3 e = srcTex.Sample(samLinear, texCoord + float2(0.0, 1.0 / 544.0)).rgb;

    // Min and max of neighbors
    float3 minColor = min(c, min(min(a, b), min(d, e)));
    float3 maxColor = max(c, max(max(a, b), max(d, e)));

    // Adaptive sharpening weight
    float sharpness = 0.5; // Configurable sharpening intensity
    float3 w = -saturate(min(minColor, 1.0 - maxColor) / max(maxColor, 0.0001)) * sharpness;
    float3 output = (c + (a + b + d + e) * w) / (1.0 + 4.0 * w);

    return float4(saturate(output), 1.0);
}
)";

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

    // Compile VS and PS
    ComPtr<ID3DBlob> vs_blob;
    ComPtr<ID3DBlob> ps_blob;
    ComPtr<ID3DBlob> error_blob;

    hr = D3DCompile(s_screen_vs, strlen(s_screen_vs), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &vs_blob, &error_blob);
    if (FAILED(hr)) {
        if (error_blob) LOG_ERROR("Screen VS error: {}", (char *)error_blob->GetBufferPointer());
        return false;
    }

    hr = D3DCompile(s_screen_cas_ps, strlen(s_screen_cas_ps), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, &ps_blob, &error_blob);
    if (FAILED(hr)) {
        if (error_blob) LOG_ERROR("Screen PS error: {}", (char *)error_blob->GetBufferPointer());
        return false;
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {};
    pso_desc.pRootSignature = s_screen_root_sig.Get();
    pso_desc.VS = { vs_blob->GetBufferPointer(), vs_blob->GetBufferSize() };
    pso_desc.PS = { ps_blob->GetBufferPointer(), ps_blob->GetBufferSize() };

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

    LOG_INFO("ScreenRenderer initialized with FidelityFX CAS upscaling");
    return true;
}

void ScreenRenderer::render(ID3D12GraphicsCommandList *command_list, ID3D12Resource *src_texture, D3D12_CPU_DESCRIPTOR_HANDLE rtv_target) {
    if (!command_list || !s_screen_pso || !s_screen_root_sig) return;

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
