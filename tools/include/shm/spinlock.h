#ifndef _SPINLOCK_H_
#define _SPINLOCK_H_

#include <stdatomic.h>
#include <sys/types.h>

// TODO "futex" between VMs?

#define BYTE_LOCKED_STATE (0xFU)
#define BYTE_UNLOCK_STATE (0xAU)
typedef atomic_uchar ByteFlag;
/* 不支持递归加锁 */
struct ByteFlagOps /* CAS原子锁操作集合 */
{
    void (*init)(ByteFlag *flag); /* 初始化原子锁 */
    int32_t (*is_locked)(ByteFlag *flag); /* 查看原子锁是否被锁定 */
    void (*lock)(ByteFlag *flag); /* 原子锁锁定 */
    int32_t (*unlock)(ByteFlag *flag); /* 原子锁释放 */
    int32_t (*try_lock)(ByteFlag *flag); /* 原子锁试图加锁 */
};
extern struct ByteFlagOps byte_flag_ops;

/* 适合存放在代码区的互斥量 */
typedef atomic_flag MarkFlag;
struct MarkFlagOps
{
    void (*lock)(MarkFlag *flag); /* 原子锁锁定 */
    void (*unlock)(MarkFlag *flag); /* 原子锁释放 */
    int32_t (*try_lock)(MarkFlag *flag); /* 原子锁试图加锁 */
};
extern struct MarkFlagOps mark_flag_ops;

#endif // _SPINLOCK_H_