/**
 * ARM64 / LoongArch64 / x86_64 高精度计时器
 *
 * ARM64:      使用 CNTFRQ_EL0 / CNTVCT_EL0 系统寄存器
 * LoongArch:  使用 rdtime.d 读取稳定计数器，cpucfg 获取频率
 * x86_64:     使用 RDTSC + clock_gettime 估算频率
 * 其他:       回退到 clock_gettime(CLOCK_MONOTONIC)
 */

#ifndef PRECISION_TIMER_H
#define PRECISION_TIMER_H

#include <stdint.h>
#include <stdio.h>

#if defined(__aarch64__) || defined(__arm64__)

static inline uint64_t get_cntfrq(void) {
    uint64_t freq;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r" (freq));
    return freq;
}

static inline uint64_t get_cntpct(void) {
    uint64_t count;
    __asm__ volatile(
        "isb\n\t"
        "mrs %0, cntvct_el0"
        : "=r" (count) : : "memory"
    );
    return count;
}

#elif defined(__loongarch__) || defined(__loongarch64) || defined(LOONGARCH64)

#include <time.h>

/**
 * LoongArch: cpucfg 0x4 的 [15:0] 存放 CC_FREQ 分子，[31:16] 存放分母
 * 若 cpucfg 返回 0，则回退到 CLOCK_MONOTONIC 估算。
 */
static inline uint64_t get_cntfrq(void) {
    uint32_t cfg;
    __asm__ volatile("cpucfg %0, %1" : "=r"(cfg) : "r"(4U));
    uint32_t num = cfg & 0xFFFF;
    uint32_t den = (cfg >> 16) & 0xFFFF;
    if (num == 0 || den == 0) {
        /* 回退：用 clock_gettime 测量 1ms 内的 tick 数来估算频率 */
        struct timespec t0, t1;
        uint64_t c0, c1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        __asm__ volatile("rdtime.d %0, $zero" : "=r"(c0));
        do { clock_gettime(CLOCK_MONOTONIC, &t1); }
        while ((t1.tv_sec - t0.tv_sec) * 1000000000LL +
               (t1.tv_nsec - t0.tv_nsec) < 1000000LL);
        __asm__ volatile("rdtime.d %0, $zero" : "=r"(c1));
        int64_t ns = (t1.tv_sec - t0.tv_sec) * 1000000000LL +
                     (t1.tv_nsec - t0.tv_nsec);
        return (c1 - c0) * 1000000000ULL / (uint64_t)ns;
    }
    /* cpucfg 0x5 存放 CPU 主频（Hz） */
    uint32_t cpu_hz;
    __asm__ volatile("cpucfg %0, %1" : "=r"(cpu_hz) : "r"(5U));
    return (uint64_t)cpu_hz * num / den;
}

static inline uint64_t get_cntpct(void) {
    uint64_t count;
    __asm__ volatile("rdtime.d %0, $zero" : "=r"(count) : : "memory");
    return count;
}

#elif defined(__x86_64__) || defined(__i386__)

#include <time.h>

static inline uint64_t get_cntfrq(void) {
    struct timespec t0, t1;
    uint32_t lo0, hi0, lo1, hi1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    __asm__ volatile("rdtsc" : "=a"(lo0), "=d"(hi0));
    do { clock_gettime(CLOCK_MONOTONIC, &t1); }
    while ((t1.tv_sec - t0.tv_sec) * 1000000000LL +
           (t1.tv_nsec - t0.tv_nsec) < 1000000LL);
    __asm__ volatile("rdtsc" : "=a"(lo1), "=d"(hi1));
    uint64_t c0 = ((uint64_t)hi0 << 32) | lo0;
    uint64_t c1 = ((uint64_t)hi1 << 32) | lo1;
    int64_t ns = (t1.tv_sec - t0.tv_sec) * 1000000000LL +
                 (t1.tv_nsec - t0.tv_nsec);
    return (c1 - c0) * 1000000000ULL / (uint64_t)ns;
}

static inline uint64_t get_cntpct(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi) : : "memory");
    return ((uint64_t)hi << 32) | lo;
}

#else

#include <time.h>

/* 通用回退：使用 CLOCK_MONOTONIC，频率固定为 1GHz（纳秒单位） */
static inline uint64_t get_cntfrq(void) {
    return 1000000000ULL;
}

static inline uint64_t get_cntpct(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

#endif /* architecture */


/**
 * 将 ticks 差值转换为微秒 (μs)
 */
static inline uint64_t ticks_to_us(uint64_t ticks_start, uint64_t ticks_end, uint64_t freq) {
    return (ticks_end - ticks_start) * 1000000ULL / freq;
}

/**
 * 将 ticks 差值转换为纳秒 (ns)
 */
static inline uint64_t ticks_to_ns(uint64_t ticks_start, uint64_t ticks_end, uint64_t freq) {
    return (ticks_end - ticks_start) * 1000000000ULL / freq;
}

/**
 * 将 ticks 差值转换为毫秒 (ms)
 */
static inline uint64_t ticks_to_ms(uint64_t ticks_start, uint64_t ticks_end, uint64_t freq) {
    return (ticks_end - ticks_start) * 1000ULL / freq;
}

/**
 * 将 ticks 差值转换为秒 (s)
 */
static inline double ticks_to_seconds(uint64_t ticks_start, uint64_t ticks_end, uint64_t freq) {
    return (double)(ticks_end - ticks_start) / (double)freq;
}

static inline double get_timer_precision_ns(uint64_t freq) {
    return 1000000000.0 / (double)freq;
}

static inline void print_timer_info(void) {
    uint64_t freq = get_cntfrq();
    printf("Timer Frequency: %lu Hz (%.2f MHz), Precision: %.2f ns/tick\n",
           freq, freq / 1000000.0, get_timer_precision_ns(freq));
}

#endif /* PRECISION_TIMER_H */
