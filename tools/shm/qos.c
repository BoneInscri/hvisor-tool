#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../include/shm/qos.h"
#include "../include/service/safe_service.h"

/* ========== QoS配置表 - 基于现有服务ID映射 ========== */
struct ServiceQoSProfile default_qos_profiles[] = {
    /* 服务ID 0: NULL服务 - 尽力而为 */
    {
        .service_id = 0,
        .qos_class = QOS_BEST_EFFORT,
        .max_latency_us = 10000,
        .min_bandwidth_mbps = 1,
        .max_jitter_us = 5000,
        .loss_tolerance = 0.1,
        .reserved_buffer_size = 1024,
        .reserved_queue_slots = 2,
        .preemption_enabled = 0,
        .batch_enabled = 0,
        .batch_size = 1,
        .batch_timeout_us = 0
    },
    
    /* 服务ID 1: Echo字符串 - 实时类 */
    {
        .service_id = 1,
        .qos_class = QOS_REALTIME,
        .max_latency_us = 100,      /* 100微秒最大延迟 */
        .min_bandwidth_mbps = 10,
        .max_jitter_us = 50,
        .loss_tolerance = 0.01,     /* 1%丢包容忍 */
        .reserved_buffer_size = 4096,
        .reserved_queue_slots = 4,
        .preemption_enabled = 1,    /* 允许抢占 */
        .batch_enabled = 0,
        .batch_size = 1,
        .batch_timeout_us = 0
    },
    
    /* 服务ID 2: Flip数字 - 实时类 */
    {
        .service_id = 2,
        .qos_class = QOS_REALTIME,
        .max_latency_us = 200,
        .min_bandwidth_mbps = 5,
        .max_jitter_us = 100,
        .loss_tolerance = 0.01,
        .reserved_buffer_size = 2048,
        .reserved_queue_slots = 3,
        .preemption_enabled = 1,
        .batch_enabled = 0,
        .batch_size = 1,
        .batch_timeout_us = 0
    },
    
    /* 服务ID 3-8: 加密/解密服务 - 可靠类 */
    {
        .service_id = 3,  /* Caesar加密 */
        .qos_class = QOS_RELIABLE,
        .max_latency_us = 500,
        .min_bandwidth_mbps = 20,
        .max_jitter_us = 200,
        .loss_tolerance = 0.001,    /* 0.1%丢包容忍 - 可靠性要求高 */
        .reserved_buffer_size = 8192,
        .reserved_queue_slots = 6,
        .preemption_enabled = 0,
        .batch_enabled = 0,
        .batch_size = 1,
        .batch_timeout_us = 0
    },
    {
        .service_id = 4,  /* Caesar解密 */
        .qos_class = QOS_RELIABLE,
        .max_latency_us = 500,
        .min_bandwidth_mbps = 20,
        .max_jitter_us = 200,
        .loss_tolerance = 0.001,
        .reserved_buffer_size = 8192,
        .reserved_queue_slots = 6,
        .preemption_enabled = 0,
        .batch_enabled = 0,
        .batch_size = 1,
        .batch_timeout_us = 0
    },
    {
        .service_id = 5,  /* XOR加密 */
        .qos_class = QOS_RELIABLE,
        .max_latency_us = 300,
        .min_bandwidth_mbps = 30,
        .max_jitter_us = 150,
        .loss_tolerance = 0.001,
        .reserved_buffer_size = 8192,
        .reserved_queue_slots = 6,
        .preemption_enabled = 0,
        .batch_enabled = 0,
        .batch_size = 1,
        .batch_timeout_us = 0
    },
    {
        .service_id = 6,  /* XOR解密 */
        .qos_class = QOS_RELIABLE,
        .max_latency_us = 300,
        .min_bandwidth_mbps = 30,
        .max_jitter_us = 150,
        .loss_tolerance = 0.001,
        .reserved_buffer_size = 8192,
        .reserved_queue_slots = 6,
        .preemption_enabled = 0,
        .batch_enabled = 0,
        .batch_size = 1,
        .batch_timeout_us = 0
    },
    {
        .service_id = 7,  /* ROT13加密 */
        .qos_class = QOS_RELIABLE,
        .max_latency_us = 400,
        .min_bandwidth_mbps = 15,
        .max_jitter_us = 200,
        .loss_tolerance = 0.001,
        .reserved_buffer_size = 4096,
        .reserved_queue_slots = 4,
        .preemption_enabled = 0,
        .batch_enabled = 0,
        .batch_size = 1,
        .batch_timeout_us = 0
    },
    {
        .service_id = 8,  /* ROT13解密 */
        .qos_class = QOS_RELIABLE,
        .max_latency_us = 400,
        .min_bandwidth_mbps = 15,
        .max_jitter_us = 200,
        .loss_tolerance = 0.001,
        .reserved_buffer_size = 4096,
        .reserved_queue_slots = 4,
        .preemption_enabled = 0,
        .batch_enabled = 0,
        .batch_size = 1,
        .batch_timeout_us = 0
    },
    
