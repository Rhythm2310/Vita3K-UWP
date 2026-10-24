# Architecture for Xbox Series S Dev Mode: Native D3D12 and Bloat Pruning

## Status
Accepted

## Context
Vita3K's upstream implementation targets desktop platforms (Windows, Linux, macOS, Android) with OpenGL and Vulkan renderers, SDL2/Qt interfaces, and numerous desktop-specific subsystems. Xbox Series S in Dev Mode operates strictly under the Universal Windows Platform (UWP) sandbox with no native Vulkan or desktop OpenGL driver, requiring Direct3D 12 for hardware acceleration.

## Decision
1. Implement a native Direct3D 12 backend (`renderer::d3d12`) consuming HLSL translated from Vita USSE SPIR-V shaders via `SPIRV-Cross`, rather than bundling heavyweight Vulkan-to-D3D12 translation layers.
2. Strip all platform features and dependencies irrelevant to Xbox Dev Mode (Qt GUI, desktop updater, camera, desktop dialogs, Android JNI, and desktop OpenGL/Vulkan renderers) to isolate the Core Emulator.
3. Drive the user interface entirely through an Xbox controller-navigable ImGui overlay rendered directly in the D3D12 swapchain.
4. Separate storage into sandboxed internal AppData for core system state and external USB drive mounting for game title libraries.
