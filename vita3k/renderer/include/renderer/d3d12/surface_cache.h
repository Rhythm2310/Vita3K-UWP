// Vita3K emulator project - Xbox Series S UWP D3D12 Port
#pragma once

#include <renderer/d3d12/types.h>
#include <map>

namespace renderer::d3d12 {

class D3D12SurfaceCache {
public:
    D3D12SurfaceCache() = default;
    ~D3D12SurfaceCache() = default;

    D3D12Texture *get_or_create_surface(ID3D12Device *device, uint32_t width, uint32_t height, DXGI_FORMAT format);
    void clear();

private:
    std::map<uint64_t, std::unique_ptr<D3D12Texture>> m_surfaces;
};

class D3D12TextureCache : public TextureCache {
public:
    D3D12TextureCache() = default;
    ~D3D12TextureCache() override = default;
};

} // namespace renderer::d3d12
