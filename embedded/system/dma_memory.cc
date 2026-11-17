#include <drivers/dma_memory.h>

#if !defined(CONFIG_DMA_CACHED_MEMORY)
extern "C" {
extern char __ram_region_start[];
extern char __ram_region_end[];
}

namespace hal {

Status dma_prepare(const void* buffer, size_t size, DmaAccess)
{
    const DmaRegion region {reinterpret_cast<uintptr_t>(__ram_region_start),
                            reinterpret_cast<uintptr_t>(__ram_region_end), 1U};
    return region.accepts(buffer, size) ? Status::Ok : Status::InvalidArgument;
}

Status dma_complete(const void* buffer, size_t size, DmaAccess access)
{
    return dma_prepare(buffer, size, access);
}

} // namespace hal
#endif
