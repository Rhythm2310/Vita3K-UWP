# D3D12 Pipeline Architecture: KISS and YAGNI Principles

## Status
Accepted

## Context
Upstream Vita3K's Vulkan backend features complex multi-threaded descriptor pooling, dynamic memory allocators, and multi-tier surface caching. On Xbox Series S, building an overly complex D3D12 engine risks instability and prolonged development.

## Decision
1. Implement a single direct command queue on the dedicated render thread, utilizing triple-buffered command allocators and frame swapchains (`DXGI_SWAP_EFFECT_FLIP_DISCARD`).
2. Utilize a single unified static Root Signature exposing root 32-bit constants for GXM uniform registers and fixed descriptor tables for SRVs (textures and samplers).
3. Translate Vita USSE bytecode via `spirv_recompiler` to SPIR-V, convert SPIR-V to HLSL via `SPIRV-Cross`, and compile HLSL to DXIL using `DirectXShaderCompiler` (DXC), backed by an on-disk PSO cache.
