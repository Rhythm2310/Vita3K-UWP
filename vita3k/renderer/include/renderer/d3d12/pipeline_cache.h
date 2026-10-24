// Vita3K emulator project - Xbox Series S UWP D3D12 Port
#pragma once

#include <renderer/d3d12/types.h>
#include <map>
#include <mutex>
#include <string>

namespace renderer::d3d12 {

struct PipelineKey {
    const D3D12VertexProgram *vertex_program = nullptr;
    const D3D12FragmentProgram *fragment_program = nullptr;
    DXGI_FORMAT rtv_format = DXGI_FORMAT_R8G8B8A8_UNORM;
    DXGI_FORMAT dsv_format = DXGI_FORMAT_UNKNOWN;
    uint32_t blend_state_hash = 0;
    uint32_t depth_state_hash = 0;
    uint32_t rasterizer_state_hash = 0;

    bool operator<(const PipelineKey &other) const {
        return memcmp(this, &other, sizeof(PipelineKey)) < 0;
    }
};

class PipelineCache {
public:
    PipelineCache() = default;
    ~PipelineCache() = default;

    ComPtr<ID3D12PipelineState> get_or_create_pipeline(
        ID3D12Device *device,
        ID3D12RootSignature *root_signature,
        const PipelineKey &key);

    void clear();

private:
    std::mutex m_mutex;
    std::map<PipelineKey, ComPtr<ID3D12PipelineState>> m_pipelines;
};

} // namespace renderer::d3d12
