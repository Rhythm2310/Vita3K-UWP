# Codebase Pruning and UWP Specialization

## Status
Accepted

## Context
Upstream Vita3K contains legacy desktop and mobile codebases including Android JNI, Qt GUI (`gui-qt`), auto-updater, camera capture, Tracy profiler, GDB debugger, and desktop file dialogs. None of these function inside the Xbox Series S UWP sandbox.

## Decision
1. Prune dead modules directly from the repository tree: delete `android/`, `vita3k/gui-qt/`, `vita3k/updater/`, `vita3k/camera/`, `external/nativefiledialog-extended/`, and desktop `gl/` renderer.
2. Standardize audio exclusively on WASAPI via `cubeb`, dropping all non-Windows audio drivers.
3. Standardize windowing, input, and lifecycle management on SDL2's native WinRT backend (`SDL_WinRTApp`).
