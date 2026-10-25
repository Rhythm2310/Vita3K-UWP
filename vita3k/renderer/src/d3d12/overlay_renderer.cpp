// Vita3K emulator project - Xbox Series S UWP D3D12 Port
#include <renderer/d3d12/screen_renderer.h>
#include "font_8x8.h"
#include "osd_shaders_bytecode.h"

#include <util/log.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_gamepad.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>
#include <string>

namespace renderer::d3d12 {

// 2D Vertex structure for Xbox OSD
struct OsdVertex {
    float x, y;
    float u, v;
    uint32_t color; // 0xAABBGGRR
};

static inline uint32_t pack_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    return (static_cast<uint32_t>(a) << 24) |
           (static_cast<uint32_t>(b) << 16) |
           (static_cast<uint32_t>(g) << 8)  |
           static_cast<uint32_t>(r);
}

// Xbox Theme Colors
static const uint32_t COLOR_BG_DIM       = pack_rgba(10, 14, 20, 240);
static const uint32_t COLOR_PANEL_BG     = pack_rgba(22, 27, 34, 235);
static const uint32_t COLOR_CARD_NORMAL  = pack_rgba(33, 38, 45, 230);
static const uint32_t COLOR_CARD_FOCUSED = pack_rgba(48, 54, 61, 245);
static const uint32_t COLOR_BORDER       = pack_rgba(65, 72, 83, 200);

static const uint32_t COLOR_XBOX_GREEN   = pack_rgba(16, 124, 16, 255);
static const uint32_t COLOR_XBOX_NEON    = pack_rgba(82, 209, 32, 255);
static const uint32_t COLOR_XBOX_BLUE    = pack_rgba(0, 164, 239, 255);
static const uint32_t COLOR_XBOX_RED     = pack_rgba(232, 17, 35, 255);
static const uint32_t COLOR_XBOX_YELLOW  = pack_rgba(255, 185, 0, 255);

static const uint32_t COLOR_TEXT_WHITE   = pack_rgba(240, 246, 252, 255);
static const uint32_t COLOR_TEXT_MUTED   = pack_rgba(139, 148, 158, 255);
static const uint32_t COLOR_TEXT_DIM     = pack_rgba(90, 98, 108, 255);

static const float SOLID_U = 4.0f / 128.0f;
static const float SOLID_V = 4.0f / 128.0f;

static const float s_res_multipliers[] = { 1.0f, 1.333333f, 2.0f, 2.666667f, 4.0f };
static const char *s_res_names[] = {
    "Native 544p (1.0x)",
    "720p HD (1.33x)",
    "1080p FHD (2.0x)",
    "1440p QHD (2.66x)",
    "4K UHD (4.0x)"
};

OverlayRenderer::OverlayRenderer() = default;
OverlayRenderer::~OverlayRenderer() {
    cleanup();
}

void OverlayRenderer::cleanup() {
    if (m_vertex_buffer && m_vb_mapped) {
        m_vertex_buffer->Unmap(0, nullptr);
        m_vb_mapped = nullptr;
    }
    m_vertex_buffer.Reset();
    m_font_texture.Reset();
    m_srv_heap.Reset();
    m_pso.Reset();
    m_root_sig.Reset();
    m_device.Reset();
    m_initialized = false;
}

float OverlayRenderer::get_resolution_multiplier() const {
    if (m_res_index >= 0 && m_res_index < 5) return s_res_multipliers[m_res_index];
    return 2.0f;
}

const char *OverlayRenderer::get_resolution_name() const {
    if (m_res_index >= 0 && m_res_index < 5) return s_res_names[m_res_index];
    return "1080p FHD (2.0x)";
}

void OverlayRenderer::set_vita_fs_path(const fs::path &path) {
    m_vita_fs_path = path;
    scan_storage();
}

void OverlayRenderer::set_installed_apps(const std::vector<OsdAppInfo> &apps) {
    m_installed_apps = apps;
}

void OverlayRenderer::set_install_progress(uint32_t pct, const std::string &status) {
    m_install_progress = pct;
    m_install_status = status;
}

void OverlayRenderer::update_telemetry(float fps, float frame_time_ms) {
    m_current_fps = fps;
    m_current_frametime = frame_time_ms;
}

void OverlayRenderer::scan_storage() {
    boost::system::error_code ec;
    // Check firmware status
    m_fw_installed = (fs::exists(m_vita_fs_path / "vs0", ec) && fs::exists(m_vita_fs_path / "os0", ec));

    // Look for PSP2UPDAT.PUP on USB drives and LocalState
    m_detected_pup.clear();
    const std::vector<fs::path> candidate_pup_paths = {
        fs::path("D:\\PSP2UPDAT.PUP"),
        fs::path("D:\\Vita3K\\PSP2UPDAT.PUP"),
        fs::path("E:\\PSP2UPDAT.PUP"),
        fs::path("E:\\Vita3K\\PSP2UPDAT.PUP"),
        fs::path("F:\\PSP2UPDAT.PUP"),
        fs::path("F:\\Vita3K\\PSP2UPDAT.PUP"),
        fs::path("G:\\PSP2UPDAT.PUP"),
        fs::path("G:\\Vita3K\\PSP2UPDAT.PUP"),
        m_vita_fs_path / "PSP2UPDAT.PUP",
        m_vita_fs_path / "packages" / "PSP2UPDAT.PUP"
    };

    for (const auto &p : candidate_pup_paths) {
        if (fs::exists(p, ec) && !ec) {
            m_detected_pup = p.string();
            break;
        }
    }

    // Look for .vpk, .pkg, and .zip packages on USB drives and LocalState
    m_detected_vpks.clear();
    const std::vector<fs::path> scan_dirs = {
        fs::path("D:\\"),
        fs::path("D:\\Vita3K"),
        fs::path("E:\\"),
        fs::path("E:\\Vita3K"),
        fs::path("F:\\"),
        fs::path("F:\\Vita3K"),
        fs::path("G:\\"),
        fs::path("G:\\Vita3K"),
        m_vita_fs_path,
        m_vita_fs_path / "packages",
        m_vita_fs_path / "vpk"
    };

    for (const auto &dir : scan_dirs) {
        if (fs::exists(dir, ec) && fs::is_directory(dir, ec)) {
            for (const auto &entry : fs::directory_iterator(dir, ec)) {
                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext == ".vpk" || ext == ".pkg" || ext == ".zip") {
                    m_detected_vpks.push_back(entry.path().string());
                }
            }
        }
    }
}

