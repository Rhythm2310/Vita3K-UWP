// Vita3K emulator project - Xbox Series S UWP D3D12 Port
#include <renderer/d3d12/allocator.h>
#include <util/log.h>

namespace renderer::d3d12 {

bool D3D12Allocator::init(ID3D12Device *device, IDXGIAdapter *adapter) {
    if (!device) return false;

    D3D12MA::ALLOCATOR_DESC desc = {};
    desc.pDevice = device;
    desc.pAdapter = adapter;
    desc.Flags = D3D12MA::ALLOCATOR_FLAG_NONE;

    HRESULT hr = D3D12MA::CreateAllocator(&desc, m_allocator.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        LOG_ERROR("Failed to create D3D12MemoryAllocator: 0x{:08X}", static_cast<uint32_t>(hr));
        return false;
    }
    LOG_INFO("D3D12MemoryAllocator successfully initialized");
    return true;
}

void D3D12Allocator::cleanup() {
    m_allocator.Reset();
}

HRESULT D3D12Allocator::create_buffer(
    size_t size,
    D3D12_HEAP_TYPE heap_type,
    D3D12_RESOURCE_STATES initial_state,
    D3D12MA::Allocation **out_allocation,
    ID3D12Resource **out_resource) {

    if (!m_allocator) return E_POINTER;

    D3D12MA::ALLOCATION_DESC alloc_desc = {};
    alloc_desc.HeapType = heap_type;

    D3D12_RESOURCE_DESC res_desc = {};
    res_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    res_desc.Alignment = 0;
    res_desc.Width = size;
    res_desc.Height = 1;
    res_desc.DepthOrArraySize = 1;
    res_desc.MipLevels = 1;
    res_desc.Format = DXGI_FORMAT_UNKNOWN;
    res_desc.SampleDesc.Count = 1;
    res_desc.SampleDesc.Quality = 0;
    res_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    res_desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    return m_allocator->CreateResource(
        &alloc_desc,
        &res_desc,
        initial_state,
        nullptr,
        out_allocation,
        IID_PPV_ARGS(out_resource));
}

HRESULT D3D12Allocator::create_texture(
    const D3D12_RESOURCE_DESC *desc,
    D3D12_RESOURCE_STATES initial_state,
    D3D12_CLEAR_VALUE *clear_val,
    D3D12MA::Allocation **out_allocation,
    ID3D12Resource **out_resource) {

    if (!m_allocator || !desc) return E_POINTER;

    D3D12MA::ALLOCATION_DESC alloc_desc = {};
    alloc_desc.HeapType = D3D12_HEAP_TYPE_DEFAULT;

    return m_allocator->CreateResource(
        &alloc_desc,
        desc,
        initial_state,
        clear_val,
        out_allocation,
        IID_PPV_ARGS(out_resource));
}

} // namespace renderer::d3d12
