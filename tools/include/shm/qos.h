#ifndef _QOS_H_
#define _QOS_H_

#include <stdint.h>
#include <time.h>
#include "msg.h"  /* 引入Msg结构定义 */

/* ========== QoS服务分类 ========== */
enum QoSClass {
    QOS_REALTIME = 0,        /* 实时服务 - 低延迟优先 */
    QOS_THROUGHPUT = 1,      /* 吞吐型服务 - 高带宽优先 */
    QOS_RELIABLE = 2,        /* 可靠服务 - 零丢包优先 */
    QOS_BEST_EFFORT = 3,     /* 尽力而为 - 无保证 */
    QOS_CLASS_COUNT = 4
};

/* ========== QoS服务配置文件 ========== */
struct ServiceQoSProfile {
    uint16_t service_id;              /* 服务ID */
    enum QoSClass qos_class;          /* QoS类别 */
    
    /* QoS保证参数 */
    uint32_t max_latency_us;          /* 最大延迟 (微秒) */
    uint32_t min_bandwidth_mbps;      /* 最小带宽 (Mbps) */
    uint32_t max_jitter_us;           /* 最大抖动 (微秒) */
    float    loss_tolerance;          /* 丢包容忍度 (0.0-1.0) */
    
    /* 资源预留 */
    uint32_t reserved_buffer_size;    /* 预留缓冲区大小 (字节) */
    uint16_t reserved_queue_slots;    /* 预留队列槽位数 */
    uint8_t  preemption_enabled;      /* 是否允许抢占 (0=否, 1=是) */
    
    /* 发送策略 */
    uint8_t  batch_enabled;           /* 是否启用批量发送 */
    uint16_t batch_size;              /* 批量大小 */
    uint32_t batch_timeout_us;        /* 批量超时 (微秒) */
};

/* ========== QoS统计指标 ========== */
struct QoSMetrics {
    /* 延迟统计 */
    uint64_t total_latency_us;        /* 累计延迟 */
    uint64_t avg_latency_us;          /* 平均延迟 */
    uint64_t min_latency_us;          /* 最小延迟 */
    uint64_t max_latency_us;          /* 最大延迟 */
    uint64_t jitter_us;               /* 抖动 */
    
    /* 吞吐量统计 */
    uint64_t total_bytes;             /* 总传输字节数 */
    uint64_t total_messages;          /* 总消息数 */
    uint64_t throughput_bps;          /* 吞吐量 (bps) */
    
    /* 可靠性统计 */
    uint32_t sent_count;              /* 发送计数 */
    uint32_t success_count;           /* 成功计数 */
    uint32_t failed_count;            /* 失败计数 */
    uint32_t dropped_count;           /* 丢弃计数 */
    float    packet_loss_rate;        /* 丢包率 */
    
    /* QoS违约统计 */
    uint32_t latency_violations;      /* 延迟违约次数 */
    uint32_t bandwidth_violations;    /* 带宽违约次数 */
    uint32_t total_violations;        /* 总违约次数 */
    
    /* 时间窗口 */
    struct timespec window_start;     /* 统计窗口开始时间 */
    struct timespec last_update;      /* 最后更新时间 */
};

/* ========== 扩展消息结构（包含QoS信息） ========== */
struct QoSMsg {
    /* 原始消息 */
    struct Msg base_msg;
    
    /* P0-CRITICAL-2警告: 
     * 当前base_msg是浅拷贝，仅支持基本类型（uint32_t, uint16_t等）
     * 如果未来struct Msg添加指针字段（如char* data），必须实现深拷贝！
     * 建议添加clone函数: struct Msg* msg_deep_clone(const struct Msg* src);
     */
    
    /* QoS扩展字段 */
    enum QoSClass qos_class;          /* QoS类别 */
    uint32_t sequence_num;            /* 消息序列号 */
    uint64_t send_timestamp;          /* 发送时间戳 (微秒) */
    uint64_t deadline;                /* 截止时间 (微秒) */
    uint8_t  retransmit_count;        /* 重传次数 */
    uint8_t  priority_boost;          /* 优先级提升标志 */
};

