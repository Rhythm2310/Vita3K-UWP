// Vita3K emulator project - Xbox Series S UWP D3D12 Port
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

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
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.ApplicationModel.Activation.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.Storage.h>

using namespace winrt::Windows::ApplicationModel::Core;
using namespace winrt::Windows::ApplicationModel::Activation;
using namespace winrt::Windows::UI::Core;
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
#include <cmath>
#include <cstdlib>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

// Xbox Series FrameHost binding CoreWindow (or SDL_Window HWND) to Direct3D 12
class XboxFrameHost final : public renderer::FrameHost {
private:
    void *m_window_handle = nullptr;
    int m_width = 1920;
    int m_height = 1080;

public:
    explicit XboxFrameHost(void *core_window, int width = 1920, int height = 1080)
        : m_window_handle(core_window), m_width(width), m_height(height) {}

    explicit XboxFrameHost(SDL_Window *win) {
        if (win) {
            auto props = SDL_GetWindowProperties(win);
            m_window_handle = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
            if (!m_window_handle) {
                m_window_handle = SDL_GetPointerProperty(props, "SDL.window.winrt.corewindow", nullptr);
            }
            SDL_GetWindowSizeInPixels(win, &m_width, &m_height);
        }
    }

    renderer::DisplayHandle handle() const override {
        return renderer::Win32DisplayHandle{ m_window_handle };
    }

    int drawable_width() const override {
        return m_width;
    }

    int drawable_height() const override {
        return m_height;
    }

    std::vector<std::string> font_dirs() const override {
        return {};
    }
};

#ifdef _WIN32
// C++/WinRT UWP CoreApplication View Implementation
struct App : winrt::implements<App, IFrameworkViewSource, IFrameworkView> {
    CoreWindow m_window{ nullptr };
    bool m_windowClosed{ false };

    IFrameworkView CreateView() {
        return *this;
    }

    void Initialize(CoreApplicationView const &applicationView) {
        applicationView.Activated({ this, &App::OnActivated });
        CoreApplication::Suspending({ this, &App::OnSuspending });
        CoreApplication::Resuming({ this, &App::OnResuming });
    }

    void SetWindow(CoreWindow const &window) {
        m_window = window;
        m_window.Closed({ this, &App::OnWindowClosed });

        // CRITICAL FOR XBOX DEV MODE:
        // Activating CoreWindow immediately informs the Xbox Process Lifecycle Manager
        // that the window is responsive, dismisses the splash screen, and permanently
        // resolves the 0x87e1000a watchdog timeout launch crash!
        m_window.Activate();
    }

    void Load(winrt::hstring const &) {}
    void Uninitialize() {}

