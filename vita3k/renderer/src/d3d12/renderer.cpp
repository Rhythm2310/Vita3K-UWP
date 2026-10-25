// Vita3K emulator project - Xbox Series S UWP D3D12 Port
#include <renderer/d3d12/functions.h>
#include <renderer/d3d12/state.h>

#include <config/state.h>
#include <display/state.h>
#include <mem/state.h>
#include <mem/functions.h>
#include <util/log.h>

#include <d3dcompiler.h>

namespace renderer::d3d12 {

D3D12State::D3D12State() = default;

D3D12State::~D3D12State() {
    cleanup();
}

bool D3D12State::init() {
    return true;
}

bool D3D12State::create(std::unique_ptr<renderer::State> &state, const Config &config) {
    UINT dxgi_factory_flags = 0;
#if defined(_DEBUG)
    {
        ComPtr<ID3D12Debug> debug_controller;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_controller)))) {
            debug_controller->EnableDebugLayer();
            dxgi_factory_flags |= DXGI_CREATE_FACTORY_DEBUG;
            LOG_INFO("D3D12 Debug Layer enabled");
        }
    }
#endif

    HRESULT hr = CreateDXGIFactory2(dxgi_factory_flags, IID_PPV_ARGS(&dxgi_factory));
    if (FAILED(hr)) {
        LOG_ERROR("Failed to create DXGI Factory 2: 0x{:08X}", static_cast<uint32_t>(hr));
        return false;
    }

    // Select hardware adapter (prefers dedicated GPU or Xbox Series S custom AMD APU)
    ComPtr<IDXGIAdapter1> selected_adapter;
    for (UINT i = 0; dxgi_factory->EnumAdapters1(i, &selected_adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 desc;
        selected_adapter->GetDesc1(&desc);
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
            continue;
        }

        // Check if D3D12 is supported
        if (SUCCEEDED(D3D12CreateDevice(selected_adapter.Get(), D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr))) {
            adapter = selected_adapter;
            break;
        }
    }

    if (!adapter) {
        LOG_ERROR("No compatible D3D12 hardware adapter found");
        return false;
    }

    DXGI_ADAPTER_DESC1 desc;
    adapter->GetDesc1(&desc);
    char adapter_desc[128];
    WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, adapter_desc, sizeof(adapter_desc), nullptr, nullptr);
    LOG_INFO("Selected GPU: {} (Dedicated Video Memory: {} MB)", adapter_desc, desc.DedicatedVideoMemory / (1024 * 1024));

    const D3D_FEATURE_LEVEL feature_levels[] = {
        D3D_FEATURE_LEVEL_12_2,
        D3D_FEATURE_LEVEL_12_1,
        D3D_FEATURE_LEVEL_12_0,
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0
    };

    D3D_FEATURE_LEVEL max_supported_fl = D3D_FEATURE_LEVEL_11_0;
    for (auto fl : feature_levels) {
        hr = D3D12CreateDevice(adapter.Get(), fl, IID_PPV_ARGS(&device));
        if (SUCCEEDED(hr)) {
            max_supported_fl = fl;
            break;
        }
    }

    if (!device) {
        LOG_ERROR("Failed to create D3D12 device: 0x{:08X}", static_cast<uint32_t>(hr));
        return false;
    }

    LOG_INFO("D3D12 Device created with Feature Level 0x{:04X}", static_cast<uint32_t>(max_supported_fl));

    // Direct command queue
    D3D12_COMMAND_QUEUE_DESC queue_desc = {};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    hr = device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&command_queue));
    if (FAILED(hr)) {
        LOG_ERROR("Failed to create direct command queue: 0x{:08X}", static_cast<uint32_t>(hr));
        return false;
    }

    // Initialize memory allocator
    if (!allocator.init(device.Get(), adapter.Get())) {
        LOG_ERROR("Failed to initialize D3D12MA");
        return false;
    }

    // Create frame resources
    for (uint32_t i = 0; i < FRAME_COUNT; ++i) {
        hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&frame_resources[i].command_allocator));
        if (FAILED(hr)) {
            LOG_ERROR("Failed to create command allocator for frame {}: 0x{:08X}", i, static_cast<uint32_t>(hr));
            return false;
        }
    }

    // RTV descriptor heap
    D3D12_DESCRIPTOR_HEAP_DESC rtv_desc = {};
    rtv_desc.NumDescriptors = FRAME_COUNT + 64;
    rtv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtv_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    hr = device->CreateDescriptorHeap(&rtv_desc, IID_PPV_ARGS(&rtv_heap));
    if (FAILED(hr)) {
        LOG_ERROR("Failed to create RTV descriptor heap: 0x{:08X}", static_cast<uint32_t>(hr));
        return false;
    }
    rtv_descriptor_size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    // Fence for frame synchronization
    hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    if (FAILED(hr)) {
        LOG_ERROR("Failed to create D3D12 fence: 0x{:08X}", static_cast<uint32_t>(hr));
        return false;
    }
    fence_event = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
    if (!fence_event) {
        LOG_ERROR("Failed to create fence event");
        return false;
    }

    // Static Root Signature:
    // Root Param 0: Vertex CBV (b0)
    // Root Param 1: Pixel CBV (b1)
    // Root Param 2: Descriptor table (t0..t15) for textures
    D3D12_DESCRIPTOR_RANGE srv_range = {};
    srv_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srv_range.NumDescriptors = 16;
    srv_range.BaseShaderRegister = 0;
    srv_range.RegisterSpace = 0;
    srv_range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER root_params[3] = {};

    root_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    root_params[0].Descriptor.ShaderRegister = 0;
    root_params[0].Descriptor.RegisterSpace = 0;
    root_params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    root_params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    root_params[1].Descriptor.ShaderRegister = 1;
    root_params[1].Descriptor.RegisterSpace = 0;
    root_params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    root_params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_params[2].DescriptorTable.NumDescriptorRanges = 1;
    root_params[2].DescriptorTable.pDescriptorRanges = &srv_range;
    root_params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // Static samplers
    D3D12_STATIC_SAMPLER_DESC samplers[2] = {};
    samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[0].ShaderRegister = 0;
    samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    samplers[1].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[1].ShaderRegister = 1;
    samplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC sig_desc = {};
    sig_desc.NumParameters = 3;
    sig_desc.pParameters = root_params;
    sig_desc.NumStaticSamplers = 2;
    sig_desc.pStaticSamplers = samplers;
    sig_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> serialized_sig;
    ComPtr<ID3DBlob> error_blob;
    hr = D3D12SerializeRootSignature(&sig_desc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized_sig, &error_blob);
    if (FAILED(hr)) {
        if (error_blob) {
            LOG_ERROR("Root signature serialization error: {}", static_cast<const char *>(error_blob->GetBufferPointer()));
        }
        return false;
    }

    hr = device->CreateRootSignature(0, serialized_sig->GetBufferPointer(), serialized_sig->GetBufferSize(), IID_PPV_ARGS(&root_signature));
    if (FAILED(hr)) {
        LOG_ERROR("Failed to create root signature: 0x{:08X}", static_cast<uint32_t>(hr));
        return false;
    }

    // Direct command list
    hr = device->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        frame_resources[0].command_allocator.Get(),
        nullptr,
        IID_PPV_ARGS(&direct_command_list));
    if (FAILED(hr)) {
        LOG_ERROR("Failed to create direct command list: 0x{:08X}", static_cast<uint32_t>(hr));
        return false;
    }
    direct_command_list->Close();

    // Initialize screen & overlay renderer
    screen_renderer.init(device.Get(), DXGI_FORMAT_R8G8B8A8_UNORM);
    overlay_renderer.init(device.Get(), command_queue.Get());

    LOG_INFO("DirectX 12 Backend fully initialized for Xbox Series S");
    return true;
}

