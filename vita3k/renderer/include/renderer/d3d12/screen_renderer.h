// Vita3K emulator project - Xbox Series S UWP D3D12 Port
#pragma once

#include <renderer/d3d12/types.h>
#include <util/fs.h>

#include <atomic>
#include <functional>
#include <string>
#include <vector>

typedef struct SDL_Gamepad SDL_Gamepad;

namespace renderer::d3d12 {

struct OsdAppInfo {
    std::string title_id;
    std::string title_name;
    std::string category;
    std::string version;
};

class OverlayRenderer {
public:
    OverlayRenderer();
    ~OverlayRenderer();

    bool init(ID3D12Device *device, ID3D12CommandQueue *command_queue);
    void render(ID3D12GraphicsCommandList *command_list, D3D12_CPU_DESCRIPTOR_HANDLE rtv_target, float width = 1920.0f, float height = 1080.0f);
    void render(ID3D12GraphicsCommandList *command_list);
    void cleanup();

    // Gamepad Navigation & Input
    void update_gamepad_input(SDL_Gamepad *gamepad, float dt);
    bool is_osd_open() const { return m_osd_open; }
    void set_osd_open(bool open) { m_osd_open = open; }
    void toggle_osd() { m_osd_open = !m_osd_open; }
    void set_in_game(bool in_game) { m_in_game = in_game; }
    bool is_in_game() const { return m_in_game; }

    // Library & Storage Data
    void set_vita_fs_path(const fs::path &path);
    void set_installed_apps(const std::vector<OsdAppInfo> &apps);
    void scan_storage();

    // Firmware & Package Installation State
    void set_firmware_installed(bool installed) { m_fw_installed = installed; }
    bool is_firmware_installed() const { return m_fw_installed; }
    void set_install_progress(uint32_t pct, const std::string &status);
    void set_install_in_progress(bool in_prog) { m_install_in_progress = in_prog; }
    bool is_install_in_progress() const { return m_install_in_progress.load(); }

    // Graphics & Telemetry Configuration
    float get_resolution_multiplier() const;
    const char *get_resolution_name() const;
    bool is_cas_enabled() const { return m_cas_enabled; }
    float get_cas_sharpness() const { return m_cas_sharpness; }
    bool is_vrr_enabled() const { return m_vrr_enabled; }
    int get_hud_mode() const { return m_hud_mode; }
    void update_telemetry(float fps, float frame_time_ms);

    // Callbacks for Main Thread / System Execution
    std::function<void(const std::string &title_id)> on_launch_app;
    std::function<void(const std::string &pup_path)> on_install_firmware;
    std::function<void(const std::string &vpk_path)> on_install_vpk;
    std::function<void()> on_restart_app;
    std::function<void()> on_exit_to_dashboard;
    std::function<void()> on_exit_app;

private:
    bool m_initialized = false;
    bool m_osd_open = true; // Open on startup
    bool m_in_game = false;
    int m_active_tab = 0; // 0=Games, 1=Firmware/Content, 2=Graphics, 3=Xbox System
    int m_selected_item = 0;
    int m_quick_menu_index = 0;

    // Installed apps
    std::vector<OsdAppInfo> m_installed_apps;
    fs::path m_vita_fs_path;

    // Firmware & storage state
    bool m_fw_installed = false;
    std::string m_detected_pup;
    std::vector<std::string> m_detected_vpks;
    std::atomic<bool> m_install_in_progress{ false };
    std::atomic<uint32_t> m_install_progress{ 0 };
    std::string m_install_status;

    // Graphics & console settings
    int m_res_index = 2; // Default 1080p (2.0x)
    bool m_cas_enabled = true;
    float m_cas_sharpness = 0.5f;
    bool m_vrr_enabled = true; // Allow tearing / VRR
    int m_hud_mode = 1; // 0=Off, 1=Minimal, 2=Full
    bool m_trigger_rumble_enabled = true;

    // Telemetry
    float m_current_fps = 60.0f;
    float m_current_frametime = 16.6f;

    // Input debounce
    float m_input_repeat_timer = 0.0f;
    bool m_prev_button_guide = false;
    bool m_prev_button_view = false;
    bool m_prev_button_menu = false;
    bool m_prev_button_south = false;
    bool m_prev_button_east = false;
    bool m_prev_button_west = false;

    // D3D12 rendering resources
    ComPtr<ID3D12Device> m_device;
    ComPtr<ID3D12RootSignature> m_root_sig;
    ComPtr<ID3D12PipelineState> m_pso;
    ComPtr<ID3D12Resource> m_font_texture;
    ComPtr<ID3D12DescriptorHeap> m_srv_heap;
    ComPtr<ID3D12Resource> m_vertex_buffer;
    D3D12_VERTEX_BUFFER_VIEW m_vb_view = {};
    void *m_vb_mapped = nullptr;
    uint32_t m_vertex_count = 0;

    void build_font_atlas(ID3D12Device *device, ID3D12CommandQueue *queue);
    void create_pipeline(ID3D12Device *device);

    void push_quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, uint32_t color);
    void draw_rect(float x, float y, float w, float h, uint32_t color);
    void draw_rect_outline(float x, float y, float w, float h, float thickness, uint32_t color);
    void draw_text(float x, float y, const std::string &text, uint32_t color, float scale = 2.0f);
    void draw_card(float x, float y, float w, float h, bool selected, const std::string &title, const std::string &subtitle, const std::string &badge = "");
    void draw_progress_bar(float x, float y, float w, float h, float pct, uint32_t fill_color, const std::string &label);

    void render_header(float sw, float sh);
    void render_library_tab(float sw, float sh);
    void render_firmware_tab(float sw, float sh);
    void render_graphics_tab(float sw, float sh);
    void render_system_tab(float sw, float sh);
    void render_in_game_quick_menu(float sw, float sh);
    void render_telemetry_hud(float sw, float sh);
    void render_footer(float sw, float sh);
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