    /* 服务ID 9-11: Hash函数 - 吞吐类 */
    {
        .service_id = 9,  /* DJB2 Hash */
        .qos_class = QOS_THROUGHPUT,
        .max_latency_us = 1000,
        .min_bandwidth_mbps = 100,   /* 高带宽要求 */
        .max_jitter_us = 500,
        .loss_tolerance = 0.05,      /* 5%丢包容忍 */
        .reserved_buffer_size = 32768,
        .reserved_queue_slots = 16,
        .preemption_enabled = 0,
        .batch_enabled = 1,          /* 启用批量处理 */
        .batch_size = 10,
        .batch_timeout_us = 2000
    },
    {
        .service_id = 10,  /* CRC32 Hash */
        .qos_class = QOS_THROUGHPUT,
        .max_latency_us = 1000,
        .min_bandwidth_mbps = 100,
        .max_jitter_us = 500,
        .loss_tolerance = 0.05,
        .reserved_buffer_size = 32768,
        .reserved_queue_slots = 16,
        .preemption_enabled = 0,
        .batch_enabled = 1,
        .batch_size = 10,
        .batch_timeout_us = 2000
    },
    {
        .service_id = 11,  /* FNV Hash */
        .qos_class = QOS_THROUGHPUT,
        .max_latency_us = 1000,
        .min_bandwidth_mbps = 100,
        .max_jitter_us = 500,
        .loss_tolerance = 0.05,
        .reserved_buffer_size = 32768,
        .reserved_queue_slots = 16,
        .preemption_enabled = 0,
        .batch_enabled = 1,
        .batch_size = 10,
        .batch_timeout_us = 2000
    },
    
    /* 服务ID 12-15: 随机数生成 - 尽力而为 */
    {
        .service_id = 12,  /* XORSHIFT随机数 */
        .qos_class = QOS_BEST_EFFORT,
        .max_latency_us = 5000,
        .min_bandwidth_mbps = 5,
        .max_jitter_us = 2000,
        .loss_tolerance = 0.1,
        .reserved_buffer_size = 2048,
        .reserved_queue_slots = 4,
        .preemption_enabled = 0,
        .batch_enabled = 1,
        .batch_size = 5,
        .batch_timeout_us = 5000
    },
    {
        .service_id = 13,  /* LCG随机数 */
        .qos_class = QOS_BEST_EFFORT,
        .max_latency_us = 5000,
        .min_bandwidth_mbps = 5,
        .max_jitter_us = 2000,
        .loss_tolerance = 0.1,
        .reserved_buffer_size = 2048,
        .reserved_queue_slots = 4,
        .preemption_enabled = 0,
        .batch_enabled = 1,
        .batch_size = 5,
        .batch_timeout_us = 5000
    },
    {
        .service_id = 14,  /* 随机字符串 */
        .qos_class = QOS_BEST_EFFORT,
        .max_latency_us = 5000,
        .min_bandwidth_mbps = 5,
        .max_jitter_us = 2000,
        .loss_tolerance = 0.1,
        .reserved_buffer_size = 4096,
        .reserved_queue_slots = 4,
        .preemption_enabled = 0,
        .batch_enabled = 1,
        .batch_size = 5,
        .batch_timeout_us = 5000
    },
    {
        .service_id = 15,  /* 随机字符 */
        .qos_class = QOS_BEST_EFFORT,
        .max_latency_us = 5000,
        .min_bandwidth_mbps = 5,
        .max_jitter_us = 2000,
        .loss_tolerance = 0.1,
        .reserved_buffer_size = 1024,
        .reserved_queue_slots = 2,
        .preemption_enabled = 0,
        .batch_enabled = 1,
        .batch_size = 5,
        .batch_timeout_us = 5000
    }
};