/* ========== QoS队列管理 ========== */
#define MAX_QOS_QUEUE_SIZE 64  /* 每个QoS队列最大消息数 */

struct QoSQueue {
    uint16_t head;                    /* 队列头 */
    uint16_t tail;                    /* 队列尾 */
    uint16_t size;                    /* 队列大小 */
    uint16_t capacity;                /* 队列容量 */
    uint16_t reserved_slots;          /* 预留槽位 */
    uint32_t reserved_buffer;         /* 预留缓冲区 */
    
    struct QoSMetrics metrics;        /* 队列统计指标 */
    struct QoSMsg messages[MAX_QOS_QUEUE_SIZE];  /* 消息缓冲区 */
};

/* ========== QoS通道控制器 ========== */
struct QoSChannelController {
    /* 每个QoS类别的独立队列 */
    struct QoSQueue queues[QOS_CLASS_COUNT];
    
    /* 全局资源管理 */
    uint32_t total_buffer_size;       /* 总缓冲区大小 */
    uint32_t available_buffer;        /* 可用缓冲区 */
    uint16_t total_queue_slots;       /* 总队列槽位 */
    uint16_t available_slots;         /* 可用槽位 */
    
    /* 调度策略 */
    enum QoSClass current_class;      /* 当前正在服务的QoS类别 */
    uint32_t class_quantum[QOS_CLASS_COUNT]; /* 每个类别的时间片 */
    uint32_t class_weight[QOS_CLASS_COUNT];  /* 每个类别的权重 */
    uint32_t class_credits[QOS_CLASS_COUNT]; /* 每个类别的当前信用值 */
    
    /* 性能监控 */
    uint64_t total_processed;         /* 总处理消息数 */
    uint64_t last_monitor_time;       /* 上次监控时间 */
};

/* ========== QoS操作接口 ========== */
struct QoSOperations {
    /* 初始化 */
    int (*qos_init)(struct QoSChannelController *qos_ctrl, 
                    uint32_t buffer_size, uint16_t queue_slots);
    
    /* QoS配置 */
    struct ServiceQoSProfile* (*get_qos_profile)(uint16_t service_id);
    int (*update_qos_profile)(uint16_t service_id, 
                              struct ServiceQoSProfile *profile);
    
    /* 消息分类 */
    enum QoSClass (*classify_message)(struct Msg *msg);
    
    /* 资源分配 */
    int (*allocate_buffer)(struct QoSChannelController *qos_ctrl, 
                          enum QoSClass qos_class, uint32_t size);
    int (*release_buffer)(struct QoSChannelController *qos_ctrl, 
                         enum QoSClass qos_class, uint32_t size);
    
    /* QoS感知调度 */
    struct Msg* (*qos_schedule)(struct QoSChannelController *qos_ctrl);
    int (*qos_enqueue)(struct QoSChannelController *qos_ctrl, 
                      struct QoSMsg *qos_msg);
    struct QoSMsg* (*qos_dequeue)(struct QoSChannelController *qos_ctrl, 
                                  enum QoSClass qos_class);
    
    /* 性能监控 */
    int (*update_metrics)(struct QoSMetrics *metrics, 
                         struct QoSMsg *qos_msg, uint8_t success);
    int (*check_qos_violation)(struct QoSMetrics *metrics, 
                              struct ServiceQoSProfile *profile);
    void (*print_qos_stats)(struct QoSChannelController *qos_ctrl);
    
    /* 自适应调整 */
    int (*adapt_qos_params)(struct QoSChannelController *qos_ctrl);
};

extern struct QoSOperations qos_ops;

/* ========== 预定义QoS配置表 ========== */
extern struct ServiceQoSProfile default_qos_profiles[];
extern const int default_qos_profiles_count;

/* ========== 工具函数 ========== */
static inline uint64_t get_timestamp_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000;
}

static inline uint64_t timespec_diff_us(struct timespec *start, struct timespec *end) {
    return (end->tv_sec - start->tv_sec) * 1000000ULL + 
           (end->tv_nsec - start->tv_nsec) / 1000;
}

#endif /* _QOS_H_ */
