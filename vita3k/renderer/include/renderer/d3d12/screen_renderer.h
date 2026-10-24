// Vita3K emulator project - Xbox Series S UWP D3D12 Port
#pragma once

#include <renderer/d3d12/types.h>

namespace renderer::d3d12 {

class OverlayRenderer {
public:
    OverlayRenderer() = default;
    ~OverlayRenderer() = default;

    bool init(ID3D12Device *device, ID3D12CommandQueue *command_queue);
    void render(ID3D12GraphicsCommandList *command_list);
    void cleanup();
};

class ScreenRenderer {
public:
    ScreenRenderer() = default;
    ~ScreenRenderer() = default;

    bool init(ID3D12Device *device, DXGI_FORMAT target_format);
    void render(ID3D12GraphicsCommandList *command_list, ID3D12Resource *src_texture, D3D12_CPU_DESCRIPTOR_HANDLE rtv_target);
    void cleanup();
};

} // namespace renderer::d3d12