const int default_qos_profiles_count = sizeof(default_qos_profiles) / sizeof(default_qos_profiles[0]);

/* ========== QoS操作实现 ========== */

/**
 * QoS控制器初始化
 */
static int qos_init_impl(struct QoSChannelController *qos_ctrl, 
                        uint32_t buffer_size, uint16_t queue_slots) {
    if (qos_ctrl == NULL) {
        return -1;
    }
    
    memset(qos_ctrl, 0, sizeof(struct QoSChannelController));
    
    /* 初始化全局资源 */
    qos_ctrl->total_buffer_size = buffer_size;
    qos_ctrl->available_buffer = buffer_size;
    qos_ctrl->total_queue_slots = queue_slots;
    qos_ctrl->available_slots = queue_slots;
    
    /* 为每个QoS类别预分配资源 */
    /* 实时类: 20%缓冲区, 25%队列 */
    qos_ctrl->queues[QOS_REALTIME].reserved_buffer = buffer_size * 20 / 100;
    qos_ctrl->queues[QOS_REALTIME].reserved_slots = queue_slots * 25 / 100;
    qos_ctrl->queues[QOS_REALTIME].capacity = (queue_slots * 25 / 100) < MAX_QOS_QUEUE_SIZE ? 
                                               (queue_slots * 25 / 100) : MAX_QOS_QUEUE_SIZE;
    
    /* 吞吐类: 50%缓冲区, 40%队列 */
    qos_ctrl->queues[QOS_THROUGHPUT].reserved_buffer = buffer_size * 50 / 100;
    qos_ctrl->queues[QOS_THROUGHPUT].reserved_slots = queue_slots * 40 / 100;
    qos_ctrl->queues[QOS_THROUGHPUT].capacity = (queue_slots * 40 / 100) < MAX_QOS_QUEUE_SIZE ? 
                                                 (queue_slots * 40 / 100) : MAX_QOS_QUEUE_SIZE;
    
    /* 可靠类: 20%缓冲区, 25%队列 */
    qos_ctrl->queues[QOS_RELIABLE].reserved_buffer = buffer_size * 20 / 100;
    qos_ctrl->queues[QOS_RELIABLE].reserved_slots = queue_slots * 25 / 100;
    qos_ctrl->queues[QOS_RELIABLE].capacity = (queue_slots * 25 / 100) < MAX_QOS_QUEUE_SIZE ? 
                                               (queue_slots * 25 / 100) : MAX_QOS_QUEUE_SIZE;
    
    /* 尽力而为: 10%缓冲区, 10%队列 */
    qos_ctrl->queues[QOS_BEST_EFFORT].reserved_buffer = buffer_size * 10 / 100;
    qos_ctrl->queues[QOS_BEST_EFFORT].reserved_slots = queue_slots * 10 / 100;
    qos_ctrl->queues[QOS_BEST_EFFORT].capacity = (queue_slots * 10 / 100) < MAX_QOS_QUEUE_SIZE ? 
                                                  (queue_slots * 10 / 100) : MAX_QOS_QUEUE_SIZE;
    
    /* 设置调度权重 (实时:吞吐:可靠:尽力而为 = 4:2:2:1) */
    qos_ctrl->class_weight[QOS_REALTIME] = 4;
    qos_ctrl->class_weight[QOS_THROUGHPUT] = 2;
    qos_ctrl->class_weight[QOS_RELIABLE] = 2;
    qos_ctrl->class_weight[QOS_BEST_EFFORT] = 1;
    
    /* 初始化信用值 */
    qos_ctrl->class_credits[QOS_REALTIME] = 4;
    qos_ctrl->class_credits[QOS_THROUGHPUT] = 2;
    qos_ctrl->class_credits[QOS_RELIABLE] = 2;
    qos_ctrl->class_credits[QOS_BEST_EFFORT] = 1;
    
    /* 设置时间片 (微秒) */
    qos_ctrl->class_quantum[QOS_REALTIME] = 100;      /* 100us */
    qos_ctrl->class_quantum[QOS_THROUGHPUT] = 1000;   /* 1ms */
    qos_ctrl->class_quantum[QOS_RELIABLE] = 500;      /* 500us */
    qos_ctrl->class_quantum[QOS_BEST_EFFORT] = 2000;  /* 2ms */
    
    qos_ctrl->current_class = QOS_REALTIME;
    qos_ctrl->last_monitor_time = get_timestamp_us();
    
    printf("[QoS] Initialized - Buffer: %u bytes, Slots: %u\n", 
           buffer_size, queue_slots);
    printf("[QoS] Resource allocation:\n");
    printf("  REALTIME:    %u bytes (%u slots)\n", 
           qos_ctrl->queues[QOS_REALTIME].reserved_buffer,
           qos_ctrl->queues[QOS_REALTIME].reserved_slots);
    printf("  THROUGHPUT:  %u bytes (%u slots)\n", 
           qos_ctrl->queues[QOS_THROUGHPUT].reserved_buffer,
           qos_ctrl->queues[QOS_THROUGHPUT].reserved_slots);
    printf("  RELIABLE:    %u bytes (%u slots)\n", 
           qos_ctrl->queues[QOS_RELIABLE].reserved_buffer,
           qos_ctrl->queues[QOS_RELIABLE].reserved_slots);
    printf("  BEST_EFFORT: %u bytes (%u slots)\n", 
           qos_ctrl->queues[QOS_BEST_EFFORT].reserved_buffer,
           qos_ctrl->queues[QOS_BEST_EFFORT].reserved_slots);
    
    return 0;
}

