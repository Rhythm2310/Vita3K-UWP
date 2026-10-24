// Vita3K emulator project - Xbox Series S UWP D3D12 Port
#pragma once

#include <renderer/types.h>
#include <gxm/types.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <array>
#include <memory>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace renderer::d3d12 {

constexpr uint32_t FRAME_COUNT = 3;

struct FrameResource {
    ComPtr<ID3D12CommandAllocator> command_allocator;
    uint64_t fence_value = 0;
};

struct D3D12Texture {
    ComPtr<ID3D12Resource> resource;
    D3D12_CPU_DESCRIPTOR_HANDLE srv_handle{};
    D3D12_RESOURCE_STATES current_state = D3D12_RESOURCE_STATE_COMMON;
    uint32_t width = 0;
    uint32_t height = 0;
    DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
};

struct D3D12Buffer {
    ComPtr<ID3D12Resource> resource;
    D3D12_GPU_VIRTUAL_ADDRESS gpu_address = 0;
    void *cpu_mapped = nullptr;
    size_t size = 0;
};

struct D3D12VertexProgram : public VertexProgram {
    std::vector<uint8_t> dxil_bytecode;
    uint32_t uniform_size = 0;
};

struct D3D12FragmentProgram : public FragmentProgram {
    std::vector<uint8_t> dxil_bytecode;
    uint32_t uniform_size = 0;
};

struct D3D12RenderTarget : public RenderTarget {
    ComPtr<ID3D12Resource> color_buffer;
    ComPtr<ID3D12Resource> depth_stencil_buffer;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle{};
    D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle{};
    uint32_t width = 0;
    uint32_t height = 0;
    DXGI_FORMAT color_format = DXGI_FORMAT_R8G8B8A8_UNORM;
    DXGI_FORMAT depth_format = DXGI_FORMAT_D24_UNORM_S8_UINT;
};

struct D3D12Context : public Context {
    MemState *mem = nullptr;
    ComPtr<ID3D12GraphicsCommandList> command_list;
    D3D12RenderTarget *current_rt = nullptr;
};

} // namespace renderer::d3d12
