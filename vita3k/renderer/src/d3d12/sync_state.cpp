// Vita3K emulator project - Xbox Series S UWP D3D12 Port
#include <renderer/d3d12/functions.h>
#include <renderer/d3d12/state.h>
#include <renderer/d3d12/types.h>
#include <renderer/functions.h>

#include <util/log.h>

namespace renderer::d3d12 {

void signal_sync_object(D3D12State &state, SceGxmSyncObject *sync_object, uint32_t timestamp) {
    if (!sync_object) return;
    renderer::subject_done(sync_object, timestamp);
}

} // namespace renderer::d3d12
