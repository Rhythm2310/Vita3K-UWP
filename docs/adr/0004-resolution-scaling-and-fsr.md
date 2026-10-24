# Resolution Scaling and Modern FSR Upscaling

## Status
Accepted

## Context
The PS Vita renders at 960x544. On Xbox Series S connected to living room TVs, players expect flexible scaling options from native handheld resolution up to 4K, along with modern spatial/temporal upscaling. Upstream Vita3K contained legacy FSR 1.0 shaders.

## Decision
1. Default display output to native Vita resolution (960x544, 1x scale) with selectable render scales: 1x (Native), 2x (1080p), 2.5x (1440p), and 4x (4K).
2. Remove legacy FSR 1.0 integration in favor of a modern FidelityFX Super Resolution pipeline implemented as a compute/post-process pass in Direct3D 12.
3. Manage memory heaps through D3D12MA to ensure stable VRAM allocation under high resolution scales.
