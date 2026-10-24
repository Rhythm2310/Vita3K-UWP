add_library(
	renderer
	STATIC
	src/d3d12/allocator.cpp
	src/d3d12/context.cpp
	src/d3d12/creation.cpp
	src/d3d12/pipeline_cache.cpp
	src/d3d12/renderer.cpp
	src/d3d12/scene.cpp
	src/d3d12/overlay_renderer.cpp
	src/d3d12/screen_renderer.cpp
	src/d3d12/surface_cache.cpp
	src/d3d12/sync_state.cpp
	src/d3d12/texture.cpp

	src/texture/cache.cpp
	src/texture/format.cpp
	src/texture/palette.cpp
	src/texture/pvrt-dec.cpp
	src/texture/replacement.cpp
	src/texture/yuv.cpp

	src/batch.cpp
	src/creation.cpp
	src/renderer.cpp
	src/scene.cpp
	src/shaders.cpp
	src/state_set.cpp
	src/sync.cpp
	src/transfer.cpp
)

target_include_directories(renderer PUBLIC include)
target_link_libraries(renderer PUBLIC display mem stb shader threads config util overlay)
target_link_libraries(renderer PRIVATE dialog ddspp SDL3::SDL3 stb ffmpeg xxHash::xxhash concurrentqueue d3d12 dxgi dxguid d3dcompiler d3d12ma)