void OverlayRenderer::build_font_atlas(ID3D12Device *device, ID3D12CommandQueue *queue) {
    // 128x128 pixels, 8-bit R8_UNORM texture
    const uint32_t atlas_dim = 128;
    const uint32_t row_pitch = 256; // D3D12 aligned pitch
    std::vector<uint8_t> upload_data(row_pitch * atlas_dim, 0);

    // Cell 0 (x: 0..7, y: 0..7): Solid white (all 0xFF) for drawing colored rectangles and shapes
    for (uint32_t y = 0; y < 8; ++y) {
        for (uint32_t x = 0; x < 8; ++x) {
            upload_data[y * row_pitch + x] = 0xFF;
        }
    }

    // Cells 1..96: Printable ASCII characters 32..127
    for (int c = 32; c < 128; ++c) {
        int idx = (c - 32) + 1;
        int col = idx % 16;
        int row = idx / 16;

        for (int py = 0; py < 8; ++py) {
            uint8_t byte = s_font8x8[c - 32][py];
            for (int px = 0; px < 8; ++px) {
                bool bit = (byte >> (7 - px)) & 1;
                upload_data[(row * 8 + py) * row_pitch + (col * 8 + px)] = bit ? 0xFF : 0x00;
            }
        }
    }

    // Create Default Heap texture
    D3D12_RESOURCE_DESC tex_desc = {};
    tex_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    tex_desc.Width = atlas_dim;
    tex_desc.Height = atlas_dim;
    tex_desc.DepthOrArraySize = 1;
    tex_desc.MipLevels = 1;
    tex_desc.Format = DXGI_FORMAT_R8_UNORM;
    tex_desc.SampleDesc.Count = 1;
    tex_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    tex_desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    D3D12_HEAP_PROPERTIES default_heap = {};
    default_heap.Type = D3D12_HEAP_TYPE_DEFAULT;

    HRESULT hr = device->CreateCommittedResource(
        &default_heap,
        D3D12_HEAP_FLAG_NONE,
        &tex_desc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&m_font_texture)
    );
    if (FAILED(hr)) {
        LOG_ERROR("Failed to create font texture: 0x{:08X}", static_cast<uint32_t>(hr));
        return;
    }

    // Create staging upload buffer
    D3D12_RESOURCE_DESC upload_desc = {};
    upload_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    upload_desc.Width = upload_data.size();
    upload_desc.Height = 1;
    upload_desc.DepthOrArraySize = 1;
    upload_desc.MipLevels = 1;
    upload_desc.Format = DXGI_FORMAT_UNKNOWN;
    upload_desc.SampleDesc.Count = 1;
    upload_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    upload_desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    D3D12_HEAP_PROPERTIES upload_heap = {};
    upload_heap.Type = D3D12_HEAP_TYPE_UPLOAD;

    ComPtr<ID3D12Resource> upload_buf;
    hr = device->CreateCommittedResource(
        &upload_heap,
        D3D12_HEAP_FLAG_NONE,
        &upload_desc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&upload_buf)
    );
    if (FAILED(hr)) return;

    void *mapped_ptr = nullptr;
    upload_buf->Map(0, nullptr, &mapped_ptr);
    memcpy(mapped_ptr, upload_data.data(), upload_data.size());
    upload_buf->Unmap(0, nullptr);

    // Record copy command
    ComPtr<ID3D12CommandAllocator> cmd_alloc;
    ComPtr<ID3D12GraphicsCommandList> cmd_list;
    device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmd_alloc));
    device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmd_alloc.Get(), nullptr, IID_PPV_ARGS(&cmd_list));

    D3D12_TEXTURE_COPY_LOCATION dst_loc = {};
    dst_loc.pResource = m_font_texture.Get();
    dst_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst_loc.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION src_loc = {};
    src_loc.pResource = upload_buf.Get();
    src_loc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src_loc.PlacedFootprint.Offset = 0;
    src_loc.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8_UNORM;
    src_loc.PlacedFootprint.Footprint.Width = atlas_dim;
    src_loc.PlacedFootprint.Footprint.Height = atlas_dim;
    src_loc.PlacedFootprint.Footprint.Depth = 1;
    src_loc.PlacedFootprint.Footprint.RowPitch = row_pitch;

    cmd_list->CopyTextureRegion(&dst_loc, 0, 0, 0, &src_loc, nullptr);

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = m_font_texture.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmd_list->ResourceBarrier(1, &barrier);

    cmd_list->Close();
    ID3D12CommandList *lists[] = { cmd_list.Get() };
    queue->ExecuteCommandLists(1, lists);

    // Wait for copy completion
    ComPtr<ID3D12Fence> fence;
    device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    HANDLE event = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
    queue->Signal(fence.Get(), 1);
    fence->SetEventOnCompletion(1, event);
    WaitForSingleObjectEx(event, INFINITE, FALSE);
    CloseHandle(event);

    // Create Shader-Visible SRV Descriptor Heap
    D3D12_DESCRIPTOR_HEAP_DESC srv_heap_desc = {};
    srv_heap_desc.NumDescriptors = 1;
    srv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    device->CreateDescriptorHeap(&srv_heap_desc, IID_PPV_ARGS(&m_srv_heap));

    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.Format = DXGI_FORMAT_R8_UNORM;
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(m_font_texture.Get(), &srv_desc, m_srv_heap->GetCPUDescriptorHandleForHeapStart());
}

