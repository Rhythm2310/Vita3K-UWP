# Vita3K Xbox Series S UWP

A streamlined, DirectX 12-native port of the Vita3K PlayStation Vita emulator optimized specifically for Xbox Series S in Dev Mode.

## Language

**Core Emulator**:
The platform-agnostic emulation engine containing the Dynarmic ARM JIT, memory manager, kernel syscalls, and GXM command interpreter.
_Avoid_: Base, backend engine

**DirectX 12 Backend**:
The native Direct3D 12 rendering implementation that executes Vita GXM drawing and transfer operations on Xbox.
_Avoid_: DX12 wrapper, D3D12 layer

**USSE-to-HLSL Translator**:
The shader pipeline converting Vita SGX543 USSE microcode to SPIR-V via `spirv_recompiler` and then to HLSL byte streams via SPIRV-Cross.
_Avoid_: Shader converter, DXBC compiler

**Xbox Dev Mode UWP**:
The target sandbox environment executing as a Windows 10/11 UWP app with expanded resource allocations on Xbox Series hardware.
_Avoid_: Xbox app, WinRT port

**Dual-Path Storage**:
The filesystem layout separating sandboxed local application data from external USB storage partitions.
_Avoid_: Split storage, hybrid filesystem

**Bloat Pruning**:
The removal of desktop-only, mobile, or debug-centric components (such as Qt, Android JNI, auto-updater, and OpenGL/Vulkan desktop backends) that cannot function in Xbox Dev Mode.
_Avoid_: Trimming, code deletion

**D3D12MA**:
The Direct3D 12 memory allocator managing GPU heaps and resources for buffers and textures.
_Avoid_: Custom allocator, raw heaps

**Resolution Scaling**:
The configurable render resolution setting ranging from 1x Native (960x544) up to 4x (4K).
_Avoid_: Internal resolution, multiplier

**FSR Upscaler**:
The modern AMD FidelityFX Super Resolution compute pipeline enhancing scaled output to target display resolutions.
_Avoid_: FSR 1, Bilinear filter

**Xbox CI Workflow**:
The GitHub Actions workflow building, testing, and packaging the UWP app with detailed diagnostics on Windows runners.
_Avoid_: Local build script, build bot