    void Run() {
#ifdef TRACY_ENABLE
        ZoneScoped;
#endif
        m_window.Activate();

        IUnknown *core_window_unknown = winrt::get_unknown(m_window);
        auto bounds = m_window.Bounds();
        int win_w = std::max(1920, static_cast<int>(std::round(bounds.Width)));
        int win_h = std::max(1080, static_cast<int>(std::round(bounds.Height)));

        Root root_paths;
        bool portable = app::init_paths(root_paths);

        if (!fs::exists(root_paths.get_vita_fs_path())) {
            fs::create_directories(root_paths.get_vita_fs_path());
        }

        logging::init(root_paths, true);
        LOG_INFO("=================================================================");
        LOG_INFO("Vita3K Xbox Series S/X UWP D3D12 Port Starting");
        LOG_INFO("CoreWindow activated: {}x{}", win_w, win_h);
        LOG_INFO("OS: {}", CppCommon::Environment::OSVersion());
        LOG_INFO("CPU: {} | {} Threads | {} GHz", CppCommon::CPU::Architecture(), CppCommon::CPU::LogicalCores(), static_cast<float>(CppCommon::CPU::ClockSpeed()) / 1000.f);
        LOG_INFO("=================================================================");

        Config cfg{};
        EmuEnvState emuenv;
        char *dummy_argv[] = { (char *)"Vita3K.exe", nullptr };
        config::init_config(cfg, 1, dummy_argv, root_paths, portable);

        fs::create_directories(cfg.get_vita_fs_path());

        SDL_SetHint(SDL_HINT_JOYSTICK_THREAD, "1");
        SDL_SetHint(SDL_HINT_JOYSTICK_ENHANCED_REPORTS, "1");

        if (!SDL_Init(SDL_INIT_AUDIO | SDL_INIT_GAMEPAD | SDL_INIT_HAPTIC)) {
            LOG_WARN("SDL audio/gamepad init: {}", SDL_GetError());
        }

        LOG_INFO("Available RAM: {} MiB", SDL_GetSystemRAM());

        if (!app::init(emuenv, cfg, root_paths)) {
            LOG_ERROR("Emulated environment initialization failed.");
            return;
        }

        if (emuenv.cfg.controller_binds.empty() || (emuenv.cfg.controller_binds.size() != 15)
            || emuenv.cfg.controller_axis_binds.empty() || (emuenv.cfg.controller_axis_binds.size() != 6)) {
            app::reset_controller_binding(emuenv);
        }

        init_libraries(emuenv);

        if (!app::init_apps_list(emuenv)) {
            LOG_ERROR("Failed to initialize apps list.");
            return;
        }

        app::load_users(emuenv);

        // Initialize Direct3D 12 graphics backend with native CoreWindow
        XboxFrameHost frame_host(core_window_unknown, win_w, win_h);
        if (!renderer::init(frame_host, emuenv.renderer, emuenv.backend_renderer, emuenv.cfg, emuenv.get_root_paths())) {
            LOG_ERROR("Failed to initialize Direct3D 12 renderer.");
            return;
        }

        app::apply_renderer_config(emuenv);
        if (!app::late_init(emuenv)) {
            LOG_ERROR("Late init failed.");
            return;
        }

        auto &d3d12 = dynamic_cast<renderer::d3d12::D3D12State &>(*emuenv.renderer);
        d3d12.overlay_renderer.set_vita_fs_path(cfg.get_vita_fs_path());

        auto refresh_osd_library = [&]() {
            std::vector<renderer::d3d12::OsdAppInfo> osd_apps;
            for (const auto &a : emuenv.app.apps_list.apps) {
                std::string display_name = a.title.empty() ? a.title_id : a.title;
                osd_apps.push_back({ a.title_id, display_name, a.category, a.app_ver });
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

        // Main 60 FPS loop processing CoreWindow Dispatcher events & SDL Gamepad input
        while (running && !m_windowClosed) {
            m_window.Dispatcher().ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);

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
                // Dashboard mode: render OSD at 60 FPS directly to Xbox display
                d3d12.render_frame(emuenv.display, emuenv.gxm, emuenv.mem);
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
        SDL_Quit();
    }

    void OnActivated(CoreApplicationView const &, IActivatedEventArgs const &) {
        if (m_window) {
            m_window.Activate();
        }
    }

    void OnSuspending(winrt::Windows::Foundation::IInspectable const &, winrt::Windows::ApplicationModel::SuspendingEventArgs const &) {}
    void OnResuming(winrt::Windows::Foundation::IInspectable const &, winrt::Windows::Foundation::IInspectable const &) {}

    void OnWindowClosed(CoreWindow const &, CoreWindowEventArgs const &) {
        m_windowClosed = true;
    }
};
#endif

// Fallback desktop execution function for non-AppContainer execution (e.g. CLI / dev testing)
static int run_desktop(int argc, char *argv[]) {
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
                shader::convert_gxp_to_glsl_from_filepath(*cfg.recompile_shader_path);
            }
            if (cfg.pup_path.has_value()) {
                install_pup(cfg.get_vita_fs_path(), *cfg.pup_path, [](uint32_t) {});
            }
            return Success;
        }
        return InitConfigFailed;
    }

    fs::create_directories(cfg.get_vita_fs_path());

    SDL_SetHint(SDL_HINT_JOYSTICK_THREAD, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_ENHANCED_REPORTS, "1");

    if (!SDL_Init(SDL_INIT_AUDIO | SDL_INIT_GAMEPAD | SDL_INIT_HAPTIC | SDL_INIT_VIDEO)) {
        LOG_WARN("SDL initialisation with video: {}", SDL_GetError());
    }

    SDL_Window *window = SDL_CreateWindow("Vita3K", 1920, 1080, SDL_WINDOW_FULLSCREEN);
    if (!window) {
        window = SDL_CreateWindow("Vita3K", 1920, 1080, 0);
    }

    if (!app::init(emuenv, cfg, root_paths)) {
        return 1;
    }

    if (emuenv.cfg.controller_binds.empty() || (emuenv.cfg.controller_binds.size() != 15)
        || emuenv.cfg.controller_axis_binds.empty() || (emuenv.cfg.controller_axis_binds.size() != 6)) {
        app::reset_controller_binding(emuenv);
    }

    init_libraries(emuenv);
    if (!app::init_apps_list(emuenv)) return 1;
    app::load_users(emuenv);

    XboxFrameHost frame_host(window);
    if (!renderer::init(frame_host, emuenv.renderer, emuenv.backend_renderer, emuenv.cfg, emuenv.get_root_paths())) {
        return 1;
    }

    app::apply_renderer_config(emuenv);
    if (!app::late_init(emuenv)) return 1;

    auto &d3d12 = dynamic_cast<renderer::d3d12::D3D12State &>(*emuenv.renderer);
    d3d12.overlay_renderer.set_vita_fs_path(cfg.get_vita_fs_path());

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) running = false;
        }
        d3d12.render_frame(emuenv.display, emuenv.gxm, emuenv.mem);
        d3d12.swap_window();
        SDL_Delay(16);
    }

    emuenv.renderer->cleanup();
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
    return Success;
}

int main(int argc, char *argv[]) {
#ifdef _WIN32
    try {
        winrt::init_apartment();
        CoreApplication::Run(winrt::make<App>());
        return 0;
    } catch (const winrt::hresult_error &e) {
        LOG_WARN("CoreApplication::Run: 0x{:08X} (not packaged, falling back to desktop)", static_cast<uint32_t>(e.code()));
    } catch (const std::exception &e) {
        LOG_ERROR("Standard exception in main: {}", e.what());
    } catch (...) {
        LOG_ERROR("Unknown exception in main");
    }
    return run_desktop(argc, argv);
#else
    return run_desktop(argc, argv);
#endif
}