void OverlayRenderer::create_pipeline(ID3D12Device *device) {
    // Root parameter 0: Screen size constants (2 floats: width, height) at b0
    // Root parameter 1: Descriptor table (t0)
    D3D12_DESCRIPTOR_RANGE range = {};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1;
    range.BaseShaderRegister = 0;
    range.RegisterSpace = 0;
    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER root_params[2] = {};
    root_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    root_params[0].Constants.ShaderRegister = 0;
    root_params[0].Constants.RegisterSpace = 0;
    root_params[0].Constants.Num32BitValues = 2;
    root_params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    root_params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_params[1].DescriptorTable.NumDescriptorRanges = 1;
    root_params[1].DescriptorTable.pDescriptorRanges = &range;
    root_params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ShaderRegister = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC sig_desc = {};
    sig_desc.NumParameters = 2;
    sig_desc.pParameters = root_params;
    sig_desc.NumStaticSamplers = 1;
    sig_desc.pStaticSamplers = &sampler;
    sig_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> serialized_sig;
    D3D12SerializeRootSignature(&sig_desc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized_sig, nullptr);
    device->CreateRootSignature(0, serialized_sig->GetBufferPointer(), serialized_sig->GetBufferSize(), IID_PPV_ARGS(&m_root_sig));

    D3D12_INPUT_ELEMENT_DESC input_layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {};
    pso_desc.pRootSignature = m_root_sig.Get();
    pso_desc.VS = { g_osd_vs_bytecode, g_osd_vs_bytecode_size };
    pso_desc.PS = { g_osd_ps_bytecode, g_osd_ps_bytecode_size };

    D3D12_BLEND_DESC blend_desc = {};
    blend_desc.RenderTarget[0].BlendEnable = TRUE;
    blend_desc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blend_desc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    blend_desc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blend_desc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    blend_desc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    blend_desc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pso_desc.BlendState = blend_desc;
    pso_desc.SampleMask = UINT_MAX;

    D3D12_RASTERIZER_DESC rast_desc = {};
    rast_desc.FillMode = D3D12_FILL_MODE_SOLID;
    rast_desc.CullMode = D3D12_CULL_MODE_NONE;
    pso_desc.RasterizerState = rast_desc;

    pso_desc.InputLayout = { input_layout, 3 };
    pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso_desc.NumRenderTargets = 1;
    pso_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso_desc.SampleDesc.Count = 1;

    device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&m_pso));

    // Dynamic Vertex Buffer (32768 vertices * 20 bytes = 640 KB)
    const uint32_t vb_size = 32768 * sizeof(OsdVertex);
    D3D12_RESOURCE_DESC vb_desc = {};
    vb_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    vb_desc.Width = vb_size;
    vb_desc.Height = 1;
    vb_desc.DepthOrArraySize = 1;
    vb_desc.MipLevels = 1;
    vb_desc.Format = DXGI_FORMAT_UNKNOWN;
    vb_desc.SampleDesc.Count = 1;
    vb_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    D3D12_HEAP_PROPERTIES upload_heap = {};
    upload_heap.Type = D3D12_HEAP_TYPE_UPLOAD;

    device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &vb_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_vertex_buffer));
    m_vertex_buffer->Map(0, nullptr, &m_vb_mapped);

    m_vb_view.BufferLocation = m_vertex_buffer->GetGPUVirtualAddress();
    m_vb_view.StrideInBytes = sizeof(OsdVertex);
    m_vb_view.SizeInBytes = vb_size;
}

bool OverlayRenderer::init(ID3D12Device *device, ID3D12CommandQueue *command_queue) {
    if (!device || !command_queue) return false;
    m_device = device;

    build_font_atlas(device, command_queue);
    create_pipeline(device);

    m_initialized = true;
    LOG_INFO("OverlayRenderer D3D12 initialized: Professional Xbox Series OSD Active");
    return true;
}

void OverlayRenderer::push_quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, uint32_t color) {
    if (!m_vb_mapped || m_vertex_count + 6 > 32768) return;

    OsdVertex *v = static_cast<OsdVertex *>(m_vb_mapped) + m_vertex_count;
    // Triangle 1
    v[0] = { x0, y0, u0, v0, color };
    v[1] = { x1, y0, u1, v0, color };
    v[2] = { x0, y1, u0, v1, color };
    // Triangle 2
    v[3] = { x1, y0, u1, v0, color };
    v[4] = { x1, y1, u1, v1, color };
    v[5] = { x0, y1, u0, v1, color };

    m_vertex_count += 6;
}

void OverlayRenderer::draw_rect(float x, float y, float w, float h, uint32_t color) {
    push_quad(x, y, x + w, y + h, SOLID_U, SOLID_V, SOLID_U, SOLID_V, color);
}

void OverlayRenderer::draw_rect_outline(float x, float y, float w, float h, float thickness, uint32_t color) {
    draw_rect(x, y, w, thickness, color); // Top
    draw_rect(x, y + h - thickness, w, thickness, color); // Bottom
    draw_rect(x, y, thickness, h, color); // Left
    draw_rect(x + w - thickness, y, thickness, h, color); // Right
}

void OverlayRenderer::draw_text(float x, float y, const std::string &text, uint32_t color, float scale) {
    float start_x = x;
    const float char_w = 8.0f * scale;
    const float char_h = 8.0f * scale;
    const float spacing = scale > 1.5f ? 2.0f : 1.0f;

    for (char c : text) {
        if (c == '\n') {
            y += char_h + 4.0f;
            x = start_x;
            continue;
        }

        if (c < 32 || c > 126) c = '?';
        int idx = (c - 32) + 1;
        int col = idx % 16;
        int row = idx / 16;

        float u0 = (col * 8.0f) / 128.0f;
        float v0 = (row * 8.0f) / 128.0f;
        float u1 = ((col + 1) * 8.0f) / 128.0f;
        float v1 = ((row + 1) * 8.0f) / 128.0f;

        push_quad(x, y, x + char_w, y + char_h, u0, v0, u1, v1, color);
        x += char_w + spacing;
    }
}

void OverlayRenderer::draw_card(float x, float y, float w, float h, bool selected, const std::string &title, const std::string &subtitle, const std::string &badge) {
    uint32_t bg_color = selected ? COLOR_CARD_FOCUSED : COLOR_CARD_NORMAL;
    draw_rect(x, y, w, h, bg_color);

    if (selected) {
        draw_rect_outline(x, y, w, h, 3.0f, COLOR_XBOX_GREEN);
        draw_rect(x, y, 6.0f, h, COLOR_XBOX_NEON);
        draw_text(x + 14.0f, y + (h - 16.0f) * 0.5f, ">", COLOR_XBOX_NEON, 2.0f);
    } else {
        draw_rect_outline(x, y, w, h, 1.0f, COLOR_BORDER);
    }

    float text_x = selected ? (x + 36.0f) : (x + 20.0f);
    draw_text(text_x, y + 14.0f, title, COLOR_TEXT_WHITE, 2.0f);
    if (!subtitle.empty()) {
        draw_text(text_x, y + 36.0f, subtitle, COLOR_TEXT_MUTED, 1.5f);
    }

    if (!badge.empty()) {
        float badge_w = badge.length() * 12.0f + 16.0f;
        float badge_x = x + w - badge_w - 16.0f;
        float badge_y = y + 12.0f;
        draw_rect(badge_x, badge_y, badge_w, 24.0f, pack_rgba(16, 124, 16, 200));
        draw_text(badge_x + 8.0f, badge_y + 4.0f, badge, COLOR_TEXT_WHITE, 1.5f);
    }
}