/**
 * 获取服务QoS配置
 */
static struct ServiceQoSProfile* get_qos_profile_impl(uint16_t service_id) {
    for (int i = 0; i < default_qos_profiles_count; i++) {
        if (default_qos_profiles[i].service_id == service_id) {
            return &default_qos_profiles[i];
        }
    }
    
    /* 未找到则返回默认配置 (ID=0) */
    return &default_qos_profiles[0];
}

/**
 * 消息分类 - 根据service_id映射到QoS类别
 */
static enum QoSClass classify_message_impl(struct Msg *msg) {
    if (msg == NULL) {
        return QOS_BEST_EFFORT;
    }
    
    struct ServiceQoSProfile *profile = get_qos_profile_impl(msg->service_id);
    return profile->qos_class;
}

/**
 * QoS消息入队
 */
static int qos_enqueue_impl(struct QoSChannelController *qos_ctrl, 
                           struct QoSMsg *qos_msg) {
    if (qos_ctrl == NULL || qos_msg == NULL) {
        return -1;
    }
    
    enum QoSClass qos_class = qos_msg->qos_class;
    if (qos_class >= QOS_CLASS_COUNT) {
        printf("[QoS] Invalid QoS class: %d\n", qos_class);
        return -1;
    }
    
    struct QoSQueue *queue = &qos_ctrl->queues[qos_class];
    
    /* 检查队列是否已满 */
    if (queue->size >= queue->capacity) {
        printf("[QoS] Queue full for class %d (size: %u, capacity: %u)\n",
               qos_class, queue->size, queue->capacity);
        return -1;
    }
    
    /* 设置发送时间戳 */
    if (qos_msg->send_timestamp == 0) {
        qos_msg->send_timestamp = get_timestamp_us();
    }
    
    /* 将消息复制到队列尾部 */
    queue->messages[queue->tail] = *qos_msg;
    
    /* 更新队列尾指针 (环形队列) */
    queue->tail = (queue->tail + 1) % queue->capacity;
    queue->size++;
    
    return 0;
}

