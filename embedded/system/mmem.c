#include <mmem.h>
#include <lwmem/lwmem.h>
#include <sys/util.h>
#include <init.h>
#include <devicetree.h>


#define DT_DRV_COMPAT msdk_mempool

#define MEMPOOL_BUFFER_NAME(inst) \
    _CONCAT_3(mempool_buffer_, DT_INST_STRING_UNQUOTED(inst, label), _##inst)

/* 为每个内存池实例生成定义 */
#define MEMPOOL_DEFINE(inst) \
    Z_GENERIC_SECTION(DT_INST_STRING_UNQUOTED(inst, section_name)) \
    static uint8_t MEMPOOL_BUFFER_NAME(inst)[DT_INST_PROP(inst, size)];

DT_INST_FOREACH_STATUS_OKAY(MEMPOOL_DEFINE)

static
lwmem_region_t mem_regions[] = {
    #define MEMPOOL_REGION(inst) \
        { (void *)MEMPOOL_BUFFER_NAME(inst), DT_INST_PROP(inst, size), DT_INST_PROP(inst, attr)},
    DT_INST_FOREACH_STATUS_OKAY(MEMPOOL_REGION)
    { NULL, 0}
};

lwmem_region_t* mmem_get_mem_regions(void)
{
    return mem_regions;
}

void *mmem_caps_malloc(size_t size, uint32_t caps)
{
    void *ptr = NULL;
    uint32_t exp_caps = MEM_CAPS_GET(caps);
    for (int i=0; i<ARRAY_SIZE(mem_regions)-1; i++) {
        if (exp_caps & MEM_CAPS_GET(mem_regions[i].attr)) {
            ptr = lwmem_malloc_ex(NULL, &mem_regions[i], size);
            if (ptr != NULL) {
                return ptr;
            }
        }
    }
    return NULL;
}

void *mmem_caps_calloc(size_t nitems, size_t size, uint32_t caps)
{
    void *ptr = NULL;
    uint32_t exp_caps = MEM_CAPS_GET(caps);
    for (int i=0; i<ARRAY_SIZE(mem_regions)-1; i++) {
        if (exp_caps & MEM_CAPS_GET(mem_regions[i].attr)) {
            ptr = lwmem_calloc_ex(NULL, &mem_regions[i], nitems, size);
            if (ptr != NULL) {
                return ptr;
            }
        }
    }
    return NULL;
}

void *mmem_malloc(size_t size)
{
    void *ptr = NULL;
    for (int i = 0; i<ARRAY_SIZE(mem_regions)-1; i++) {
        ptr = lwmem_malloc_ex(NULL, &mem_regions[i], size);
        if (ptr != NULL) {
            return ptr;
        }
    }
    return NULL;
}

void *mmem_calloc(size_t nitems, size_t size)
{
    void *ptr = NULL;
    for (int i = 0; i<ARRAY_SIZE(mem_regions)-1; i++) {
        ptr = lwmem_calloc_ex(NULL, &mem_regions[i], nitems, size);
        if (ptr != NULL) {
            return ptr;
        }
    }
    return NULL;
}

void *mmem_realloc(void* ptr, size_t size)
{
    return lwmem_realloc(ptr, size);
}

void mmem_free(void* ptr)
{
    lwmem_free(ptr);
}

void *mmem_caps_aligned_alloc(size_t alignment, size_t size, uint32_t caps)
{
    int offset = alignment - 1 + sizeof(void*);
    void* p1 = (void*)mmem_caps_malloc(size + offset, caps);
    if (p1 == NULL)
        return NULL;
    void** p2 = (void**)( ( (size_t)p1 + offset ) & ~(alignment - 1) );
    p2[-1] = p1;
    return p2;
}

void mmem_aligned_free(void* ptr)
{
    void* p1 = ((void**)ptr)[-1];
    lwmem_free(p1);
}

void
_swap(lwmem_region_t *a, lwmem_region_t *b) {
    lwmem_region_t temp;
    temp = *a;
    *a = *b;
    *b = temp;
}

static void
quickSort(lwmem_region_t *arr, int maxlen, int begin, int end) {
    int i, j;
    if (begin < end) {
        i = begin + 1;
        j = end;
        while (i < j) {
            if(arr[i].start_addr > arr[begin].start_addr) {
                _swap(&arr[i], &arr[j]);
                j--;
            } else {
                i++;
            }
        }
        if (arr[i].start_addr >= arr[begin].start_addr) {
            i--;
        }
        _swap(&arr[begin], &arr[i]);
        quickSort(arr, maxlen, begin, i);
        quickSort(arr, maxlen, j, end);
    }
}

int mmem_init(void)
{
    quickSort(mem_regions, ARRAY_SIZE(mem_regions)-1, 0, ARRAY_SIZE(mem_regions)-2);
    lwmem_assignmem(mem_regions);
    return 0;
}

SYS_INIT(mmem_init, PRE_KERNEL_1,
	 18);

