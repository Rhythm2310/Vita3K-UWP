// Vita3K emulator project - Xbox Series S UWP D3D12 Port
#pragma once

#include <renderer/d3d12/types.h>
#include <shared_mutex>
#include <string>
#include <unordered_map>

namespace renderer::d3d12 {

struct PipelineKey {
    const D3D12VertexProgram *vertex_program = nullptr;
    const D3D12FragmentProgram *fragment_program = nullptr;
    DXGI_FORMAT rtv_format = DXGI_FORMAT_R8G8B8A8_UNORM;
    DXGI_FORMAT dsv_format = DXGI_FORMAT_UNKNOWN;
    uint32_t blend_state_hash = 0;
    uint32_t depth_state_hash = 0;
    uint32_t rasterizer_state_hash = 0;

    bool operator==(const PipelineKey &other) const noexcept {
        return vertex_program == other.vertex_program &&
               fragment_program == other.fragment_program &&
               rtv_format == other.rtv_format &&
               dsv_format == other.dsv_format &&
               blend_state_hash == other.blend_state_hash &&
               depth_state_hash == other.depth_state_hash &&
               rasterizer_state_hash == other.rasterizer_state_hash;
    }
};

struct PipelineKeyHash {
    size_t operator()(const PipelineKey &k) const noexcept {
        size_t h = 14695981039346656037ULL;
        auto hash_combine = [&h](size_t val) noexcept {
            h ^= val;
            h *= 1099511628211ULL;
        };
        hash_combine(reinterpret_cast<size_t>(k.vertex_program));
        hash_combine(reinterpret_cast<size_t>(k.fragment_program));
        hash_combine(static_cast<size_t>(k.rtv_format));
        hash_combine(static_cast<size_t>(k.dsv_format));
        hash_combine(static_cast<size_t>(k.blend_state_hash));
        hash_combine(static_cast<size_t>(k.depth_state_hash));
        hash_combine(static_cast<size_t>(k.rasterizer_state_hash));
        return h;
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
    std::shared_mutex m_mutex;
    std::unordered_map<PipelineKey, ComPtr<ID3D12PipelineState>, PipelineKeyHash> m_pipelines;
};

} // namespace renderer::d3d12
