// Vita3K emulator project - Xbox Series S UWP D3D12 Port
#include "archive.h"
#include "interface.h"

#include <app/functions.h>
#include <config/functions.h>
#include <config/version.h>
#include <emuenv/app_launch_request.h>
#include <emuenv/state.h>
#include <include/cpu.h>
#include <include/environment.h>
#include <io/state.h>
#include <modules/module_parent.h>
#include <packages/functions.h>
#include <packages/license.h>
#include <packages/pkg.h>
#include <packages/sfo.h>
#include <renderer/d3d12/state.h>
#include <renderer/frame_host.h>
#include <renderer/functions.h>
#include <shader/spirv_recompiler.h>
#include <util/log.h>
#include <util/string_utils.h>

#ifdef _WIN32
#include <combaseapi.h>
#define SDL_MAIN_HANDLED
#endif

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#endif

#include <SDL3/SDL.h>
#include <SDL3/SDL_cpuinfo.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

// Xbox Series FrameHost binding SDL_Window to Direct3D 12
class XboxFrameHost final : public renderer::FrameHost {
public:
    explicit XboxFrameHost(SDL_Window *win) : m_window(win) {}

    renderer::DisplayHandle handle() const override {
        if (!m_window) return renderer::Win32DisplayHandle{ nullptr };
        auto props = SDL_GetWindowProperties(m_window);
        void *hwnd = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
        if (!hwnd) {
            hwnd = SDL_GetPointerProperty(props, "SDL.window.winrt.corewindow", nullptr);
        }
        return renderer::Win32DisplayHandle{ hwnd };
    }

    int drawable_width() const override {
        int w = 1920, h = 1080;
        if (m_window) SDL_GetWindowSizeInPixels(m_window, &w, &h);
        return w;
    }

    int drawable_height() const override {
        int w = 1920, h = 1080;
        if (m_window) SDL_GetWindowSizeInPixels(m_window, &w, &h);
        return h;
    }

    std::vector<std::string> font_dirs() const override {
        return {};
    }
};

int main(int argc, char *argv[]) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif

    Root root_paths;
    bool portable = app::init_paths(root_paths);

    if (!fs::exists(root_paths.get_vita_fs_path())) {
        fs::create_directories(root_paths.get_vita_fs_path());
    }

    if (logging::init(root_paths, true) != Success) {
        return InitConfigFailed;
    }

    Config cfg{};
    EmuEnvState emuenv;
    const auto config_err = config::init_config(cfg, argc, argv, root_paths, portable);

    if (config_err != Success) {
        if (config_err == QuitRequested) {
            if (cfg.recompile_shader_path.has_value()) {
                LOG_INFO("Recompiling {}", *cfg.recompile_shader_path);
                shader::convert_gxp_to_glsl_from_filepath(*cfg.recompile_shader_path);
            }
            if (cfg.delete_title_id.has_value()) {
                LOG_INFO("Deleting title id {}", *cfg.delete_title_id);
                fs::remove_all(cfg.get_vita_fs_path() / "ux0/app" / *cfg.delete_title_id);
                fs::remove_all(cfg.get_vita_fs_path() / "ux0/addcont" / *cfg.delete_title_id);
                fs::remove_all(cfg.get_vita_fs_path() / "ux0/user/00/savedata" / *cfg.delete_title_id);
                fs::remove_all(root_paths.get_cache_path() / "shaders" / *cfg.delete_title_id);
            }
            if (cfg.pup_path.has_value()) {
                fs::create_directories(cfg.get_vita_fs_path());
                LOG_INFO("Installing firmware file {}", *cfg.pup_path);
                install_pup(cfg.get_vita_fs_path(), *cfg.pup_path, [](uint32_t progress) {
                    LOG_INFO("Firmware installation progress: {}%", progress);
                });
            }
            if (cfg.pkg_path.has_value() && cfg.pkg_zrif.has_value()) {
                fs::create_directories(cfg.get_vita_fs_path());
                LOG_INFO("Installing pkg from {} ", *cfg.pkg_path);
                emuenv.cache_path = root_paths.get_cache_path().generic_path();
                emuenv.vita_fs_path = cfg.get_vita_fs_path();
                auto pkg_path = fs_utils::utf8_to_path(*cfg.pkg_path);
                install_pkg(pkg_path, emuenv, *cfg.pkg_zrif, [](float) {});
            }
            return Success;
        }
        LOG_ERROR("Failed to initialise config");
        return InitConfigFailed;
    }

    fs::create_directories(cfg.get_vita_fs_path());

#ifdef _WIN32
    {
        auto res = CoInitializeEx(NULL, COINIT_MULTITHREADED);
        LOG_ERROR_IF(res == S_FALSE, "Failed to initialize COM Library");
    }
