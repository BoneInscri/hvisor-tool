#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <signal.h>
#include "shm/config/config_zone.h"
#include "shm/config/config_common.h"
#include "shm/config/config_shm.h"
#include "shm/config/config_addr.h"
#include "shm/shm.h"
#include "hvisor.h"
#include <errno.h>

// TODO: add a mutex to protect shm init process (shm_init_mark)
static struct Shm shm = {0}; /* 共享内存管理信息 */
/* 通道初始化只能由一个任务(线程）完成 */ 
// static volatile uint32_t shm_init_mark = INIT_MARK_RAW_STATE; /* 初始化相关标记信息 */
static int mem_fd = -1;
static int hvisor_fd = -1;

static uint64_t malloc_from_volatile_block(int start_index, int end_index, int size)
{
    for (int i = start_index; i < end_index; i++)
    {
        if (shm.shm_infos->vblock_infos[i].state == VBLOCK_STATE_ENABLE) /* 当前区块还可以尝试继续进行分配 */
        {
            if (size <= shm.shm_infos->vblock_infos[i].available_size) /* 当前块可以满足此次内存分配请求 */
            {
                
                int32_t result = shm.shm_infos->vblock_infos[i].next_alloc_offset;

                shm.shm_infos->vblock_infos[i].available_size -= size;
                shm.shm_infos->vblock_infos[i].alloc_count += 1;
                shm.shm_infos->vblock_infos[i].next_alloc_offset += size;

                shm.shm_infos->vblock_current = i; 

                return result;
            }
            else /* 当前块无法满足分配要求 */
            {

            #if UNUSED_COUNT
                shm.unused_mem_size += shm.shm_infos->vblock_infos[i].available_size; /* 内存碎片 */
            #endif /* UNUSED_COUNT */
                shm.shm_infos->vblock_infos[i].state = VBLOCK_STATE_DISENABLE; /* 当前块弃用，开始启用下一块 */
            }
        }
        else /* 查看当前块能不能重新加入分配 */
        {
            if (shm.shm_infos->vblock_infos[i].free_count == shm.shm_infos->vblock_infos[i].alloc_count)
            {
                shm.shm_infos->vblock_infos[i].next_alloc_offset = shm.shm_infos->vblock_infos[i].alloc_start_offset;
                shm.shm_infos->vblock_infos[i].available_size = shm.shm_infos->vblock_infos[i].total_length;
                shm.shm_infos->vblock_infos[i].free_count = 0;
                shm.shm_infos->vblock_infos[i].alloc_count = 0;
                shm.shm_infos->vblock_infos[i].state = VBLOCK_STATE_ENABLE;

                i--; /* 当前块重新加入分配 */
            }
        }
    }

    return -1; /* 分配未成功 */
}

static uint64_t shm_addr_to_offset(void* ptr)
{
    // printf("shm_addr_to_offset\n");
    printf("shm_addr_to_offset: called with ptr=%p\n", ptr);
    printf("shm_addr_to_offset: zone_start=%p, zone_len=0x%x\n", 
        shm.shm_zone_start, shm.shm_cfg->zone_shm->len);
    printf("shm_addr_to_offset: zone_end=%p\n", 
        (void*)(shm.shm_zone_start + shm.shm_cfg->zone_shm->len));

    if (ptr < shm.shm_zone_start || ptr > shm.shm_zone_start + shm.shm_cfg->zone_shm->len)
    {
        printf("shm_addr_to_offset_error: ptr out of range, ptr = %p, shm.shm_zone_start = %p, shm.shm_zone_end = %p\n", 
            ptr, shm.shm_zone_start, (void*)(shm.shm_zone_start + shm.shm_cfg->zone_shm->len));
        while(1) {}
    }
    // return ptr - shm.shm_zone_start;
        
    uint64_t offset = ptr - shm.shm_zone_start;
    printf("shm_addr_to_offset: calculated offset=0x%lx\n", offset);
    return offset;
}

static void* shm_offset_to_addr(uint32_t offset)
{
    // printf("shm_offset_to_addr\n");
    if (offset > shm.shm_cfg->zone_shm->len)
    {
        printf("shm_offset_to_addr_error: error range, offset = %u, shm.shm_cfg->zone_shm->len = %u\n", 
            offset, shm.shm_cfg->zone_shm->len);
        while(1) {}
    }
    return (void*)(shm.shm_zone_start + offset);
}