/**
 * QoS消息出队 - P1修复: 增加边界检查
 */
static struct QoSMsg* qos_dequeue_impl(struct QoSChannelController *qos_ctrl, 
                                      enum QoSClass qos_class) {
    if (qos_ctrl == NULL || qos_class >= QOS_CLASS_COUNT) {
        return NULL;
    }
    
    struct QoSQueue *queue = &qos_ctrl->queues[qos_class];
    
    /* 检查队列是否为空 */
    if (queue->size == 0) {
        return NULL;
    }
    
    /* P1修复: 边界检查 - 防止head越界 */
    if (queue->head >= queue->capacity) {
        printf("[QoS] ERROR: Invalid queue head during dequeue: %u >= %u\n", 
               queue->head, queue->capacity);
        /* 尝试修复：重置head */
        queue->head = 0;
        return NULL;
    }
    
    /* 获取队列头部消息 */
    struct QoSMsg *msg = &queue->messages[queue->head];
    
    /* 更新队列头指针 (环形队列) */
    queue->head = (queue->head + 1) % queue->capacity;
    queue->size--;
    
    return msg;
}

/**
 * 分配缓冲区资源 - P0-CRITICAL-9修复版本
 * 
 * 正确的资源预留逻辑:
 * 1. 优先使用该QoS类别的预留资源（保证QoS）
 * 2. 预留资源不足时，使用全局共享资源
 * 3. 都不足时才失败
 */
static int allocate_buffer_impl(struct QoSChannelController *qos_ctrl, 
                               enum QoSClass qos_class, uint32_t size) {
    if (qos_ctrl == NULL || qos_class >= QOS_CLASS_COUNT) {
        return -1;
    }
    
    struct QoSQueue *queue = &qos_ctrl->queues[qos_class];
    
    /* P0-CRITICAL-9修复: 正确的资源预留逻辑 */
    if (queue->reserved_buffer >= size) {
        /* 方案1: 使用预留资源（保证QoS，高优先级队列的特权） */
        queue->reserved_buffer -= size;
        printf("[QoS] Allocated %u bytes from reserved buffer (class %d)\n", size, qos_class);
        return 0;
    } else if (qos_ctrl->available_buffer >= size) {
        /* 方案2: 使用全局共享资源（预留资源已用完） */
        qos_ctrl->available_buffer -= size;
        printf("[QoS] Allocated %u bytes from shared buffer (class %d)\n", size, qos_class);
        return 0;
    } else {
        /* 方案3: 真正的资源耗尽 */
        printf("[QoS] ERROR: Out of buffer (requested: %u, reserved: %u, available: %u)\n",
               size, queue->reserved_buffer, qos_ctrl->available_buffer);
        return -1;
    }
}

/**
 * 释放缓冲区资源 - P0-CRITICAL-9修复版本
 * 
 * 正确的资源释放逻辑:
 * 1. 优先补充预留资源（恢复QoS保障能力）
 * 2. 预留资源满了，补充到全局共享池
 */