#endif

    std::atexit(SDL_Quit);

    SDL_SetHint(SDL_HINT_JOYSTICK_THREAD, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_ENHANCED_REPORTS, "1");

    if (!SDL_Init(SDL_INIT_AUDIO | SDL_INIT_GAMEPAD | SDL_INIT_HAPTIC | SDL_INIT_VIDEO)) {
        LOG_ERROR("SDL initialisation failed: {}", SDL_GetError());
        return SDLInitFailed;
    }

    LOG_INFO("{}", window_title);
    LOG_INFO("OS: {}", CppCommon::Environment::OSVersion());
    LOG_INFO("CPU: {} | {} Threads | {} GHz", CppCommon::CPU::Architecture(), CppCommon::CPU::LogicalCores(), static_cast<float>(CppCommon::CPU::ClockSpeed()) / 1000.f);
    LOG_INFO("Available RAM: {} MiB", SDL_GetSystemRAM());

    // Create main Xbox presentation window
    SDL_Window *window = SDL_CreateWindow("Vita3K", 1920, 1080, SDL_WINDOW_FULLSCREEN);
    if (!window) {
        LOG_WARN("SDL_CreateWindow fullscreen failed, trying windowed: {}", SDL_GetError());
        window = SDL_CreateWindow("Vita3K", 1920, 1080, 0);
    }

    if (!app::init(emuenv, cfg, root_paths)) {
        LOG_ERROR("Emulated environment initialization failed.");
        return 1;
    }

    if (emuenv.cfg.controller_binds.empty() || (emuenv.cfg.controller_binds.size() != 15)
        || emuenv.cfg.controller_axis_binds.empty() || (emuenv.cfg.controller_axis_binds.size() != 6)) {
        app::reset_controller_binding(emuenv);
    }

    init_libraries(emuenv);

    if (!app::init_apps_list(emuenv)) {
        LOG_ERROR("Failed to initialize apps list.");
        return 1;
    }

    app::load_users(emuenv);

    // Initialize Direct3D 12 graphics backend
    XboxFrameHost frame_host(window);
    if (!renderer::init(frame_host, emuenv.renderer, emuenv.backend_renderer, emuenv.cfg, emuenv.get_root_paths())) {
        LOG_ERROR("Failed to initialize Direct3D 12 renderer.");
        return 1;
    }

    apply_renderer_config(emuenv);
    if (!late_init(emuenv)) {
        LOG_ERROR("Late init failed.");
        return 1;
    }

    auto &d3d12 = dynamic_cast<renderer::d3d12::D3D12State &>(*emuenv.renderer);
    d3d12.overlay_renderer.set_vita_fs_path(cfg.get_vita_fs_path());

    auto refresh_osd_library = [&]() {
        std::vector<renderer::d3d12::OsdAppInfo> osd_apps;
        for (const auto &a : emuenv.app.apps_list.apps) {
            osd_apps.push_back({ a.title_id, a.title_name, a.category, a.app_ver });
        }
        d3d12.overlay_renderer.set_installed_apps(osd_apps);
    };
    refresh_osd_library();

    bool running = true;
    bool app_running = false;

    // Connect OSD Firmware and Package installer callbacks
    d3d12.overlay_renderer.on_install_firmware = [&](const std::string &pup_path) {
        LOG_INFO("OSD: Triggered firmware install from: {}", pup_path);
        d3d12.overlay_renderer.set_install_in_progress(true);
        d3d12.overlay_renderer.set_install_progress(5, "Reading PUP archive...");

        std::thread worker([&d3d12, &emuenv, pup_path, refresh_osd_library]() {
            try {
                std::string fw_ver = install_pup(
                    emuenv.vita_fs_path,
                    pup_path,
                    [&d3d12](uint32_t pct) {
                        std::string msg = "Installing firmware system partitions...";
                        if (pct <= 10) msg = "Extracting PUP archive...";
                        else if (pct <= 20) msg = "Verifying package signatures...";
                        else if (pct <= 30) msg = "Decrypting firmware payloads (SCE keys)...";
                        else if (pct <= 70) msg = "Extracting vs0, os0, sa0 system modules...";
                        else if (pct >= 100) msg = "Firmware extraction complete!";
                        d3d12.overlay_renderer.set_install_progress(pct, msg);
                    }
                );
                d3d12.overlay_renderer.set_firmware_installed(true);
                d3d12.overlay_renderer.set_install_progress(100, "PS Vita Firmware v" + (fw_ver.empty() ? std::string("3.65") : fw_ver) + " successfully installed!");
                app::init_apps_list(emuenv);
                refresh_osd_library();
            } catch (const std::exception &e) {
                d3d12.overlay_renderer.set_install_progress(0, std::string("Install Error: ") + e.what());
            }
            d3d12.overlay_renderer.set_install_in_progress(false);
        });
        worker.detach();
    };

    d3d12.overlay_renderer.on_install_vpk = [&](const std::string &vpk_path) {
        LOG_INFO("OSD: Triggered VPK install from: {}", vpk_path);
        d3d12.overlay_renderer.set_install_in_progress(true);
        d3d12.overlay_renderer.set_install_progress(10, "Extracting package contents...");

        std::thread worker([&d3d12, &emuenv, vpk_path, refresh_osd_library]() {
            try {
                install_archive(emuenv, fs::path(vpk_path), [&d3d12](ArchiveContents contents) {
                    if (contents.progress.has_value()) {
                        uint32_t pct = static_cast<uint32_t>(*contents.progress * 100.0f);
                        d3d12.overlay_renderer.set_install_progress(pct, "Extracting package (" + std::to_string(pct) + "%)...");
                    }
                });
                d3d12.overlay_renderer.set_install_progress(100, "Package installed successfully!");
                app::init_apps_list(emuenv);
                refresh_osd_library();
            } catch (const std::exception &e) {
                d3d12.overlay_renderer.set_install_progress(0, std::string("Install Error: ") + e.what());
            }
            d3d12.overlay_renderer.set_install_in_progress(false);
        });
        worker.detach();
    };

    d3d12.overlay_renderer.on_launch_app = [&](const std::string &title_id) {
        LOG_INFO("OSD: Launching title {}", title_id);
        int32_t main_module_id = 0;
        AppLaunchRequest request{ .app_path = title_id };
        ExitCode load_code = load_app(main_module_id, emuenv, request);
        if (load_code == Success) {
            run_app(emuenv, main_module_id, request);
            d3d12.overlay_renderer.set_osd_open(false);
            d3d12.overlay_renderer.set_in_game(true);
            renderer::start_render_thread(*emuenv.renderer, emuenv.display, emuenv.gxm, emuenv.mem, emuenv.cfg);
            app_running = true;
        } else {
            LOG_ERROR("Failed to load application {}", title_id);
            d3d12.overlay_renderer.set_osd_open(true);
            d3d12.overlay_renderer.set_in_game(false);
        }
    };

    d3d12.overlay_renderer.on_restart_app = [&]() {
        LOG_INFO("OSD: Restart requested");
    };

    d3d12.overlay_renderer.on_exit_to_dashboard = [&]() {
        LOG_INFO("OSD: Exiting to Vita3K Dashboard");
        if (app_running) {
            renderer::stop_render_thread(*emuenv.renderer);
            app_running = false;
        }
        d3d12.overlay_renderer.set_in_game(false);
        d3d12.overlay_renderer.set_osd_open(true);
        refresh_osd_library();
    };

    d3d12.overlay_renderer.on_exit_app = [&]() {
        LOG_INFO("OSD: Exiting Vita3K to Xbox Dashboard");
        running = false;
    };

    SDL_Gamepad *gamepad = nullptr;
    uint64_t last_time = SDL_GetTicks();
    uint64_t fps_last_calc = last_time;
    uint32_t frame_counter = 0;

    // Auto-boot app if specified via CLI
    if (cfg.run_app_path.has_value() && !cfg.run_app_path->empty()) {
        d3d12.overlay_renderer.on_launch_app(*cfg.run_app_path);
    }

    // Main Interactive Loop (Xbox Series S 60 FPS Presentation)
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (event.type == SDL_EVENT_GAMEPAD_ADDED) {
                if (!gamepad) gamepad = SDL_OpenGamepad(event.gdevice.which);
            } else if (event.type == SDL_EVENT_GAMEPAD_REMOVED) {
                if (gamepad && SDL_GetGamepadID(gamepad) == event.gdevice.which) {
                    SDL_CloseGamepad(gamepad);
                    gamepad = nullptr;
                }
            }
        }

        uint64_t now = SDL_GetTicks();
        float dt = (now - last_time) / 1000.0f;
        last_time = now;

        frame_counter++;
        if (now - fps_last_calc >= 500) {
            float fps = (frame_counter * 1000.0f) / (now - fps_last_calc);
            float ft = 1000.0f / (fps > 0.0f ? fps : 60.0f);
            d3d12.overlay_renderer.update_telemetry(fps, ft);
            frame_counter = 0;
            fps_last_calc = now;
        }

        // Update OSD gamepad inputs
        d3d12.overlay_renderer.update_gamepad_input(gamepad, dt);

        if (!app_running) {
            // Dashboard mode: render OSD at 60 FPS
            d3d12.render_frame(emuenv.display, emuenv.gxm, *emuenv.mem);
            d3d12.swap_window();
            SDL_Delay(16); // ~60 FPS
        } else {
            SDL_Delay(10);
        }
    }

    if (app_running) {
        renderer::stop_render_thread(*emuenv.renderer);
    }

    if (gamepad) {
        SDL_CloseGamepad(gamepad);
    }

    emuenv.renderer->cleanup();
    if (window) {
        SDL_DestroyWindow(window);
    }

#ifdef _WIN32
    CoUninitialize();
#endif

    return Success;
}
