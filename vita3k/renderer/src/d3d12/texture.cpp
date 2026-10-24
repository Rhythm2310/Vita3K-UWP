// Vita3K emulator project - Xbox Series S UWP D3D12 Port
#include <renderer/d3d12/functions.h>
#include <renderer/d3d12/state.h>
#include <renderer/d3d12/types.h>

#include <util/log.h>

namespace renderer::d3d12 {

// Helper to create and upload texture data to D3D12 resource
bool upload_texture_data(
    ID3D12Device *device,
    ID3D12GraphicsCommandList *command_list,
    ID3D12Resource *destination_resource,
    ID3D12Resource *intermediate_resource,
    const void *data,
    size_t size) {

    if (!device || !command_list || !destination_resource || !intermediate_resource || !data) {
        return false;
    }

    void *mapped_data = nullptr;
    D3D12_RANGE read_range = { 0, 0 };
    HRESULT hr = intermediate_resource->Map(0, &read_range, &mapped_data);
    if (FAILED(hr)) {
        LOG_ERROR("Failed to map intermediate upload buffer: 0x{:08X}", static_cast<uint32_t>(hr));
        return false;
    }

    memcpy(mapped_data, data, size);
    intermediate_resource->Unmap(0, nullptr);

    // Transition destination resource to COPY_DEST
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = destination_resource;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    command_list->ResourceBarrier(1, &barrier);

    D3D12_TEXTURE_COPY_LOCATION src_location = {};
    src_location.pResource = intermediate_resource;
    src_location.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;

    D3D12_TEXTURE_COPY_LOCATION dst_location = {};
    dst_location.pResource = destination_resource;
    dst_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst_location.SubresourceIndex = 0;

    command_list->CopyTextureRegion(&dst_location, 0, 0, 0, &src_location, nullptr);

    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    command_list->ResourceBarrier(1, &barrier);

    return true;
}

} // namespace renderer::d3d12
