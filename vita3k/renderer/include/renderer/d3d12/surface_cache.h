// Vita3K emulator project - Xbox Series S UWP D3D12 Port
#pragma once

#include <renderer/d3d12/types.h>
#include <renderer/texture_cache.h>
#include <map>
#include <memory>

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

    void select(size_t index, const SceGxmTexture &texture) override {}
    void configure_texture(const SceGxmTexture &texture) override {}
    void upload_texture_impl(SceGxmTextureBaseFormat base_format, uint32_t width, uint32_t height, uint32_t mip_index, const void *pixels, int face, uint32_t pixels_per_stride) override {}
    void import_configure_impl(SceGxmTextureBaseFormat base_format, uint32_t width, uint32_t height, bool is_srgb, uint16_t nb_components, uint16_t mipcount, bool swap_rb) override {}
};

} // namespace renderer::d3d12