void D3D12State::late_init(const Config &cfg, const std::string_view game_id, MemState &mem) {
    this->mem = &mem;
}

void D3D12State::cleanup() {
    wait_for_gpu();

    if (fence_event) {
        CloseHandle(fence_event);
        fence_event = nullptr;
    }

    pipeline_cache.clear();
    surface_cache.clear();
    screen_renderer.cleanup();
    overlay_renderer.cleanup();
    allocator.cleanup();
}

void D3D12State::render_frame(DisplayState &display, const GxmState &gxm, MemState &mem) {
    auto &frame = frame_resources[frame_index];
    frame.command_allocator->Reset();
    direct_command_list->Reset(frame.command_allocator.Get(), nullptr);

    // If backbuffers exist, render screen and overlay
    if (swapchain && back_buffers[frame_index]) {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = back_buffers[frame_index].Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        direct_command_list->ResourceBarrier(1, &barrier);

        D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle = rtv_heap->GetCPUDescriptorHandleForHeapStart();
        rtv_handle.ptr += frame_index * rtv_descriptor_size;

        const float clear_color[] = { 0.0f, 0.0f, 0.0f, 1.0f };
        direct_command_list->ClearRenderTargetView(rtv_handle, clear_color, 0, nullptr);

        // Screen rendering pass (Vita FB -> Backbuffer with scaling)
        screen_renderer.render(direct_command_list.Get(), nullptr, rtv_handle);

        // Overlay rendering pass (UI / notification)
        overlay_renderer.render(direct_command_list.Get());

        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        direct_command_list->ResourceBarrier(1, &barrier);
    }

    direct_command_list->Close();
    ID3D12CommandList *cmd_lists[] = { direct_command_list.Get() };
    command_queue->ExecuteCommandLists(1, cmd_lists);
}