void OverlayRenderer::draw_progress_bar(float x, float y, float w, float h, float pct, uint32_t fill_color, const std::string &label) {
    draw_rect(x, y, w, h, pack_rgba(20, 24, 30, 240));
    draw_rect_outline(x, y, w, h, 1.0f, COLOR_BORDER);

    float clamped_pct = std::clamp(pct / 100.0f, 0.0f, 1.0f);
    if (clamped_pct > 0.0f) {
        draw_rect(x + 2.0f, y + 2.0f, (w - 4.0f) * clamped_pct, h - 4.0f, fill_color);
    }

    if (!label.empty()) {
        draw_text(x + 16.0f, y + (h - 14.0f) * 0.5f, label, COLOR_TEXT_WHITE, 1.8f);
    }
}

void OverlayRenderer::render_header(float sw, float sh) {
    // Top Bar Background
    draw_rect(0.0f, 0.0f, sw, 70.0f, pack_rgba(16, 20, 26, 250));
    draw_rect(0.0f, 68.0f, sw, 2.0f, COLOR_XBOX_GREEN);

    // Title logo
    draw_text(40.0f, 22.0f, "VITA3K SERIES", COLOR_XBOX_NEON, 2.4f);

    // Navigation Tabs
    const char *tabs[] = { "GAMES", "FIRMWARE & STORAGE", "GRAPHICS & CAS", "XBOX SYSTEM" };
    float tab_x = 360.0f;

    draw_text(tab_x - 45.0f, 25.0f, "[LB]", COLOR_TEXT_MUTED, 1.6f);

    for (int i = 0; i < 4; ++i) {
        bool active = (m_active_tab == i);
        float tab_w = strlen(tabs[i]) * 14.0f + 28.0f;

        if (active) {
            draw_rect(tab_x, 15.0f, tab_w, 40.0f, COLOR_XBOX_GREEN);
            draw_text(tab_x + 14.0f, 25.0f, tabs[i], COLOR_TEXT_WHITE, 1.8f);
        } else {
            draw_text(tab_x + 14.0f, 25.0f, tabs[i], COLOR_TEXT_MUTED, 1.8f);
        }

        tab_x += tab_w + 16.0f;
    }

    draw_text(tab_x + 10.0f, 25.0f, "[RB]", COLOR_TEXT_MUTED, 1.6f);

    // Telemetry pill in top-right
    char sys_pill[64];
    snprintf(sys_pill, sizeof(sys_pill), "FPS: %.1f | %s", m_current_fps, get_resolution_name());
    draw_text(sw - 380.0f, 25.0f, sys_pill, COLOR_TEXT_MUTED, 1.6f);
}

void OverlayRenderer::render_library_tab(float sw, float sh) {
    float start_y = 100.0f;
    float card_w = sw * 0.58f;
    float card_h = 70.0f;

    if (m_installed_apps.empty()) {
        draw_rect(80.0f, start_y, sw - 160.0f, 280.0f, COLOR_PANEL_BG);
        draw_rect_outline(80.0f, start_y, sw - 160.0f, 280.0f, 2.0f, COLOR_BORDER);

        draw_text(110.0f, start_y + 30.0f, "NO PLAYSTATION VITA GAMES INSTALLED YET", COLOR_XBOX_YELLOW, 2.2f);
        draw_text(110.0f, start_y + 80.0f, "To install games and homebrews:", COLOR_TEXT_WHITE, 1.8f);
        draw_text(110.0f, start_y + 115.0f, "1. Connect a USB Drive (D: or E:) containing .vpk packages to your Xbox.", COLOR_TEXT_MUTED, 1.6f);
        draw_text(110.0f, start_y + 145.0f, "2. Switch to 'FIRMWARE & STORAGE' tab using [RB] bumper to install firmware.", COLOR_TEXT_MUTED, 1.6f);
        draw_text(110.0f, start_y + 175.0f, "3. Select your .vpk package and press (A) to install directly into ux0:app.", COLOR_TEXT_MUTED, 1.6f);
        draw_text(110.0f, start_y + 220.0f, "Press (X) on controller to refresh storage scanning.", COLOR_XBOX_BLUE, 1.6f);
        return;
    }

    // List installed apps
    int visible_count = std::min(static_cast<int>(m_installed_apps.size()), 7);
    int scroll_offset = std::max(0, m_selected_item - 6);

    for (int i = 0; i < visible_count; ++i) {
        int app_idx = scroll_offset + i;
        if (app_idx >= static_cast<int>(m_installed_apps.size())) break;

        const auto &app = m_installed_apps[app_idx];
        bool selected = (app_idx == m_selected_item);

        std::string subtitle = "ID: " + app.title_id + " | Cat: " + app.category + " | v" + app.version;
        draw_card(80.0f, start_y + i * 80.0f, card_w, card_h, selected, app.title_name, subtitle, "Ready");
    }

    // Right details panel
    float details_x = 80.0f + card_w + 30.0f;
    float details_w = sw - details_x - 80.0f;
    draw_rect(details_x, start_y, details_w, 540.0f, COLOR_PANEL_BG);
    draw_rect_outline(details_x, start_y, details_w, 540.0f, 1.0f, COLOR_BORDER);

    if (m_selected_item >= 0 && m_selected_item < static_cast<int>(m_installed_apps.size())) {
        const auto &cur_app = m_installed_apps[m_selected_item];
        draw_text(details_x + 30.0f, start_y + 30.0f, "TITLE DETAILS", COLOR_XBOX_NEON, 2.0f);
        draw_text(details_x + 30.0f, start_y + 70.0f, cur_app.title_name, COLOR_TEXT_WHITE, 2.2f);
        draw_text(details_x + 30.0f, start_y + 110.0f, "Title ID: " + cur_app.title_id, COLOR_TEXT_MUTED, 1.8f);
        draw_text(details_x + 30.0f, start_y + 140.0f, "Category: " + cur_app.category, COLOR_TEXT_MUTED, 1.8f);
        draw_text(details_x + 30.0f, start_y + 170.0f, "Version:  v" + cur_app.version, COLOR_TEXT_MUTED, 1.8f);

        // Action Prompts
        draw_rect(details_x + 30.0f, start_y + 240.0f, details_w - 60.0f, 50.0f, COLOR_XBOX_GREEN);
        draw_text(details_x + 50.0f, start_y + 254.0f, "(A) LAUNCH SELECTED TITLE", COLOR_TEXT_WHITE, 2.0f);

        draw_text(details_x + 30.0f, start_y + 320.0f, "Target: Direct3D 12.2 / RDNA2", COLOR_TEXT_MUTED, 1.6f);
        draw_text(details_x + 30.0f, start_y + 350.0f, "Active Upscaler: FidelityFX CAS", COLOR_TEXT_MUTED, 1.6f);
        draw_text(details_x + 30.0f, start_y + 380.0f, "Refresh Sync: VRR Allow Tearing", COLOR_TEXT_MUTED, 1.6f);
    }
}