static int32_t shm_init(void)
{
    // printf("shm_init\n");
    // TODO: add a mutex to protect shm init process (shm_init_mark)

    // check shm_init_mark
    // if (shm_init_mark == INIT_MARK_INITIALIZED) /* 已初始化 */
    // {
        // TODO: release the mutex
        // while(1) {}
    // }
    // if (shm_init_mark == INIT_MARK_DESTORYED)   /* 已经损坏 */
    // {
    //     // TODO: release the mutex
    //     while(1) {}
    // }
    
    struct ShmCfg* shm_cfg = shm_cfg_ops.get_by_id(zone_info->id); /* 获取当前核心的共享内存配置信息 */
    if (shm_cfg == NULL)
    {
        // shm_init_mark = INIT_MARK_DESTORYED;
        // TODO: release the mutex
        printf("shm_init error: have no shm cfg for core = %u\n", zone_info->id);
        while(1) {}
    }
    shm.shm_cfg = shm_cfg;

    if (shm_cfg->zone_shm == NULL) /* 当前节点没有共享内存 */
    {
        // shm_init_mark = INIT_MARK_DESTORYED;
        // TODO: release the mutex
        printf("shm_init error: must have total shm addr info even have no core shm\n");
        while(1) {}
    }
    

    /* 提取各项共享内存配置信息 */
    uint64_t zone_shm_start = 0;
    uint32_t zone_shm_length = 0;
    uint64_t zone_shm_end = 0;

    if (shm_cfg->zone_shm == NULL)
    {
        /* 当前节点没有共享内存 */
        printf("shm_init_warn: have no core shm\n");
    }
    else 
    {
        /* 当前节点存在共享内存 */  
        zone_shm_start = shm_cfg->zone_shm->start;
        zone_shm_length = shm_cfg->zone_shm->len;
        zone_shm_end = zone_shm_start + zone_shm_length;
        // printf("shm_init_info: zone_shm [start = %p, length = %u KB]\n", 
        //     (void*)zone_shm_start, zone_shm_length / KB);
    }

    /* 初始化共享内存管理指针 */
    mem_fd = open(MEM_DRIVE, O_RDWR | O_SYNC);
    if (mem_fd < 0) 
    {
        // shm_init_mark = INIT_MARK_DESTORYED;
        // TODO: release the mutex
        printf("shm_init_error: open mem dev fail %d\n", mem_fd);
        // return -1;
        while(1) {}
    }
    // printf("shm_init_info: open mem dev success %d\n", mem_fd);

    hvisor_fd = open(HVISOR_DRIVE, O_RDWR | O_SYNC); /* hvisor */
    if (hvisor_fd < 0) 
    {
        // shm_init_mark = INIT_MARK_DESTORYED;
	    // mark_flag_ops.unlock(&shm_init_lock);
        // TODO: release the mutex
        printf("shm_init_error: open hvisor dev fail %d\n", hvisor_fd);
        while(1) {}
        // return -1;
    }
    // printf("shm_init_info: open hvisor dev success %d\n", hvisor_fd);

    kmalloc_info_t kmalloc_info;
    kmalloc_info.pa = 0;
    kmalloc_info.size = MEM_PAGE_SIZE;

    // TODO: don't forget to free the region!!!
    long ret = ioctl(hvisor_fd, HVISOR_ZONE_M_ALLOC, &kmalloc_info);
    
    uint64_t shm_infos_pa = kmalloc_info.pa;
    shm.shm_infos_pa = shm_infos_pa;// used for free the region!
    shm.shm_infos = (struct ShmInfos*)mmap(NULL, MEM_PAGE_SIZE, PROT_READ | PROT_WRITE, 
        MAP_SHARED, hvisor_fd, shm_infos_pa); // use hvisor_mmap handler

    if (shm.shm_infos == NULL)
    {
        // shm_init_mark = INIT_MARK_DESTORYED;
        // TODO: release the mutex
	    printf("shm_init_error: mmap shm manage info fail\n");
        while(1) {}
    }
    printf("shm_init_info: virtual addr [zone_start = %p, zone_end = %p]\n", 
        shm.shm_zone_start, (void*)(shm.shm_zone_start + zone_shm_length));
    printf("shm_init_info: mmap shm manage info success [shm_infos_pa = %p, shm_infos_va = %p]\n", 
        (void*)shm_infos_pa, shm.shm_infos);

    /* 映射出内核中的管理信息块 */
    printf("shm_init_debug: mapping zone_shm_start=0x%lx, length=0x%x\n", zone_shm_start, zone_shm_length);
    shm.shm_zone_start = mmap(NULL, zone_shm_length, PROT_READ | PROT_WRITE, 
        MAP_SHARED, mem_fd, zone_shm_start);
    printf("shm_init_debug: mmap returned %p, MAP_FAILED=%p\n", shm.shm_zone_start, MAP_FAILED);

    if (shm.shm_zone_start == MAP_FAILED)

    {
        // shm_init_mark = INIT_MARK_DESTORYED;
        // TODO: release the mutex
        printf("shm_init_error: mmap shm fail, errno=%d\n", errno);
        while(1) {}
    }
    if (shm.shm_zone_start == NULL)
    {
        printf("shm_init_error: mmap returned NULL, errno=%d\n", errno);
        while(1) {}
    }
    // printf("shm_init_info: virtual addr [zone_start = %p, zone_end = %p]\n", 
    //     shm.shm_zone_start, (void*)(shm.shm_zone_start + zone_shm_length));

// #endif /* #if PLATFORM_CHOICE == PLATFORM_LINUX_USER */
// #if (PLATFORM_CHOICE / PLATFORM_RTOS_BASE) == 1
//     shm.shm_infos = &shm_infos;

//     shm.shm_zone_start = (void *)total_shm_start;
//     shm.shm_core_start = (void *)zone_shm_start;
// #endif /* PLATFORM_CHOICE == PLATFORM_RTOS_XIUOS */

//     if (atomic_load(&shm.shm_infos->block_init_mark) == INIT_MARK_INITIALIZED) /* 在整个节点内存探测是否已经被初始化过了 */
//     {
//         shm_init_mark = INIT_MARK_INITIALIZED;
//         mark_flag_ops.unlock(&shm_init_lock);

//         printf("shm_init_info: shm init before 2\n");
//         return 0;
//     }

//     uint32_t except_state = INIT_MARK_RAW_STATE; /* 假定该共享内存还未被初始化 */
//     /* 试图获取初始化权 */
//     if (!atomic_compare_exchange_strong(&shm.shm_infos->block_init_mark,&except_state,INIT_MARK_INITIALIZING))
//     {
//         while (1)
//         {
//             uint8_t init_state = atomic_load(&shm.shm_infos->block_init_mark);
//             if (init_state == INIT_MARK_INITIALIZING)
//             {
//                 printf("shm_init_info: wait others shm init over\n");
//             }
//             else if (init_state == INIT_MARK_INITIALIZED)
//             {
//                 shm_init_mark = INIT_MARK_INITIALIZED;
//                 mark_flag_ops.unlock(&shm_init_lock);

//                 printf("shm_init_info: init by others\n");
//                 return 0;
//             }
//             else if (init_state == INIT_MARK_DESTORYED)
//             {
//                 shm_init_mark = INIT_MARK_DESTORYED;
//                 mark_flag_ops.unlock(&shm_init_lock);

//                 printf("shm_init_error: init shm error by others or init mark error\n");
//                 return -1;
//             }
//         }
//     }

    /* 开始进行共享内存的初始化 */
    shm.shm_infos->block_state_info = BLOCK_STATE_UNUSED;

    // printf("shm_init: step2\n");
    /* 当前节点没有共享内存 just return */
    if (zone_shm_length == 0) 
    {
        // atomic_store(&shm.shm_infos->block_init_mark,INIT_MARK_INITIALIZED);
        // /* 通知其他线程共享内存初始化完毕 */ shm_init_mark =
        // INIT_MARK_INITIALIZED; mark_flag_ops.unlock(&shm_init_lock);
        printf("shm_init_info: init success, but have no zone shm\n");
        return 0;
    }

    // printf("shm_init: step3\n");

    /* 初始化当前节点的共享内存 */
    uint32_t min_block_size = MAX(shm_cfg->min_block_size, MEMORY_ALIGN_SIZE);

    // printf("min_block_size = 0x%x\n", min_block_size);

    /* 短期区块最小区块大小 */
    uint32_t pblock_size = (shm_cfg->pblock_size > min_block_size) ? shm_cfg->pblock_size : 0;

    // printf("pblock_size = 0x%x\n", pblock_size);

    // printf("shm_init: step5\n");
    if (pblock_size > 0)
    {
        // printf("[Attention] exist pblock\n");
        /* 共享内存里面有长期块 */
        shm.shm_infos->block_state_info |= BLOCK_STATE_PERSISTENT; 
    }

    /* 短期区块总大小 */ 
    uint32_t vblock_size = shm_cfg->vblock_size > min_block_size ? shm_cfg->vblock_size : 0;

    // printf("vblock_size = 0x%x\n", vblock_size);
    uint32_t vblock_each_size = 0U;

    if (vblock_size > 0 && SHM_VBLOCK_CNT > 0)
    {
         /* 共享内存里面有短期块 */
        shm.shm_infos->block_state_info |= BLOCK_STATE_VOLATILE;
        vblock_each_size = vblock_size / SHM_VBLOCK_CNT;

        // printf("exist vblock, vblock_each_size = 0x%x\n", vblock_each_size);

        if (vblock_each_size < min_block_size) /* 查看内存配置是否合理 */
        {
            // atomic_store(&shm.shm_infos->block_init_mark,INIT_MARK_DESTORYED);
            // /* 告知其他线程共享内存初始化失败 */ 
            // shm_init_mark =
            //     INIT_MARK_DESTORYED; mark_flag_ops.unlock(&shm_init_lock);
            printf("shm_init_error: each vblock is too small vblock_each_size =%u, min_block_size = %u, vblock_size = %u, vblock_count = %u\n",
                vblock_each_size, min_block_size, vblock_size, SHM_VBLOCK_CNT);
            while(1) {}
        }
        shm.shm_infos->vblock_each_size = vblock_each_size;
    }
    // printf("shm_init_info: pblock_size = %u KB, vblock_size = %u KB, vblock_each_size = %u KB, min_block_size = %u B\n", 
    //         pblock_size / KB, vblock_size / KB, vblock_each_size / KB, min_block_size);
    // /* 初始化互斥标记 */
    byte_flag_ops.init(&shm.shm_infos->pblock_lock);
    byte_flag_ops.init(&shm.shm_infos->vblock_lock);

    uint64_t init_offset = 0;  // can't use uint32_t
    //  开始初始化共享内存的管理信息 */

    /* 先初始化长期区 */
    shm.shm_infos->pblock_info.next_alloc_offset = init_offset;
    shm.shm_infos->pblock_info.available_size = pblock_size;
    init_offset += pblock_size;

    // printf("after init_offset += pblock_size, init_offset = 0x%lx\n",
    //        init_offset);

    /* 初始化短期区 */
    if (vblock_each_size > min_block_size) /* 存在短期区 */
    {
        for (int i = 0; i < SHM_VBLOCK_CNT; i++)
        {
            shm.shm_infos->vblock_infos[i].state = VBLOCK_STATE_ENABLE;
            shm.shm_infos->vblock_infos[i].alloc_count = 0U;
            shm.shm_infos->vblock_infos[i].free_count = 0U;
            shm.shm_infos->vblock_infos[i].alloc_start_offset = init_offset; 
            shm.shm_infos->vblock_infos[i].next_alloc_offset = init_offset;

            if (i != SHM_VBLOCK_CNT - 1) 
            {
                /* 非最后一个短期块 */
                shm.shm_infos->vblock_infos[i].total_length = vblock_each_size;
                shm.shm_infos->vblock_infos[i].available_size = vblock_each_size; 
                init_offset += vblock_each_size;
            }
            else 
            {
                /* 最后一个短期块 */
                shm.shm_infos->vblock_infos[i].total_length = vblock_each_size + vblock_size % SHM_VBLOCK_CNT;
                shm.shm_infos->vblock_infos[i].available_size = shm.shm_infos->vblock_infos[i].total_length; 
                init_offset += shm.shm_infos->vblock_infos[i].total_length;
            }
            // printf("shm_init_info: index = %d, start_offset = 0x%lx, "
            //        "total_length = %u KB\n",
            //        i, shm.shm_infos->vblock_infos[i].alloc_start_offset,
            //        shm.shm_infos->vblock_infos[i].total_length / KB);
        }

        if (init_offset != zone_shm_end - zone_shm_start) 
        {
            /* 是否正确初始化 */
            // atomic_store(&shm.shm_infos->block_init_mark,INIT_MARK_DESTORYED);
            // shm_init_mark = INIT_MARK_DESTORYED;
            // mark_flag_ops.unlock(&shm_init_lock);
            printf("shm_init_error: zone_shm_end = 0x%lx, init_offset = 0x%lx\n", 
                zone_shm_end, init_offset); 
            while(1) {}
        }
    }

#if UNUSED_COUNT
    /* 碎片计数器 */
    shm.unused_mem_size = 0U; 
#endif /* UNUSED_COUNT */

    // atomic_store(&shm.shm_infos->block_init_mark,INIT_MARK_INITIALIZED);
    // /* 通知其他线程共享内存初始化完毕 */ 

    // shm_init_mark = INIT_MARK_INITIALIZED; 
    // TODO: release the mutex
    // printf("shm_init_success: init shm success\n");
    printf("shm_init_success: init shm success\n");
    printf("shm_init_summary: zone_start=%p, zone_length=0x%x, pblock_size=0x%x, vblock_size=0x%x\n",
        shm.shm_zone_start, zone_shm_length, shm.shm_cfg->pblock_size, shm.shm_cfg->vblock_size);
    return 0;
}