void D3D12State::swap_window() {
    if (swapchain) {
        swapchain->Present(1, 0);
    }
    move_to_next_frame();
}

std::vector<uint32_t> D3D12State::dump_frame(DisplayState &display, uint32_t &width, uint32_t &height) {
    width = 960;
    height = 544;
    return std::vector<uint32_t>(width * height, 0);
}

uint32_t D3D12State::get_features_mask() {
    return 0xFFFFFFFF;
}

int D3D12State::get_supported_filters() {
    return 3;
}

void D3D12State::set_screen_filter(const std::string_view &filter) {
}

int D3D12State::get_max_anisotropic_filtering() {
    return 16;
}

void D3D12State::set_anisotropic_filtering(int anisotropic_filtering) {
}

int D3D12State::get_max_2d_texture_width() {
    return 16384;
}

void D3D12State::set_async_compilation(bool enable) {
}

bool D3D12State::map_memory(MemState &mem, Ptr<void> address, uint32_t size) {
    return true;
}

void D3D12State::unmap_memory(MemState &mem, Ptr<void> address) {
}

std::string_view D3D12State::get_gpu_name() {
    return "Xbox Series S Custom RDNA2 GPU";
}

uint32_t D3D12State::get_gpu_version() {
    return 12;
}

void D3D12State::precompile_shader(const ShadersHash &hash) {
}

void D3D12State::preclose_action() {
    wait_for_gpu();
}

void D3D12State::wait_for_gpu() {
    if (!command_queue || !fence || !fence_event) return;

    current_fence_value++;
    command_queue->Signal(fence.Get(), current_fence_value);
    fence->SetEventOnCompletion(current_fence_value, fence_event);
    WaitForSingleObjectEx(fence_event, INFINITE, FALSE);
}

void D3D12State::move_to_next_frame() {
    if (!command_queue || !fence || !fence_event) return;

    const uint64_t fence_to_signal = ++current_fence_value;
    command_queue->Signal(fence.Get(), fence_to_signal);
    frame_resources[frame_index].fence_value = fence_to_signal;

    frame_index = (frame_index + 1) % FRAME_COUNT;

    if (fence->GetCompletedValue() < frame_resources[frame_index].fence_value) {
        fence->SetEventOnCompletion(frame_resources[frame_index].fence_value, fence_event);
        WaitForSingleObjectEx(fence_event, INFINITE, FALSE);
    }
}

} // namespace renderer::d3d12
