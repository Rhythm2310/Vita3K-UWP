// Vita3K emulator project - Xbox Series S UWP D3D12 Port
#pragma once

#include <renderer/state.h>
#include <renderer/types.h>

#include <renderer/d3d12/allocator.h>
#include <renderer/d3d12/pipeline_cache.h>
#include <renderer/d3d12/screen_renderer.h>
#include <renderer/d3d12/surface_cache.h>
#include <renderer/d3d12/types.h>

struct Config;
struct MemState;

namespace renderer::d3d12 {

struct D3D12State : public renderer::State {
    MemState *mem = nullptr;

    ComPtr<IDXGIFactory6> dxgi_factory;
    ComPtr<IDXGIAdapter1> adapter;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> command_queue;

    ComPtr<IDXGISwapChain3> swapchain;
    std::array<ComPtr<ID3D12Resource>, FRAME_COUNT> back_buffers;
    ComPtr<ID3D12DescriptorHeap> rtv_heap;
    uint32_t rtv_descriptor_size = 0;

    std::array<FrameResource, FRAME_COUNT> frame_resources;
    uint32_t frame_index = 0;

    ComPtr<ID3D12Fence> fence;
    HANDLE fence_event = nullptr;
    uint64_t current_fence_value = 0;

    ComPtr<ID3D12RootSignature> root_signature;
    ComPtr<ID3D12GraphicsCommandList> direct_command_list;

    D3D12Allocator allocator;
    PipelineCache pipeline_cache;
    D3D12SurfaceCache surface_cache;
    D3D12TextureCache texture_cache;

    ScreenRenderer screen_renderer;
    OverlayRenderer overlay_renderer;
    bool tearing_supported = false;

    D3D12State();
    ~D3D12State() override;

    bool init() override;
    bool create(std::unique_ptr<renderer::State> &state, const Config &config);
    void late_init(const Config &cfg, const std::string_view game_id, MemState &mem) override;
    void cleanup() override;

    TextureCache *get_texture_cache() override {
        return &texture_cache;
    }

    void render_frame(DisplayState &display, const GxmState &gxm, MemState &mem) override;
    void swap_window() override;
    std::vector<uint32_t> dump_frame(DisplayState &display, uint32_t &width, uint32_t &height) override;

    uint32_t get_features_mask() override;
    int get_supported_filters() override;
    void set_screen_filter(const std::string_view &filter) override;
    int get_max_anisotropic_filtering() override;
    void set_anisotropic_filtering(int anisotropic_filtering) override;
    int get_max_2d_texture_width() override;
    void set_async_compilation(bool enable) override;

    bool map_memory(MemState &mem, Ptr<void> address, uint32_t size) override;
    void unmap_memory(MemState &mem, Ptr<void> address) override;

    std::string_view get_gpu_name() override;
    uint32_t get_gpu_version() override;

    void precompile_shader(const ShadersHash &hash) override;
    void preclose_action() override;

    void wait_for_gpu();
    void move_to_next_frame();
};

} // namespace renderer::d3d12