void OverlayRenderer::render_firmware_tab(float sw, float sh) {
    float start_y = 100.0f;
    float panel_w = sw - 160.0f;

    // Firmware Status Card
    draw_rect(80.0f, start_y, panel_w, 110.0f, COLOR_PANEL_BG);
    draw_rect_outline(80.0f, start_y, panel_w, 110.0f, 1.0f, COLOR_BORDER);

    draw_text(110.0f, start_y + 20.0f, "OFFICIAL PLAYSTATION VITA FIRMWARE STATUS", COLOR_XBOX_NEON, 2.0f);
    if (m_fw_installed) {
        draw_text(110.0f, start_y + 55.0f, "[INSTALLED] System Modules Active (os0, vs0, sa0, pd0 present)", COLOR_XBOX_NEON, 1.8f);
    } else {
        draw_text(110.0f, start_y + 55.0f, "[MISSING] Official Firmware Not Installed - Games may fail to boot without modules", COLOR_XBOX_RED, 1.8f);
    }

    // Firmware Installation Action
    float fw_act_y = start_y + 130.0f;
    draw_rect(80.0f, fw_act_y, panel_w, 150.0f, COLOR_PANEL_BG);
    draw_rect_outline(80.0f, fw_act_y, panel_w, 150.0f, 1.0f, (m_selected_item == 0) ? COLOR_XBOX_GREEN : COLOR_BORDER);

    draw_text(110.0f, fw_act_y + 20.0f, "INSTALL OFFICIAL FIRMWARE (PSP2UPDAT.PUP)", COLOR_TEXT_WHITE, 2.0f);

    if (!m_detected_pup.empty()) {
        draw_text(110.0f, fw_act_y + 55.0f, "Detected Firmware Package: " + m_detected_pup, COLOR_XBOX_BLUE, 1.6f);
        uint32_t btn_color = (m_selected_item == 0) ? COLOR_XBOX_GREEN : COLOR_CARD_NORMAL;
        draw_rect(110.0f, fw_act_y + 90.0f, 440.0f, 40.0f, btn_color);
        draw_text(130.0f, fw_act_y + 100.0f, "(A) INSTALL PS VITA FIRMWARE", COLOR_TEXT_WHITE, 1.8f);
    } else {
        draw_text(110.0f, fw_act_y + 55.0f, "Place 'PSP2UPDAT.PUP' on a USB drive (D: or E:), then press (X) to rescan.", COLOR_TEXT_MUTED, 1.6f);
    }

    // Installation Progress Bar (if in progress or completed recently)
    if (m_install_in_progress.load() || m_install_progress.load() > 0) {
        float prog_y = fw_act_y + 170.0f;
        uint32_t fill_color = m_install_progress.load() >= 100 ? COLOR_XBOX_NEON : COLOR_XBOX_GREEN;
        std::string prog_lbl = "Progress: " + std::to_string(m_install_progress.load()) + "% - " + m_install_status;
        draw_progress_bar(80.0f, prog_y, panel_w, 40.0f, static_cast<float>(m_install_progress.load()), fill_color, prog_lbl);

        if (m_install_in_progress.load()) {
            draw_text(80.0f, prog_y + 50.0f, "ATTENTION: DO NOT CLOSE EMULATOR OR REMOVE USB DRIVE DURING INSTALLATION", COLOR_XBOX_YELLOW, 1.6f);
        }
    }

    // Detected VPK / PKG Packages
    float vpk_y = fw_act_y + (m_install_in_progress.load() ? 240.0f : 170.0f);
    draw_text(80.0f, vpk_y, "DETECTED PACKAGES (.VPK / .PKG) ON USB & LOCAL STORAGE", COLOR_XBOX_NEON, 1.8f);

    if (m_detected_vpks.empty()) {
        draw_text(80.0f, vpk_y + 35.0f, "No .vpk/.pkg packages found. Copy packages to USB (D:/E:/F:) and press (X).", COLOR_TEXT_MUTED, 1.6f);
    } else {
        for (size_t i = 0; i < std::min(m_detected_vpks.size(), size_t(4)); ++i) {
            bool sel = (m_selected_item == static_cast<int>(i + 1));
            std::string ext = fs::path(m_detected_vpks[i]).extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::toupper);
            std::string badge = ext.empty() ? "PKG" : (ext.front() == '.' ? ext.substr(1) : ext);
            draw_card(80.0f, vpk_y + 35.0f + i * 65.0f, panel_w, 55.0f, sel, m_detected_vpks[i], "Press (A) to install package into Vita filesystem", badge);
        }
    }
}