static void* shm_malloc(uint32_t size, enum MallocType type)
{
    // printf("shm_malloc\n");
    // if (shm_init_mark != INIT_MARK_INITIALIZED)
    // {
    //     printf("shm_malloc_error: init shm before use\n");
    //     while(1) {}
    // }
    uint32_t original_size = size;

    /* 按照配置信息进行字节对齐 */
    size = (size + shm.shm_cfg->bit_align -1) & (~(shm.shm_cfg->bit_align - 1));
    
    printf("shm_malloc_debug: size alignment [original=%d, aligned=%d, bit_align=%d]\n", 
        original_size, size, shm.shm_cfg->bit_align);
    switch (type)
    {
        case MALLOC_TYPE_P:
        {
            byte_flag_ops.lock(&shm.shm_infos->pblock_lock); /* 长期区锁 */
            if (size > shm.shm_infos->pblock_info.available_size) /* 申请的内存大小超过了最大内存限制 */
            {
                byte_flag_ops.unlock(&shm.shm_infos->pblock_lock); /* 解锁 */
                printf("shm_malloc_warn: the size is large than max avi size, size = %d, avi size = %d\n", 
                    size, shm.shm_infos->pblock_info.available_size);
                return NULL;
            }
            void* result = shm.shm_zone_start + shm.shm_infos->pblock_info.next_alloc_offset; /* 记录下需要返回的地址 */

            printf("shm_malloc_debug: MALLOC_TYPE_P [zone_start=%p, offset=%d, final_addr=%p, size=%d]\n",
                shm.shm_zone_start, shm.shm_infos->pblock_info.next_alloc_offset, result, size);
            printf("shm_malloc_debug: pblock info [next_alloc_offset=%d, available_size=%d]\n",
                shm.shm_infos->pblock_info.next_alloc_offset, shm.shm_infos->pblock_info.available_size);
            shm.shm_infos->pblock_info.available_size -= size; /* 减去已经被分配的空间大小 */
            shm.shm_infos->pblock_info.next_alloc_offset += size; /* 将待分配地址指向下一个位置 */

            byte_flag_ops.unlock(&shm.shm_infos->pblock_lock); /* 解锁 */

            // printf("shm_malloc_success: MALLOC_TYPE_P [start address: %p, length = %d B]\n", 
            //     result, size);
            return result;
        }
        case MALLOC_TYPE_V:
        {
            byte_flag_ops.lock(&shm.shm_infos->vblock_lock); /* 短期锁 */
            uint32_t current_volatile_block_index = shm.shm_infos->vblock_current; /* 记录开始查找前短期块的当前位置下标 */
            int32_t result = malloc_from_volatile_block(current_volatile_block_index, SHM_VBLOCK_CNT, size); /* 从当前位置往后找 */
            if (result < 0) /* 没找到 */
            {
                result = malloc_from_volatile_block(0, current_volatile_block_index, size); /* 从前往当前位置找 */
            }

            if (result < 0) /* 无法分配空间 */
            {
                byte_flag_ops.unlock(&shm.shm_infos->vblock_lock); /* 解锁 */
                printf("shm_malloc_warn: have no such size space free now!\n");
                return NULL;
            }

            byte_flag_ops.unlock(&shm.shm_infos->vblock_lock); /* 解锁 */

            // printf("shm_malloc_success: MALLOC_TYPE_V [start address: %p, length = %d B]\n", 
            //     shm.shm_zone_start + result, size);

            // return shm.shm_zone_start + result;
            void* final_addr = shm.shm_zone_start + result;
            printf("shm_malloc_debug: MALLOC_TYPE_V [zone_start=%p, offset=%d, final_addr=%p, size=%d]\n", 
                shm.shm_zone_start, result, final_addr, size);
            // Additional debug info for volatile block allocation
            printf("shm_malloc_debug: volatile block details [current_block=%d, result_offset=%d]\n",
                shm.shm_infos->vblock_current, result);
            if (shm.shm_infos->vblock_current < SHM_VBLOCK_CNT) {
                int current_block = shm.shm_infos->vblock_current;
                printf("shm_malloc_debug: block[%d] info [start_offset=%d, available_size=%d, total_length=%d]\n",
                    current_block,
                    shm.shm_infos->vblock_infos[current_block].alloc_start_offset,
                    shm.shm_infos->vblock_infos[current_block].available_size,
                    shm.shm_infos->vblock_infos[current_block].total_length);
            }
            return final_addr;
        }
        default:
        {
            printf("shm_malloc_warn: not suported alloc type! check the type\n");
            while(1) {}
            return NULL;
        }
    }
    
}