static int release_buffer_impl(struct QoSChannelController *qos_ctrl, 
                               enum QoSClass qos_class, uint32_t size) {
    if (qos_ctrl == NULL || qos_class >= QOS_CLASS_COUNT) {
        return -1;
    }
    
    struct QoSQueue *queue = &qos_ctrl->queues[qos_class];
    
    /* P0-CRITICAL-9修复: 计算初始预留资源大小 */
    uint32_t initial_reserved = qos_ctrl->total_buffer_size * 
        (qos_class == QOS_REALTIME ? 20 :
         qos_class == QOS_THROUGHPUT ? 50 :
         qos_class == QOS_RELIABLE ? 20 : 10) / 100;
    
    /* 优先补充预留资源到初始水平 */
    if (queue->reserved_buffer < initial_reserved) {
        uint32_t can_restore = initial_reserved - queue->reserved_buffer;
        uint32_t restore_amount = (size < can_restore) ? size : can_restore;
        
        queue->reserved_buffer += restore_amount;
        size -= restore_amount;
        printf("[QoS] Restored %u bytes to reserved buffer (class %d)\n", restore_amount, qos_class);
    }
    
    /* 剩余部分归还到全局共享池 */
    if (size > 0) {
        qos_ctrl->available_buffer += size;
        printf("[QoS] Released %u bytes to shared buffer\n", size);
    }
    
    return 0;
}

/**
 * 加权轮询调度算法 (Weighted Round-Robin) - P0修复版本
 * 
 * 正确的WRR实现逻辑:
 * 1. 初始化每个队列的信用值 (RT=4, TP=2, RL=2, BE=1)
 * 2. 按优先级顺序轮询，有消息且有信用的队列可调度
 * 3. 每次调度消耗1个信用
 * 4. 当所有队列信用都为0时，统一重置所有信用（防止高优先级饿死低优先级）
 */
static struct Msg* qos_schedule_impl(struct QoSChannelController *qos_ctrl) {
    if (qos_ctrl == NULL) {
        return NULL;
    }
    
    /* 使用控制器内部的信用值数组，支持多控制器 */
    uint32_t *class_credits = qos_ctrl->class_credits;
    
    /* P0修复: 检查是否所有信用都已用完（需要重置） */
    int all_credits_zero = 1;
    int has_messages = 0;
    
    for (int i = 0; i < QOS_CLASS_COUNT; i++) {
        if (class_credits[i] > 0) {
            all_credits_zero = 0;
        }
        if (qos_ctrl->queues[i].size > 0) {
            has_messages = 1;
        }
    }
    
    /* P0修复: 如果有消息但所有信用都为0，统一重置所有信用 */
    if (all_credits_zero && has_messages) {
        for (int i = 0; i < QOS_CLASS_COUNT; i++) {
            class_credits[i] = qos_ctrl->class_weight[i];
        }
    }
    
    /* P0修复: 按优先级顺序检查各队列（RT > TP > RL > BE） */
    for (int i = 0; i < QOS_CLASS_COUNT; i++) {
        enum QoSClass qos_class = (enum QoSClass)i;
        struct QoSQueue *queue = &qos_ctrl->queues[qos_class];
        
        /* 如果队列有消息且有信用值 */
        if (queue->size > 0 && class_credits[qos_class] > 0) {
            /* P0修复: 只消耗信用，不立即重置（避免高优先级永久霸占） */
            class_credits[qos_class]--;
            
            qos_ctrl->current_class = qos_class;
            
            /* P1修复: 增加head边界检查 */
            if (queue->head >= queue->capacity) {
                printf("[QoS] ERROR: Invalid queue head: %u >= %u\n", 
                       queue->head, queue->capacity);
                return NULL;
            }
            
            /* 返回队列头部消息的base_msg指针（不出队，由调用者决定何时出队） */
            return &queue->messages[queue->head].base_msg;
        }
    }
    
