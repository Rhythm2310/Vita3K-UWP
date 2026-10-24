// Vita3K emulator project - Xbox Series S UWP D3D12 Port
#include "archive.h"
#include "interface.h"

#include <app/functions.h>
#include <config/functions.h>
#include <config/version.h>
#include <emuenv/state.h>
#include <include/cpu.h>
#include <include/environment.h>
#include <io/state.h>
#include <modules/module_parent.h>
#include <packages/functions.h>
#include <packages/license.h>
#include <packages/pkg.h>
#include <packages/sfo.h>
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

#include <cstdlib>
#include <optional>

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

    // Boot app if specified
    if (!cfg.run_app_path.empty()) {
        int32_t main_module_id = 0;
        LOG_INFO("Launching title: {}", cfg.run_app_path);
        ExitCode load_code = load_app(main_module_id, emuenv);
        if (load_code == Success) {
            run_app(emuenv, main_module_id);
        } else {
            LOG_ERROR("Failed to load application {}", cfg.run_app_path);
        }
    } else if (!emuenv.apps_list.empty()) {
        // Auto-boot first available title or homebrew if no CLI argument passed
        cfg.run_app_path = emuenv.apps_list.front().title_id;
        int32_t main_module_id = 0;
        LOG_INFO("Auto-launching installed title: {}", cfg.run_app_path);
        ExitCode load_code = load_app(main_module_id, emuenv);
        if (load_code == Success) {
            run_app(emuenv, main_module_id);
        }
    } else {
        LOG_INFO("Vita3K ready. No applications installed yet in ux0:app.");
    }

#ifdef _WIN32
    CoUninitialize();
#endif

    return Success;
}
