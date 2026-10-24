// Vita3K emulator project - Xbox Series S UWP D3D12 Port
#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <D3D12MemAlloc.h>

using Microsoft::WRL::ComPtr;

namespace renderer::d3d12 {

class D3D12Allocator {
public:
    D3D12Allocator() = default;
    ~D3D12Allocator() { cleanup(); }

    bool init(ID3D12Device *device, IDXGIAdapter *adapter);
    void cleanup();

    HRESULT create_buffer(
        size_t size,
        D3D12_HEAP_TYPE heap_type,
        D3D12_RESOURCE_STATES initial_state,
        D3D12MA::Allocation **out_allocation,
        ID3D12Resource **out_resource);

    HRESULT create_texture(
        const D3D12_RESOURCE_DESC *desc,
        D3D12_RESOURCE_STATES initial_state,
        D3D12_CLEAR_VALUE *clear_val,
        D3D12MA::Allocation **out_allocation,
        ID3D12Resource **out_resource);

    D3D12MA::Allocator *get_allocator() const { return m_allocator.Get(); }

private:
    ComPtr<D3D12MA::Allocator> m_allocator;
};

} // namespace renderer::d3d12