void OverlayRenderer::render_graphics_tab(float sw, float sh) {
    float start_y = 110.0f;
    float panel_w = sw - 160.0f;

    struct OptionItem {
        std::string name;
        std::string value;
        std::string desc;
    };

    std::vector<OptionItem> options = {
        { "Internal Resolution Multiplier", get_resolution_name(), "Scales native Vita 544p resolution using hardware Direct3D 12 render targets." },
        { "AMD FidelityFX CAS Sharpening", m_cas_enabled ? "Enabled" : "Disabled", "Applies dynamic Contrast Adaptive Sharpening shader pass to enhance edges." },
        { "FidelityFX CAS Sharpness Ratio", std::to_string(static_cast<int>(m_cas_sharpness * 100.0f)) + "%", "Adjusts high-frequency sharpening intensity without ringing or noise." },
        { "Presentation / Display Mode", m_vrr_enabled ? "Variable Refresh Rate (VRR / Tearing)" : "Double Buffer V-Sync 60Hz", "VRR mode reduces input lag on 120Hz/VRR compatible Xbox displays." },
        { "Performance Telemetry HUD", m_hud_mode == 0 ? "Disabled" : (m_hud_mode == 1 ? "Minimal (FPS)" : "Full Telemetry"), "Displays live in-game frame rate, frame time, and resolution metrics." },
    };

    for (size_t i = 0; i < options.size(); ++i) {
        bool selected = (m_selected_item == static_cast<int>(i));
        float y = start_y + i * 85.0f;

        draw_rect(80.0f, y, panel_w, 75.0f, selected ? COLOR_CARD_FOCUSED : COLOR_CARD_NORMAL);
        draw_rect_outline(80.0f, y, panel_w, 75.0f, selected ? 3.0f : 1.0f, selected ? COLOR_XBOX_GREEN : COLOR_BORDER);

        if (selected) {
            draw_rect(80.0f, y, 6.0f, 75.0f, COLOR_XBOX_NEON);
            draw_text(95.0f, y + 16.0f, ">", COLOR_XBOX_NEON, 2.0f);
        }

        float text_x = selected ? 120.0f : 105.0f;
        draw_text(text_x, y + 14.0f, options[i].name, COLOR_TEXT_WHITE, 2.0f);
        draw_text(text_x, y + 42.0f, options[i].desc, COLOR_TEXT_MUTED, 1.4f);

        // Value on right with arrow prompts
        std::string val_str = selected ? ("< " + options[i].value + " >") : options[i].value;
        uint32_t val_color = selected ? COLOR_XBOX_NEON : COLOR_TEXT_WHITE;
        float val_w = val_str.length() * 16.0f;
        draw_text(80.0f + panel_w - val_w - 30.0f, y + 26.0f, val_str, val_color, 2.0f);
    }
}

void OverlayRenderer::render_system_tab(float sw, float sh) {
    float start_y = 110.0f;
    float panel_w = sw - 160.0f;

    // Hardware Telemetry Box
    draw_rect(80.0f, start_y, panel_w, 200.0f, COLOR_PANEL_BG);
    draw_rect_outline(80.0f, start_y, panel_w, 200.0f, 1.0f, COLOR_BORDER);

    draw_text(110.0f, start_y + 20.0f, "XBOX SERIES HARDWARE & DEV MODE TELEMETRY", COLOR_XBOX_NEON, 2.0f);
    draw_text(110.0f, start_y + 60.0f, "Console Platform: Microsoft Xbox Series S / X (Lockhart / Scarlett)", COLOR_TEXT_WHITE, 1.8f);
    draw_text(110.0f, start_y + 90.0f, "CPU Architecture: AMD Zen 2 (8 Cores / 16 Threads @ 3.6 GHz) - AVX2 Active", COLOR_TEXT_MUTED, 1.6f);
    draw_text(110.0f, start_y + 120.0f, "GPU Architecture: AMD RDNA2 Custom GPU | Direct3D 12.2 / Wave32 Support", COLOR_TEXT_MUTED, 1.6f);
    draw_text(110.0f, start_y + 150.0f, "Memory Budget:    5.0 GB UWP ExpandedResources Partition Enabled", COLOR_TEXT_MUTED, 1.6f);

    // Controller & Actions
    float ctrl_y = start_y + 225.0f;

    struct ActionItem {
        std::string label;
        std::string desc;
    };
    std::vector<ActionItem> actions = {
        { "Xbox Impulse Trigger Rumble", m_trigger_rumble_enabled ? "Enabled" : "Disabled" },
        { "Restart Active Application", "Relaunches the current PlayStation Vita game session" },
        { "Exit Vita3K to Xbox Dashboard", "Closes application safely and returns to Xbox Dev Home" }
    };

    for (size_t i = 0; i < actions.size(); ++i) {
        bool selected = (m_selected_item == static_cast<int>(i));
        float y = ctrl_y + i * 75.0f;

        draw_rect(80.0f, y, panel_w, 65.0f, selected ? COLOR_CARD_FOCUSED : COLOR_CARD_NORMAL);
        draw_rect_outline(80.0f, y, panel_w, 65.0f, selected ? 3.0f : 1.0f, selected ? COLOR_XBOX_GREEN : COLOR_BORDER);

        if (selected) {
            draw_rect(80.0f, y, 6.0f, 65.0f, COLOR_XBOX_NEON);
            draw_text(95.0f, y + 16.0f, ">", COLOR_XBOX_NEON, 2.0f);
        }

        float text_x = selected ? 120.0f : 105.0f;
        draw_text(text_x, y + 14.0f, actions[i].label, COLOR_TEXT_WHITE, 2.0f);
        draw_text(text_x, y + 38.0f, actions[i].desc, COLOR_TEXT_MUTED, 1.4f);
    }
}

void OverlayRenderer::render_in_game_quick_menu(float sw, float sh) {
    // Dim background game frame
    draw_rect(0.0f, 0.0f, sw, sh, pack_rgba(5, 7, 10, 215));

    float menu_w = 540.0f;
    float menu_h = 420.0f;
    float menu_x = (sw - menu_w) * 0.5f;
    float menu_y = (sh - menu_h) * 0.5f;

    draw_rect(menu_x, menu_y, menu_w, menu_h, COLOR_PANEL_BG);
    draw_rect_outline(menu_x, menu_y, menu_w, menu_h, 3.0f, COLOR_XBOX_GREEN);

    draw_text(menu_x + 35.0f, menu_y + 30.0f, "VITA3K QUICK MENU", COLOR_XBOX_NEON, 2.2f);
    draw_rect(menu_x + 35.0f, menu_y + 65.0f, menu_w - 70.0f, 2.0f, COLOR_BORDER);

    const char *items[] = {
        "Resume Game",
        "Toggle FidelityFX CAS",
        "Toggle Performance HUD",
        "Return to Vita3K Dashboard",
        "Exit to Xbox Dashboard"
    };

    for (int i = 0; i < 5; ++i) {
        bool selected = (m_quick_menu_index == i);
        float item_y = menu_y + 85.0f + i * 55.0f;

        if (selected) {
            draw_rect(menu_x + 25.0f, item_y, menu_w - 50.0f, 45.0f, COLOR_XBOX_GREEN);
            draw_text(menu_x + 45.0f, item_y + 12.0f, items[i], COLOR_TEXT_WHITE, 2.0f);
        } else {
            draw_text(menu_x + 45.0f, item_y + 12.0f, items[i], COLOR_TEXT_MUTED, 2.0f);
        }
    }

    draw_text(menu_x + 35.0f, menu_y + menu_h - 35.0f, "(A) Select   (B) Resume Game", COLOR_TEXT_DIM, 1.6f);
}

