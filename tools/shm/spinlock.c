#include "shm/spinlock.h"
#include <sched.h>
#include <stdio.h>
#include <stdint.h>

static void byte_flag_init(ByteFlag* flag)
{
    atomic_init(flag,BYTE_UNLOCK_STATE);
}

static int32_t byte_flag_is_locked(ByteFlag* flag)
{
    return atomic_load(flag) == BYTE_LOCKED_STATE ? 0 : -1;
}

static void byte_flag_lock(ByteFlag* flag)
{
    uint8_t expected_state = BYTE_UNLOCK_STATE; /* 只有处于未加锁状态的锁才能被加锁 */
    uint8_t aim_state = BYTE_LOCKED_STATE; /* 需要将锁设置为已加锁状态 */
    /* 尝试：直至加锁成功 */
    while (!atomic_compare_exchange_strong(flag, &expected_state, aim_state))
    {
        /* 一次尝试后需要重置期望值 */
        expected_state = BYTE_UNLOCK_STATE;
        sched_yield(); /* 让出CPU */
    }
}

static int32_t byte_flag_unlock(ByteFlag* flag)
{
    uint8_t expected_state = BYTE_LOCKED_STATE; /* 只有处于加锁状态的锁才能被解锁 */
    uint8_t aim_state = BYTE_UNLOCK_STATE; /* 需要将锁设置为未加锁状态 */
    /* 只有处于加锁状态的锁才能解锁：不支持递归加锁 */
    if (!atomic_compare_exchange_strong(flag, &expected_state, aim_state))
    {
        printf("byte_flag_unlock_error: unexpected lock state\n");
        return -1;
    }
    return 0;
}

static int32_t byte_flag_try_lock(ByteFlag* flag)
{
    uint8_t expected_state = BYTE_UNLOCK_STATE; /* 只有处于未加锁状态的锁才能被加锁 */
    uint8_t aim_state = BYTE_LOCKED_STATE; /* 需要将锁设置为已加锁状态 */
    /* 只有处于加锁状态的锁才能解锁：不支持递归加锁 */
    if (!atomic_compare_exchange_strong(flag, &expected_state, aim_state))
    {
        printf("byte_flag_try_lock_warn: byte flag is locked before\n");
        return -1;
    }
    return 0;
}

struct ByteFlagOps byte_flag_ops = 
{
    .init = byte_flag_init,
    .is_locked = byte_flag_is_locked,
    .lock = byte_flag_lock,
    .unlock = byte_flag_unlock,
    .try_lock = byte_flag_try_lock
};

static void mark_flag_lock(MarkFlag* flag)
{
    /* 尝试：直至加锁成功 */
    while (atomic_flag_test_and_set(flag))
    {
        sched_yield();
    }
}

static void mark_flag_unlock(MarkFlag* flag)
{
    atomic_flag_clear(flag);
}

static int32_t mark_flag_try_lock(MarkFlag* flag)
{
    if (atomic_flag_test_and_set(flag))
    {
        printf("lock_flag_try_lock_warn: lock flag is locked\n");
        return -1;
    }
    return 0;
}

struct MarkFlagOps mark_flag_ops = 
{
    .lock = mark_flag_lock,
    .unlock = mark_flag_unlock,
    .try_lock = mark_flag_try_lock
};