static int32_t shm_destory(void)
{
    // printf("shm_destory\n");
    // mark_flag_ops.lock(&shm_init_lock); /* 单线程销毁 */

    // if (shm_init_mark != INIT_MARK_INITIALIZED)
    // {
    //     // mark_flag_ops.unlock(&shm_init_lock);
    //     printf("shm_destory: shm not init\n");
    //     while(1) {}
    // }

    uint32_t shm_info_length = sizeof(struct ShmInfos);

    uint8_t* shm_info_byte = shm.shm_infos;
    for (int i = 0; i < shm_info_length; i++)
    {
        /* 将内核中的共享内存分配信息清零 */
        shm_info_byte[i] = 0U;
    }
    
    munmap(shm.shm_infos, MEM_PAGE_SIZE); /* 将连接到进程的 MMAP 内存解绑 */
    munmap(shm.shm_zone_start, shm.shm_cfg->zone_shm->len); /* 将连接到进程的 MMAP 内存解绑 */
    // write the H_free ioctl! to free the memory of shm_infos
    kmalloc_info_t kmalloc_info;
    kmalloc_info.pa = shm.shm_infos_pa;
    kmalloc_info.size = MEM_PAGE_SIZE;
    long ret = ioctl(hvisor_fd, HVISOR_ZONE_M_FREE, &kmalloc_info);
    if (ret < 0)
    {
        printf("shm_destory_error: free shm_infos failed\n");
        while(1) {}
    }

#if UNUSED_COUNT
    shm.unused_mem_size = 0U; /* 清空计数器 */
#endif /* UNUSED_COUNT */

    // shm_init_mark = INIT_MARK_RAW_STATE;

    // mark_flag_ops.unlock(&shm_init_lock);
    // printf("shm_destory: destory success\n");

    return 0;
}