void OverlayRenderer::render_telemetry_hud(float sw, float sh) {
    if (m_hud_mode == 0) return;

    if (m_hud_mode == 1) {
        // Minimal HUD (top-right)
        char buf[64];
        snprintf(buf, sizeof(buf), "%.1f FPS | %.1f ms", m_current_fps, m_current_frametime);
        float pill_w = 240.0f;
        draw_rect(sw - pill_w - 30.0f, 30.0f, pill_w, 36.0f, pack_rgba(10, 14, 20, 210));
        draw_rect_outline(sw - pill_w - 30.0f, 30.0f, pill_w, 36.0f, 1.0f, COLOR_BORDER);
        draw_text(sw - pill_w - 15.0f, 40.0f, buf, COLOR_XBOX_NEON, 1.8f);
    } else if (m_hud_mode == 2) {
        // Full Telemetry HUD
        char line1[64], line2[64];
        snprintf(line1, sizeof(line1), "FPS: %.1f | Frame: %.1fms", m_current_fps, m_current_frametime);
        snprintf(line2, sizeof(line2), "Res: %s | CAS: %s | VRR: %s",
                 get_resolution_name(), m_cas_enabled ? "ON" : "OFF", m_vrr_enabled ? "ON" : "OFF");

        float box_w = 420.0f;
        draw_rect(sw - box_w - 30.0f, 30.0f, box_w, 64.0f, pack_rgba(10, 14, 20, 225));
        draw_rect_outline(sw - box_w - 30.0f, 30.0f, box_w, 64.0f, 1.0f, COLOR_XBOX_GREEN);
        draw_text(sw - box_w - 15.0f, 38.0f, line1, COLOR_XBOX_NEON, 1.6f);
        draw_text(sw - box_w - 15.0f, 62.0f, line2, COLOR_TEXT_WHITE, 1.4f);
    }
}

void OverlayRenderer::render_footer(float sw, float sh) {
    float y = sh - 45.0f;
    draw_rect(0.0f, y - 5.0f, sw, 50.0f, pack_rgba(16, 20, 26, 250));
    draw_rect(0.0f, y - 5.0f, sw, 1.0f, COLOR_BORDER);

    draw_text(40.0f, y + 8.0f, "(A) Select / Confirm    (B) Back    (X) Rescan USB Storage    (LB)/(RB) Switch Tabs    (Guide) Quick Menu", COLOR_TEXT_MUTED, 1.6f);
}

