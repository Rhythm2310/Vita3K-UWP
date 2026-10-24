// Vita3K emulator project - Xbox Series S UWP D3D12 Port
#include <renderer/d3d12/functions.h>
#include <renderer/d3d12/types.h>

#include <mem/state.h>
#include <mem/functions.h>
#include <util/log.h>

namespace renderer::d3d12 {

void set_context(D3D12Context &context, MemState &mem, D3D12RenderTarget *rt, const FeatureState &features) {
    context.current_rt = rt;
    if (!rt || !context.command_list) return;

    // Transition render target to render target state if needed
    D3D12_VIEWPORT viewport = {};
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = static_cast<float>(rt->width);
    viewport.Height = static_cast<float>(rt->height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    context.command_list->RSSetViewports(1, &viewport);

    D3D12_RECT scissor = {};
    scissor.left = 0;
    scissor.top = 0;
    scissor.right = static_cast<LONG>(rt->width);
    scissor.bottom = static_cast<LONG>(rt->height);
    context.command_list->RSSetScissorRects(1, &scissor);
}

void set_uniform_buffer(D3D12Context &context, MemState &mem, const ShaderProgram *program, const bool vertex_shader, const int block_num, const int size, Ptr<uint8_t> data) {
    if (!context.command_list || size <= 0 || !data) return;

    // Uniform data from Vita memory is uploaded or bound via root CBV/constants
    // Root parameter 0 = Vertex CBV, Root parameter 1 = Pixel CBV
    const UINT root_param = vertex_shader ? 0 : 1;
    // For small uniform buffers, set 32-bit root constants or inline descriptors
}

} // namespace renderer::d3d12