static void shm_free(void* ptr) {
    // if (shm_init_mark != INIT_MARK_INITIALIZED)
    // {
    //     printf("shm_free: shm not init, check it\n");
    //     while(1) {}
    // }
    if (shm.shm_infos->block_state_info & BLOCK_STATE_VOLATILE == 0)
    {
        printf("shm_free_error: have no volatile blocks, check it\n");
        while(1) {}
    }
    if (ptr < shm.shm_zone_start || ptr >= shm.shm_zone_start + shm.shm_cfg->zone_shm->len)
    {
        printf("shm_free_error: not such shm, check it\n");
        while(1) {}
    }

    uint32_t data_offset = ptr - shm.shm_zone_start - shm.shm_cfg->pblock_size;
    uint32_t block_index = data_offset / shm.shm_infos->vblock_each_size; /* 读取短期块的控制信息 */

    byte_flag_ops.lock(&shm.shm_infos->vblock_lock);
    shm.shm_infos->vblock_infos[block_index].free_count += 1;

    // printf("shm_free_success: start = %p, index = %u, free_count = %u\n", ptr,
    //     block_index, shm.shm_infos->vblock_infos[block_index].free_count);
    byte_flag_ops.unlock(&shm.shm_infos->vblock_lock);
}

struct ShmOps shm_ops =
{
    .addr_to_offset = shm_addr_to_offset,
    .offset_to_addr = shm_offset_to_addr,
    .shm_init = shm_init,
    .shm_destory = shm_destory,
    .shm_malloc = shm_malloc,
    .shm_free = shm_free    
};