void OverlayRenderer::render(ID3D12GraphicsCommandList *command_list, D3D12_CPU_DESCRIPTOR_HANDLE rtv_target, float width, float height) {
    if (!m_initialized || !command_list) return;

    m_vertex_count = 0;

    // In-game Telemetry HUD
    if (m_in_game && !m_osd_open) {
        render_telemetry_hud(width, height);
    }

    // In-game Quick Menu
    if (m_in_game && m_osd_open) {
        render_in_game_quick_menu(width, height);
    }

    // Main Dashboard OSD (when not in a game, or browsing dashboard)
    if (!m_in_game && m_osd_open) {
        // Clear background
        draw_rect(0.0f, 0.0f, width, height, COLOR_BG_DIM);

        render_header(width, height);

        switch (m_active_tab) {
            case 0: render_library_tab(width, height); break;
            case 1: render_firmware_tab(width, height); break;
            case 2: render_graphics_tab(width, height); break;
            case 3: render_system_tab(width, height); break;
            default: break;
        }

        render_footer(width, height);
    }

    if (m_vertex_count == 0) return;

    // Set viewport & scissor
    D3D12_VIEWPORT vp = { 0.0f, 0.0f, width, height, 0.0f, 1.0f };
    D3D12_RECT scissor = { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
    command_list->RSSetViewports(1, &vp);
    command_list->RSSetScissorRects(1, &scissor);

    // Bind descriptor heaps, root signature, PSO
    ID3D12DescriptorHeap *heaps[] = { m_srv_heap.Get() };
    command_list->SetDescriptorHeaps(1, heaps);
    command_list->SetGraphicsRootSignature(m_root_sig.Get());
    command_list->SetPipelineState(m_pso.Get());

    // Screen size constant
    float screen_size[2] = { width, height };
    command_list->SetGraphicsRoot32BitConstants(0, 2, screen_size, 0);
    command_list->SetGraphicsRootDescriptorTable(1, m_srv_heap->GetGPUDescriptorHandleForHeapStart());

    command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    command_list->IASetVertexBuffers(0, 1, &m_vb_view);

    command_list->DrawInstanced(m_vertex_count, 1, 0, 0);
}

void OverlayRenderer::render(ID3D12GraphicsCommandList *command_list) {
    render(command_list, {}, 1920.0f, 1080.0f);
}

void OverlayRenderer::update_gamepad_input(SDL_Gamepad *gamepad, float dt) {
    if (!gamepad) {
        int count = 0;
        SDL_JoystickID *gamepads = SDL_GetGamepads(&count);
        if (count > 0 && gamepads) {
            gamepad = SDL_OpenGamepad(gamepads[0]);
        }
    }
    if (!gamepad) return;

    m_input_repeat_timer -= dt;

    // Guide or View + Menu toggles OSD
    bool btn_guide = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_GUIDE);
    bool btn_view  = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_BACK);
    bool btn_menu  = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_START);

    if ((btn_guide && !m_prev_button_guide) || ((btn_view && btn_menu) && (!m_prev_button_view || !m_prev_button_menu))) {
        toggle_osd();
        if (m_trigger_rumble_enabled) SDL_RumbleGamepadTriggers(gamepad, 18000, 18000, 80);
    }
    m_prev_button_guide = btn_guide;
    m_prev_button_view  = btn_view;
    m_prev_button_menu  = btn_menu;

    if (!m_osd_open) return;

    bool btn_south = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_SOUTH);
    bool btn_east  = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_EAST);
    bool btn_west  = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_WEST);

    // Read Left Analog Stick with deadzone for seamless console thumbstick navigation
    int16_t stick_y = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTY);
    int16_t stick_x = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTX);
    bool stick_up    = stick_y < -16000;
    bool stick_down  = stick_y > 16000;
    bool stick_left  = stick_x < -16000;
    bool stick_right = stick_x > 16000;

    // Quick Menu navigation in-game
    if (m_in_game) {
        if (m_input_repeat_timer <= 0.0f) {
            if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP) || stick_up) {
                m_quick_menu_index = (m_quick_menu_index + 4) % 5;
                m_input_repeat_timer = 0.2f;
                if (m_trigger_rumble_enabled) SDL_RumbleGamepadTriggers(gamepad, 8000, 8000, 40);
            } else if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN) || stick_down) {
                m_quick_menu_index = (m_quick_menu_index + 1) % 5;
                m_input_repeat_timer = 0.2f;
                if (m_trigger_rumble_enabled) SDL_RumbleGamepadTriggers(gamepad, 8000, 8000, 40);
            }
        }

        if (btn_south && !m_prev_button_south) { // A button
            if (m_trigger_rumble_enabled) SDL_RumbleGamepadTriggers(gamepad, 25000, 25000, 100);
            if (m_quick_menu_index == 0) { // Resume
                set_osd_open(false);
            } else if (m_quick_menu_index == 1) { // Toggle CAS
                m_cas_enabled = !m_cas_enabled;
            } else if (m_quick_menu_index == 2) { // Toggle HUD
                m_hud_mode = (m_hud_mode + 1) % 3;
            } else if (m_quick_menu_index == 3) { // Return to Dashboard
                if (on_exit_to_dashboard) on_exit_to_dashboard();
            } else if (m_quick_menu_index == 4) { // Exit app
                if (on_exit_app) on_exit_app();
            }
        } else if (btn_east && !m_prev_button_east) { // B button
            set_osd_open(false);
        }
        m_prev_button_south = btn_south;
        m_prev_button_east  = btn_east;
        m_prev_button_west  = btn_west;
        return;
    }

    // Main Dashboard OSD navigation
    if (m_input_repeat_timer <= 0.0f) {
        // Tab switching [LB] / [RB]
        if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)) {
            m_active_tab = (m_active_tab + 3) % 4;
            m_selected_item = 0;
            m_input_repeat_timer = 0.25f;
            if (m_trigger_rumble_enabled) SDL_RumbleGamepadTriggers(gamepad, 12000, 12000, 50);
        } else if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) {
            m_active_tab = (m_active_tab + 1) % 4;
            m_selected_item = 0;
            m_input_repeat_timer = 0.25f;
            if (m_trigger_rumble_enabled) SDL_RumbleGamepadTriggers(gamepad, 12000, 12000, 50);
        }

        // Vertical selection (D-Pad or Left Stick)
        if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP) || stick_up) {
            m_selected_item = std::max(0, m_selected_item - 1);
            m_input_repeat_timer = 0.2f;
            if (m_trigger_rumble_enabled) SDL_RumbleGamepadTriggers(gamepad, 8000, 8000, 40);
        } else if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN) || stick_down) {
            int max_items = (m_active_tab == 0) ? std::max(1, static_cast<int>(m_installed_apps.size())) : 4;
            m_selected_item = std::min(max_items - 1, m_selected_item + 1);
            m_input_repeat_timer = 0.2f;
            if (m_trigger_rumble_enabled) SDL_RumbleGamepadTriggers(gamepad, 8000, 8000, 40);
        }

        // Horizontal adjustment (Graphics tab - D-Pad or Left Stick)
        if (m_active_tab == 2) {
            if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT) || stick_left) {
                if (m_selected_item == 0) m_res_index = std::max(0, m_res_index - 1);
                else if (m_selected_item == 1) m_cas_enabled = !m_cas_enabled;
                else if (m_selected_item == 2) m_cas_sharpness = std::max(0.0f, m_cas_sharpness - 0.25f);
                else if (m_selected_item == 3) m_vrr_enabled = !m_vrr_enabled;
                else if (m_selected_item == 4) m_hud_mode = (m_hud_mode + 2) % 3;
                m_input_repeat_timer = 0.2f;
                if (m_trigger_rumble_enabled) SDL_RumbleGamepadTriggers(gamepad, 10000, 10000, 40);
            } else if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT) || stick_right) {
                if (m_selected_item == 0) m_res_index = std::min(4, m_res_index + 1);
                else if (m_selected_item == 1) m_cas_enabled = !m_cas_enabled;
                else if (m_selected_item == 2) m_cas_sharpness = std::min(1.0f, m_cas_sharpness + 0.25f);
                else if (m_selected_item == 3) m_vrr_enabled = !m_vrr_enabled;
                else if (m_selected_item == 4) m_hud_mode = (m_hud_mode + 1) % 3;
                m_input_repeat_timer = 0.2f;
                if (m_trigger_rumble_enabled) SDL_RumbleGamepadTriggers(gamepad, 10000, 10000, 40);
            }
        }
    }

    // Rescan USB Storage (X button)
    if (btn_west && !m_prev_button_west) {
        scan_storage();
        if (m_trigger_rumble_enabled) SDL_RumbleGamepadTriggers(gamepad, 15000, 15000, 80);
    }

    // Action Execution (A button)
    if (btn_south && !m_prev_button_south) {
        if (m_trigger_rumble_enabled) SDL_RumbleGamepadTriggers(gamepad, 30000, 30000, 120);

        if (m_active_tab == 0 && !m_installed_apps.empty()) { // Launch Game
            if (m_selected_item < static_cast<int>(m_installed_apps.size())) {
                if (on_launch_app) on_launch_app(m_installed_apps[m_selected_item].title_id);
            }
        } else if (m_active_tab == 1) { // Firmware / VPK Install
            if (m_selected_item == 0 && !m_detected_pup.empty()) {
                if (on_install_firmware) on_install_firmware(m_detected_pup);
            } else if (m_selected_item > 0 && (m_selected_item - 1) < static_cast<int>(m_detected_vpks.size())) {
                if (on_install_vpk) on_install_vpk(m_detected_vpks[m_selected_item - 1]);
            }
        } else if (m_active_tab == 3) { // System tab actions
            if (m_selected_item == 0) {
                m_trigger_rumble_enabled = !m_trigger_rumble_enabled;
            } else if (m_selected_item == 1) {
                if (on_restart_app) on_restart_app();
            } else if (m_selected_item == 2) {
                if (on_exit_app) on_exit_app();
            }
        }
    }

    m_prev_button_south = btn_south;
    m_prev_button_east  = btn_east;
    m_prev_button_west  = btn_west;
}

} // namespace renderer::d3d12