    return NULL;
}

/**
 * 更新QoS统计指标
 */
static int update_metrics_impl(struct QoSMetrics *metrics, 
                              struct QoSMsg *qos_msg, uint8_t success) {
    if (metrics == NULL || qos_msg == NULL) {
        return -1;
    }
    
    uint64_t now = get_timestamp_us();
    uint64_t latency = now - qos_msg->send_timestamp;
    
    /* 更新延迟统计 */
    metrics->total_latency_us += latency;
    metrics->total_messages++;
    metrics->avg_latency_us = metrics->total_latency_us / metrics->total_messages;
    
    if (latency < metrics->min_latency_us || metrics->min_latency_us == 0) {
        metrics->min_latency_us = latency;
    }
    if (latency > metrics->max_latency_us) {
        metrics->max_latency_us = latency;
    }
    
    /* 计算抖动 (与平均延迟的偏差) */
    uint64_t deviation = (latency > metrics->avg_latency_us) ? 
                         (latency - metrics->avg_latency_us) : 
                         (metrics->avg_latency_us - latency);
    metrics->jitter_us = (metrics->jitter_us * 7 + deviation) / 8;  /* 指数平滑 */
    
    /* 更新吞吐量 */
    metrics->total_bytes += qos_msg->base_msg.length;
    struct timespec ts_now;
    clock_gettime(CLOCK_MONOTONIC, &ts_now);
    uint64_t time_window_us = timespec_diff_us(&metrics->window_start, &ts_now);
    if (time_window_us > 0) {
        metrics->throughput_bps = (metrics->total_bytes * 8 * 1000000ULL) / time_window_us;
    }
    
    /* 更新可靠性统计 */
    metrics->sent_count++;
    if (success) {
        metrics->success_count++;
    } else {
        metrics->failed_count++;
    }
    metrics->packet_loss_rate = (float)metrics->failed_count / metrics->sent_count;
    
    metrics->last_update = ts_now;
    
    return 0;
}

/**
 * 检查QoS违约
 */
static int check_qos_violation_impl(struct QoSMetrics *metrics, 
                                   struct ServiceQoSProfile *profile) {
    if (metrics == NULL || profile == NULL) {
        return 0;
    }
    
    int violations = 0;
    
    /* 检查延迟违约 */
    if (metrics->avg_latency_us > profile->max_latency_us) {
        metrics->latency_violations++;
        violations++;
        printf("[QoS] VIOLATION: Latency exceeded - avg: %lu us, max allowed: %u us\n",
               metrics->avg_latency_us, profile->max_latency_us);
    }
    
    /* 检查带宽违约 */
    uint64_t bandwidth_mbps = metrics->throughput_bps / 1000000;
    if (bandwidth_mbps < profile->min_bandwidth_mbps) {
        metrics->bandwidth_violations++;
        violations++;
        printf("[QoS] VIOLATION: Bandwidth below minimum - current: %lu Mbps, min: %u Mbps\n",
               bandwidth_mbps, profile->min_bandwidth_mbps);
    }
    
    /* 检查丢包率违约 */
    if (metrics->packet_loss_rate > profile->loss_tolerance) {
        violations++;
        printf("[QoS] VIOLATION: Packet loss exceeded - current: %.2f%%, max: %.2f%%\n",
               metrics->packet_loss_rate * 100, profile->loss_tolerance * 100);
    }
    
    metrics->total_violations += violations;
    
    return violations;
}

/**
 * 打印QoS统计信息
 */
