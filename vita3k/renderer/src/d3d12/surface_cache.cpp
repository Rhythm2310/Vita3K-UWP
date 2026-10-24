// Vita3K emulator project - Xbox Series S UWP D3D12 Port
#include <renderer/d3d12/surface_cache.h>
#include <util/log.h>

namespace renderer::d3d12 {

D3D12Texture *D3D12SurfaceCache::get_or_create_surface(
    ID3D12Device *device,
    uint32_t width,
    uint32_t height,
    DXGI_FORMAT format) {

    uint64_t key = (static_cast<uint64_t>(width) << 32) |
                   (static_cast<uint64_t>(height) << 16) |
                   static_cast<uint64_t>(format);

    auto it = m_surfaces.find(key);
    if (it != m_surfaces.end()) {
        return it->second.get();
    }

    auto tex = std::make_unique<D3D12Texture>();
    tex->width = width;
    tex->height = height;
    tex->format = format;

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Alignment = 0;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_HEAP_PROPERTIES heap_props = {};
    heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;

    HRESULT hr = device->CreateCommittedResource(
        &heap_props,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        nullptr,
        IID_PPV_ARGS(&tex->resource));

    if (FAILED(hr)) {
        LOG_ERROR("Failed to allocate surface texture {}x{} format {}: 0x{:08X}", width, height, static_cast<int>(format), static_cast<uint32_t>(hr));
        return nullptr;
    }

    auto *ptr = tex.get();
    m_surfaces[key] = std::move(tex);
    return ptr;
}

void D3D12SurfaceCache::clear() {
    m_surfaces.clear();
}

} // namespace renderer::d3d12