static void print_qos_stats_impl(struct QoSChannelController *qos_ctrl) {
    if (qos_ctrl == NULL) {
        return;
    }
    
    printf("\n========== QoS Statistics ==========\n");
    printf("Total processed messages: %lu\n\n", qos_ctrl->total_processed);
    
    const char* qos_class_names[] = {"REALTIME", "THROUGHPUT", "RELIABLE", "BEST_EFFORT"};
    
    for (int i = 0; i < QOS_CLASS_COUNT; i++) {
        struct QoSQueue *queue = &qos_ctrl->queues[i];
        struct QoSMetrics *m = &queue->metrics;
        
        printf("--- %s ---\n", qos_class_names[i]);
        printf("  Queue: size=%u/%u, reserved_slots=%u\n", 
               queue->size, queue->capacity, queue->reserved_slots);
        printf("  Latency: avg=%lu us, min=%lu us, max=%lu us, jitter=%lu us\n",
               m->avg_latency_us, m->min_latency_us, m->max_latency_us, m->jitter_us);
        printf("  Throughput: %lu bps (%.2f Mbps)\n", 
               m->throughput_bps, (float)m->throughput_bps / 1000000);
        printf("  Messages: total=%lu, success=%u, failed=%u, dropped=%u\n",
               m->total_messages, m->success_count, m->failed_count, m->dropped_count);
        printf("  Loss rate: %.2f%%\n", m->packet_loss_rate * 100);
        printf("  Violations: latency=%u, bandwidth=%u, total=%u\n\n",
               m->latency_violations, m->bandwidth_violations, m->total_violations);
    }
    
    printf("Available resources: buffer=%u/%u bytes, slots=%u/%u\n",
           qos_ctrl->available_buffer, qos_ctrl->total_buffer_size,
           qos_ctrl->available_slots, qos_ctrl->total_queue_slots);
    printf("====================================\n\n");
}

/**
 * 自适应QoS参数调整
 */
static int adapt_qos_params_impl(struct QoSChannelController *qos_ctrl) {
    if (qos_ctrl == NULL) {
        return -1;
    }
    
    uint64_t now = get_timestamp_us();
    uint64_t monitor_interval = now - qos_ctrl->last_monitor_time;
    
    /* 每秒调整一次 */
    if (monitor_interval < 1000000) {
        return 0;
    }
    
    printf("[QoS] Adaptive adjustment triggered...\n");
    
    for (int i = 0; i < QOS_CLASS_COUNT; i++) {
        struct QoSQueue *queue = &qos_ctrl->queues[i];
        struct QoSMetrics *m = &queue->metrics;
        
        /* 如果队列使用率高(>80%)，增加预留资源 */
        float usage_rate = (float)queue->size / queue->capacity;
        if (usage_rate > 0.8 && qos_ctrl->available_slots > 0) {
            uint16_t additional_slots = qos_ctrl->available_slots / 4;
            queue->reserved_slots += additional_slots;
            queue->capacity += additional_slots;
            qos_ctrl->available_slots -= additional_slots;
            
            printf("[QoS] Increased capacity for class %d: +%u slots\n", i, additional_slots);
        }
        
        /* 如果延迟违约频繁(>5次/秒)，提升权重 */
        if (m->latency_violations > 5 && qos_ctrl->class_weight[i] < 10) {
            qos_ctrl->class_weight[i]++;
            printf("[QoS] Increased weight for class %d: %u\n", i, qos_ctrl->class_weight[i]);
        }
    }
    
    qos_ctrl->last_monitor_time = now;
    
    return 0;
}

/* ========== 操作接口导出 ========== */
struct QoSOperations qos_ops = {
    .qos_init = qos_init_impl,
    .get_qos_profile = get_qos_profile_impl,
    .classify_message = classify_message_impl,
    .allocate_buffer = allocate_buffer_impl,
    .release_buffer = release_buffer_impl,
    .qos_schedule = qos_schedule_impl,
    .qos_enqueue = qos_enqueue_impl,
    .qos_dequeue = qos_dequeue_impl,
    .update_metrics = update_metrics_impl,
    .check_qos_violation = check_qos_violation_impl,
    .print_qos_stats = print_qos_stats_impl,
    .adapt_qos_params = adapt_qos_params_impl
};
