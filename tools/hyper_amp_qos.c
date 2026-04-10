/*
 * HyperAMP QoS Client and Server Implementation
 * 
 * This module provides QoS-aware HyperAMP communication:
 * - Client: Sends requests with QoS classification and statistics
 * - Server: Processes requests using three-phase batch scheduling
 * 
 * Author: HyperAMP Team
 * Date: 2025-10-31
 */

#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <poll.h>
#include <stdbool.h>

#include "def.h"
#include "hvisor.h"
#include "shm/qos.h"
#include "shm/msgqueue.h"
#include "shm/channel.h"
#include "shm/client.h"
#include "shm/addr.h"
#include "shm/config/config_zone.h"
#include "shm/config/config_msgqueue.h"
#include "shm/config/config_addr.h"
#include "shm/config/config_common.h"
#include "service/safe_service.h"
#include "hyper_amp_qos.h"

// 全局变量
static struct QoSChannelController global_qos_ctrl;
static struct QoSChannelController server_qos_ctrl;
static int qos_initialized = 0;
static int server_qos_initialized = 0;
static volatile int running = 1;

// 信号处理
static void signal_handler(int signum) {
    printf("\n[QoS] Received signal %d, shutting down gracefully...\n", signum);
    running = 0;
}

// 外部依赖的操作函数（在其他模块中定义）
extern struct ClientOps client_ops;
extern struct ChannelOps channel_ops;
extern struct MsgQueueOps msg_queue_ops;
extern struct QoSOperations qos_ops;
extern void parse_global_addr(char *shm_json_path);

// ===== HyperAMP 安全服务加解密函数 =====

/**
 * AES-like 加密函数 (简化版) - Service ID 1
 * 使用简单的字节替换和位移操作模拟AES加密
 */
// HyperAMP加密服务实现（简单XOR加密）- 导出给QoS模块使用
int hyperamp_encrypt_service(char* data, int data_len, int max_len) {
    if (data == NULL || data_len <= 0 || max_len <= 0) {
        return -1;
    }
    
    printf("[HyperAMP] Executing encryption service (Service ID: 1)\n");
    printf("[HyperAMP] Input data length: %d bytes\n", data_len);
    
    // 显示原始数据
    printf("[HyperAMP] Original data: ");
    for (int i = 0; i < data_len; i++) {
        printf("0x%02x ", (unsigned char)data[i]);
    }
    printf("\n");    
    // 简化的加密密钥 (16字节)
    const unsigned char key[16] = {
        0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
        0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c
    };
    
    // 加密过程：多轮字节替换和XOR操作
        for (int i = 0; i < data_len; i++) {
            unsigned char byte = (unsigned char)data[i];
            
            // 简单XOR加密
            byte ^= key[i % 16];
            
            // // Round 2: 字节替换 (S-Box模拟)
            // byte = ((byte << 1) | (byte >> 7)) & 0xFF;  // 循环左移1位
            byte ^= 0x55;// 额外的固定XOR
            // // Round 3: 再次XOR
            // byte ^= key[(i + round) % 16];
            
            data[i] = (char)byte;
        }
    // 显示加密后数据
    printf("[HyperAMP] Encrypted data: ");
    for (int i = 0; i < data_len; i++) {
        printf("0x%02x ", (unsigned char)data[i]);
    }
    printf("\n");

    printf("[HyperAMP] Encryption completed successfully\n");
    return 0;
}

/**
 * AES-like 解密函数 (简化版) - Service ID 2  
 * 对应加密函数的逆向操作
 */
// HyperAMP解密服务实现（简单XOR解密）- 导出给QoS模块使用
int hyperamp_decrypt_service(char* data, int data_len, int max_len) {
    if (data == NULL || data_len <= 0 || max_len <= 0) {
        return -1;
    }
    
    printf("[HyperAMP] Executing decryption service (Service ID: 2)\n");
    printf("[HyperAMP] Input data length: %d bytes\n", data_len);
    
    // 显示加密数据
    printf("[HyperAMP] Encrypted data: ");
    for (int i = 0; i < data_len; i++) {
        printf("0x%02x ", (unsigned char)data[i]);
    }
    printf("\n");
    
    // 相同的解密密钥
    const unsigned char key[16] = {
        0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
        0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c
    };
    
    // 解密过程：完全逆向加密操作
    for (int i = 0; i < data_len; i++) {
        unsigned char byte = (unsigned char)data[i];
        
        // 逆向操作（与加密顺序相反）
        byte ^= 0x55;                // 逆向固定XOR
        byte ^= key[i % 16];         // 逆向密钥XOR
        
        data[i] = (char)byte;
    }
    
    // 显示解密后数据
    printf("[HyperAMP] Decrypted data: ");
    for (int i = 0; i < data_len; i++) {
        printf("0x%02x ", (unsigned char)data[i]);
    }
    printf("\n");
    
    printf("[HyperAMP] Decryption completed successfully\n");
    return 0;
}

// addr_infos 已经在 config_addr.h 中声明为 extern
// 我们通过包含的头文件可以直接使用

/**
 * P0修复: 打印QoS统计信息
 * 支持命令: ./hvisor shm qos_stats
 */
int hyper_amp_qos_print_stats(void) {
    printf("\n");
    printf("========================================\n");
    printf("  QoS System Statistics Report\n");
    printf("========================================\n\n");
    
    /* 打印客户端QoS控制器统计 */
    if (qos_initialized) {
        printf("[Client QoS Controller]\n");
        qos_ops.print_qos_stats(&global_qos_ctrl);
    } else {
        printf("[Client QoS Controller] Not initialized\n");
    }
    
    printf("\n");
    
    /* 打印服务端QoS控制器统计 */
    if (server_qos_initialized) {
        printf("[Server QoS Controller]\n");
        qos_ops.print_qos_stats(&server_qos_ctrl);
    } else {
        printf("[Server QoS Controller] Not initialized\n");
    }
    
    printf("\n");
    printf("========================================\n");
    printf("  End of QoS Statistics Report\n");
    printf("========================================\n\n");
    
    return 0;
}

/**
 * HyperAMP QoS Client - 发送带QoS分类和统计的请求
 * 
 * @param shm_json_path 共享内存配置文件路径
 * @param data_input 输入数据（字符串或 @文件名）
 * @param service_id 服务ID
 * @return 0表示成功，-1表示失败
 */
int hyper_amp_qos_client(char* shm_json_path, char* data_input, uint32_t service_id) {
    printf("=== HyperAMP QoS Client ===\n");
    printf("Initializing QoS-aware client...\n");
    
    // step 1: 解析配置
    parse_global_addr(shm_json_path);
    printf("[QoS-Client] Configuration parsed\n");
    
    // step 2: 初始化客户端
    struct Client amp_client = {0};
    if (client_ops.client_init(&amp_client, ZONE_NPUcore_ID) != 0) {
        printf("[Error] Failed to initialize client\n");
        return -1;
    }
    printf("[QoS-Client] Client initialized\n");
    
    // step 3: 初始化QoS控制器（P2: 增强错误处理）
    if (!qos_initialized) {
        int qos_init_result = qos_ops.qos_init(&global_qos_ctrl, 1024*1024, 64);
        if (qos_init_result == 0) {
            qos_initialized = 1;
            printf("[QoS-Client] QoS controller initialized (1MB buffer, 64 slots)\n");
        } else {
            printf("[Warning] QoS initialization failed (error: %d), continuing without QoS\n", qos_init_result);
        }
    }
    
    // step 4: 准备数据（P2: 增强参数验证）
    if (data_input == NULL || strlen(data_input) == 0) {
        printf("[Error] Invalid data input (NULL or empty)\n");
        client_ops.client_destory(&amp_client);
        return -1;
    }
    
    char* data_to_send = NULL;
    uint32_t data_size = 0;
    
    if (data_input[0] == '@') {
        // 从文件读取
        char* filename = data_input + 1;
        FILE* fp = fopen(filename, "r");
        if (!fp) {
            printf("[Error] Failed to open file: %s (errno: %d)\n", filename, errno);
            client_ops.client_destory(&amp_client);
            return -1;
        }
        
        fseek(fp, 0, SEEK_END);
        data_size = ftell(fp);
        
        // P2: 文件大小验证
        if (data_size == 0) {
            printf("[Error] File is empty: %s\n", filename);
            fclose(fp);
            client_ops.client_destory(&amp_client);
            return -1;
        }
        if (data_size > 1024*1024) {  // 1MB限制
            printf("[Error] File too large: %u bytes (max: 1MB)\n", data_size);
            fclose(fp);
            client_ops.client_destory(&amp_client);
            return -1;
        }
        
        fseek(fp, 0, SEEK_SET);
        data_to_send = malloc(data_size + 1);
        if (data_to_send == NULL) {
            printf("[Error] Failed to allocate memory for file content\n");
            fclose(fp);
            client_ops.client_destory(&amp_client);
            return -1;
        }
        
        size_t bytes_read = fread(data_to_send, 1, data_size, fp);
        if (bytes_read != data_size) {
            printf("[Error] Failed to read file completely (read: %zu, expected: %u)\n", bytes_read, data_size);
            free(data_to_send);
            fclose(fp);
            client_ops.client_destory(&amp_client);
            return -1;
        }
        
        data_to_send[data_size] = '\0';
        fclose(fp);
    } else {
        data_to_send = strdup(data_input);
        if (data_to_send == NULL) {
            printf("[Error] Failed to duplicate data input\n");
            client_ops.client_destory(&amp_client);
            return -1;
        }
        data_size = strlen(data_to_send);
    }
    
    printf("[QoS-Client] Data prepared: %u bytes\n", data_size);
    
    // step 5: 获取空消息（P2: 增强验证）
    struct Msg* msg = client_ops.empty_msg_get(&amp_client, service_id);
    if (msg == NULL) {
        printf("[Error] Failed to get empty message (service_id: %u)\n", service_id);
        free(data_to_send);
        client_ops.client_destory(&amp_client);
        return -1;
    }
    
    // step 6: QoS分类和资源预留（发送前）
    struct timespec send_time, recv_time;
    clock_gettime(CLOCK_MONOTONIC, &send_time);
    
    enum QoSClass qos_class = QOS_BEST_EFFORT;  // 默认值
    
    if (qos_initialized) {
        qos_class = qos_ops.classify_message(msg);
        
        printf("\n[QoS-Send] Message Classification:\n");
        printf("  Service ID: %u\n", msg->service_id);
        printf("  QoS Class: %s\n", 
               qos_class == QOS_REALTIME ? "REALTIME" :
               qos_class == QOS_THROUGHPUT ? "THROUGHPUT" :
               qos_class == QOS_RELIABLE ? "RELIABLE" : "BEST_EFFORT");
        printf("  Data Size: %u bytes\n", data_size);
        printf("  Priority: %s\n",
               qos_class == QOS_REALTIME ? "Highest (1)" :
               qos_class == QOS_THROUGHPUT ? "High (2)" :
               qos_class == QOS_RELIABLE ? "Medium (3)" : "Low (4)");
        
        /* P0修复: 资源预留检查 */
        int alloc_result = qos_ops.allocate_buffer(&global_qos_ctrl, qos_class, data_size);
        if (alloc_result != 0) {
            printf("[QoS-Warning] Failed to allocate buffer (class: %d, size: %u)\n",
                   qos_class, data_size);
            printf("  This message may be sent without QoS guarantee\n");
            /* 注意: 仍然允许发送，但已警告用户 */
        } else {
            printf("[QoS-Client] Buffer allocated successfully\n");
        }
    }
    
    // step 7: 写入数据到共享内存
    void* shm_addr = client_ops.shm_offset_to_addr(msg->offset);
    if (shm_addr == NULL) {
        printf("[Error] Failed to get shared memory address\n");
        free(data_to_send);
        client_ops.empty_msg_put(&amp_client, msg);
        client_ops.client_destory(&amp_client);
        return -1;
    }
    
    // 关键修复: 使用逐字节拷贝替代 memcpy() 以避免总线错误
    // memcpy() 可能使用 SIMD 指令,对共享内存区域的未对齐地址会触发硬件异常
    // memcpy(shm_addr, data_to_send, data_size);
    for (int j = 0; j < data_size; j++) {
        ((char*)shm_addr)[j] = data_to_send[j];
    }
    ((char*)shm_addr)[data_size] = '\0';
    msg->length = data_size + 1;
    
    printf("[QoS-Client] Data written to shared memory\n");
    
    // step 8: 发送消息并通知服务端
    // CRITICAL: 必须使用 msg_send_and_notify() 而不是 msg_send()
    // msg_send_and_notify() 会执行 transfer(wait_h → proc_ing_h) 并发送中断通知服务端
    if (client_ops.msg_send_and_notify(&amp_client, msg) != 0) {
        printf("[Error] Failed to send message\n");
        free(data_to_send);
        client_ops.empty_msg_put(&amp_client, msg);
        client_ops.client_destory(&amp_client);
        return -1;
    }
    
    printf("[QoS-Client] Message sent, waiting for response...\n");
    
    // step 9: 等待响应
    int max_wait = 100;
    int wait_count = 0;
    while (msg->flag.deal_state != MSG_DEAL_STATE_YES && wait_count < max_wait) {
        usleep(10000); // 10ms
        wait_count++;
    }
    
    clock_gettime(CLOCK_MONOTONIC, &recv_time);
    
    if (msg->flag.deal_state != MSG_DEAL_STATE_YES) {
        printf("[Error] Timeout waiting for response\n");
        free(data_to_send);
        client_ops.empty_msg_put(&amp_client, msg);
        client_ops.client_destory(&amp_client);
        return -1;
    }
    
    // step 10: QoS统计和资源释放（接收后）
    if (qos_initialized) {
        qos_class = qos_ops.classify_message(msg);
        
        /* P0-CRITICAL-5修复: 延迟计算说明
         * 注意：当前延迟计算包含客户端轮询等待时间（最多1秒）
         * 真实的服务处理延迟应该由服务端记录处理开始和结束时间戳
         * 
         * 建议改进方案：
         * 1. 在struct Msg中添加server_start_time和server_end_time字段
         * 2. 服务端在Phase 2开始时记录server_start_time
         * 3. 服务端在处理完成时记录server_end_time
         * 4. 客户端计算真实延迟 = server_end_time - server_start_time
         * 
         * 当前计算的是端到端延迟（含网络抖动、轮询开销）
         */
        uint64_t latency_us = (recv_time.tv_sec - send_time.tv_sec) * 1000000ULL +
                             (recv_time.tv_nsec - send_time.tv_nsec) / 1000;
        
        printf("\n[QoS-Stats] Performance Metrics:\n");
        printf("  End-to-End Latency: %lu μs (%.2f ms)\n", latency_us, latency_us / 1000.0);
        printf("  ⚠️  Note: Latency includes polling overhead (up to 1000ms)\n");
        printf("  Result: %s\n", 
               msg->flag.service_result == MSG_SERVICE_RET_SUCCESS ? "SUCCESS" : "FAILED");
        
        // 检查QoS违规
        struct ServiceQoSProfile* profile = qos_ops.get_qos_profile(msg->service_id);
        if (profile && latency_us > profile->max_latency_us) {
            printf("  ⚠️  QoS VIOLATION: Latency exceeded limit (%lu μs > %u μs)\n",
                   latency_us, profile->max_latency_us);
        }
        
        /* P0修复: 释放预留的资源 */
        int release_result = qos_ops.release_buffer(&global_qos_ctrl, qos_class, data_size);
        if (release_result == 0) {
            printf("[QoS-Client] Buffer released successfully\n");
        } else {
            printf("[QoS-Warning] Failed to release buffer\n");
        }
    }
    
    // step 11: 读取结果 (修复: 逐字节读取避免总线错误)
    printf("\n[QoS-Client] Response received:\n");
    
    // 安全地显示数据 - 逐字节打印,因为加密后的数据可能包含不可打印字符
    char* result_data = (char*)shm_addr;
    printf("  Data (hex): ");
    for (int i = 0; i < msg->length && i < 32; i++) {  // 最多显示32字节
        printf("0x%02x ", (unsigned char)result_data[i]);
    }
    if (msg->length > 32) printf("...");
    printf("\n");
    
    // 尝试显示ASCII (仅可打印字符)
    printf("  Data (ascii): ");
    for (int i = 0; i < msg->length && i < 32; i++) {
        if (result_data[i] >= 32 && result_data[i] <= 126) {
            printf("%c", result_data[i]);
        } else {
            printf(".");
        }
    }
    if (msg->length > 32) printf("...");
    printf("\n");
    
    printf("  Service Result: %u\n", msg->flag.service_result);
    
    // step 12: 清理
    free(data_to_send);
    client_ops.empty_msg_put(&amp_client, msg);
    client_ops.client_destory(&amp_client);
    
    printf("\n[QoS-Client] Transaction completed successfully\n");
    return 0;
}

/**
 * HyperAMP QoS Server - 使用三阶段批处理调度处理请求
 * 
 * @param shm_json_path 共享内存配置文件路径
 * @return 0表示成功，-1表示失败
 */
int hyper_amp_qos_service(char* shm_json_path) {
    printf("=== HyperAMP QoS Service ===\n");
    printf("Initializing QoS-aware server with three-phase batch processing...\n");
    
    // step 1: 解析配置
    parse_global_addr(shm_json_path);
    printf("[QoS-Server] Configuration parsed\n");
    
    // step 2: 打开 /dev/mem
    int mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (mem_fd < 0) {
        printf("[Error] Failed to open /dev/mem: %s\n", strerror(errno));
        return -1;
    }
    printf("[QoS-Server] /dev/mem opened\n");
    
    // step 3: 映射内存
    // CRITICAL: 需要映射两个队列:
    // 1. zone0队列 - 客户端在这里执行transfer,服务端从这里读取消息
    // 2. zonex队列 - 客户端检查这里的working_mark来判断服务端是否就绪
    void* buf_virt = mmap(NULL, addr_infos[0].len, PROT_READ | PROT_WRITE,
                         MAP_SHARED, mem_fd, addr_infos[0].start);
    void* zone0_msg_queue_virt = mmap(NULL, addr_infos[1].len, PROT_READ | PROT_WRITE,
                                    MAP_SHARED, mem_fd, addr_infos[1].start);
    void* zonex_msg_queue_virt = mmap(NULL, addr_infos[2].len, PROT_READ | PROT_WRITE,
                                    MAP_SHARED, mem_fd, addr_infos[2].start);
    
    if (buf_virt == MAP_FAILED || zone0_msg_queue_virt == MAP_FAILED || zonex_msg_queue_virt == MAP_FAILED) {
        printf("[Error] Failed to map memory\n");
        close(mem_fd);
        return -1;
    }
    
    uint64_t buf_addr = (uint64_t)buf_virt;
    uint64_t zone0_msg_queue_addr = (uint64_t)zone0_msg_queue_virt;
    uint64_t zonex_msg_queue_addr = (uint64_t)zonex_msg_queue_virt;
    struct AmpMsgQueue* root_msg_queue = (struct AmpMsgQueue*)zone0_msg_queue_addr;
    struct AmpMsgQueue* zonex_msg_queue = (struct AmpMsgQueue*)zonex_msg_queue_addr;
    uint64_t root_msg_entries_addr = zone0_msg_queue_addr + sizeof(struct AmpMsgQueue);
    
    printf("[QoS-Server] Memory mapped successfully\n");
    printf("  Buffer: %p (%u bytes)\n", buf_virt, addr_infos[0].len);
    printf("  Zone0 Queue: %p (%u bytes) - For message processing\n", 
           zone0_msg_queue_virt, addr_infos[1].len);
    printf("  Zonex Queue: %p (%u bytes) - For client readiness check\n", 
           zonex_msg_queue_virt, addr_infos[2].len);
    
    // step 3.5: 初始化两个消息队列（关键！设置 working_mark）
    // Zone0队列 - 服务端实际处理消息的队列
    if (msg_queue_ops.init(root_msg_queue, addr_infos[1].len) != 0) {
        printf("[Error] Failed to initialize zone0 message queue\n");
        return -1;
    }
    root_msg_queue->working_mark = INIT_MARK_INITIALIZED;
    root_msg_queue->working_mark = MSG_QUEUE_MARK_IDLE;
    printf("[QoS-Server] Zone0 queue initialized (working_mark=0x%x, buf_size=%u)\n",
           root_msg_queue->working_mark, root_msg_queue->buf_size);
    
    // Zonex队列 - 让客户端能检测到服务端就绪
    if (msg_queue_ops.init(zonex_msg_queue, addr_infos[2].len) != 0) {
        printf("[Error] Failed to initialize zonex message queue\n");
        return -1;
    }
    zonex_msg_queue->working_mark = INIT_MARK_INITIALIZED;
    zonex_msg_queue->working_mark = MSG_QUEUE_MARK_IDLE;
    printf("[QoS-Server] Zonex queue initialized (working_mark=0x%x, buf_size=%u)\n",
           zonex_msg_queue->working_mark, zonex_msg_queue->buf_size);
    
    // step 4: 初始化QoS控制器
    if (!server_qos_initialized) {
        if (qos_ops.qos_init(&server_qos_ctrl, 1024*1024, 64) == 0) {
            server_qos_initialized = 1;
            printf("[QoS-Server] QoS controller initialized (1MB, 64 batch)\n");
        } else {
            printf("[Warning] QoS initialization failed\n");
        }
    }
    
    // step 5: 打开中断设备
    int shm_fd = open("/dev/hshm0", O_RDONLY);
    if (shm_fd < 0) {
        printf("[Warning] Failed to open /dev/hshm0, using polling mode\n");
    } else {
        printf("[QoS-Server] Interrupt device opened\n");
    }
    
    struct pollfd pfd;
    if (shm_fd >= 0) {
        pfd.fd = shm_fd;
        pfd.events = POLLIN;
    }
    
    // step 6: 安装信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    printf("\n[QoS-Server] Waiting for requests...\n");
    printf("Press Ctrl+C to exit\n");
    printf("[QoS-Server-DEBUG] Entering main loop (polling mode: %s)\n", 
           shm_fd < 0 ? "YES" : "NO");
    printf("[QoS-Server-DEBUG] Initial queue state: proc_ing_h=%u, wait_h=%u, buf_size=%u\n",
           root_msg_queue->proc_ing_h, root_msg_queue->wait_h, root_msg_queue->buf_size);
    printf("\n");
    
    int msg_count = 0;
    const int MAX_BATCH_SIZE = 64;
    
    // step 7: 主循环
    int poll_count = 0;
    printf("[QoS-Server-DEBUG] Starting main loop iteration...\n");
    fflush(stdout);  // 强制刷新输出缓冲区
    
    while (running) {
        poll_count++;
        
        // DEBUG: 立即打印,确认循环在运行
        if (poll_count == 1 || poll_count % 10 == 0) {
            printf("[QoS-Server-DEBUG] Loop iteration #%d\n", poll_count);
            fflush(stdout);
        }
        
        // 等待中断或超时
        if (shm_fd >= 0) {
            int ret = poll(&pfd, 1, 1000); // 1秒超时
            if (ret <= 0) continue;
            if (!(pfd.revents & POLLIN)) continue;
        } else {
            usleep(100000); // 100ms轮询
        }
        
        // DEBUG: 每10次轮询打印一次队列状态
        if (poll_count % 10 == 0) {
            printf("[QoS-Server-DEBUG] Poll #%d - Queue: proc_ing_h=%u, wait_h=%u, working_mark=0x%x, buf_size=%u\n",
                   poll_count, root_msg_queue->proc_ing_h, root_msg_queue->wait_h, 
                   root_msg_queue->working_mark, root_msg_queue->buf_size);
            fflush(stdout);
        }
        
        // DEBUG: 如果检测到任何队列有消息,立即打印
        if (root_msg_queue->proc_ing_h < root_msg_queue->buf_size || 
            root_msg_queue->wait_h < root_msg_queue->buf_size) {
            printf("[QoS-Server-DEBUG] *** MESSAGE DETECTED *** proc_ing_h=%u, wait_h=%u, buf_size=%u\n",
                   root_msg_queue->proc_ing_h, root_msg_queue->wait_h, root_msg_queue->buf_size);
        }
        
        // 检查是否有消息需要处理
        // 注意: 客户端的 msg_send_and_notify() 已经完成了 transfer(wait_h → proc_ing_h)
        // 所以这里直接检查 proc_ing_h 即可
        if (root_msg_queue->proc_ing_h >= root_msg_queue->buf_size) {
            continue;
        }
        
        printf("\n[QoS-Server] ========== BATCH PROCESSING #%d ==========\n", ++msg_count);
        
        // ===== 阶段 1: 收集消息并入队到QoS队列 (P0-CRITICAL: 死循环防护) =====
        printf("[QoS-Server] Phase 1: Collecting messages...\n");
        
        int collected_count = 0;
        int visited_count = 0;
        const int MAX_VISIT = 100;
        
        // P0-CRITICAL-6修复: 不保存MsgEntry指针，直接在Phase 1同步清理
        // P0-CRITICAL-7修复: 添加访问过的节点记录，防止并发修改导致环
        uint16_t visited_nodes[MAX_BATCH_SIZE];
        int visited_nodes_count = 0;
        
        uint16_t buf_size = root_msg_queue->buf_size;
        uint16_t current_head = root_msg_queue->proc_ing_h;
        
        while (current_head < buf_size && collected_count < MAX_BATCH_SIZE && visited_count < MAX_VISIT) {
            visited_count++;
            
            // P0-CRITICAL-7修复: 检查是否访问过此节点（防止环）
            int is_duplicate = 0;
            for (int i = 0; i < visited_nodes_count; i++) {
                if (visited_nodes[i] == current_head) {
                    printf("[QoS-Server] ⚠️  CRITICAL: Circular reference detected at node %u!\n", current_head);
                    is_duplicate = 1;
                    break;
                }
            }
            if (is_duplicate) break;
            
            // 记录访问过的节点
            if (visited_nodes_count < MAX_BATCH_SIZE) {
                visited_nodes[visited_nodes_count++] = current_head;
            }
            
            uint64_t msg_entry_addr = root_msg_entries_addr + sizeof(struct MsgEntry) * current_head;
            struct MsgEntry* msg_entry = (struct MsgEntry*)msg_entry_addr;
            struct Msg* msg = &msg_entry->msg;
            
            // 边界保护
            if (collected_count >= MAX_BATCH_SIZE) {
                printf("[QoS-Server] ERROR: Exceeded max batch size\n");
                break;
            }
            
            // P0-CRITICAL-6修复: 读取next_idx后立即清理此节点（防止并发访问）
            uint16_t next_idx = msg_entry->nxt_idx;
            msg_entry->nxt_idx = buf_size;  // 立即标记为已处理
            
            // 将消息入队到QoS队列（统一数据流）
            if (server_qos_initialized) {
                enum QoSClass qos_class = qos_ops.classify_message(msg);
                
                // 创建 QoSMsg 结构体（P0-CRITICAL-2: 深拷贝消息）
                struct QoSMsg qos_msg;
                memset(&qos_msg, 0, sizeof(struct QoSMsg));
                
                // P0-CRITICAL-2修复: 显式复制每个字段（未来支持深拷贝）
                qos_msg.base_msg.service_id = msg->service_id;
                qos_msg.base_msg.offset = msg->offset;
                qos_msg.base_msg.length = msg->length;
                qos_msg.base_msg.flag = msg->flag;
                /* 注意: 如果未来添加指针字段，需要在此深拷贝 */
                
                qos_msg.qos_class = qos_class;
                qos_msg.send_timestamp = 0; // 将由enqueue函数设置
                qos_msg.sequence_num = collected_count;
                qos_msg.deadline = 0;
                qos_msg.retransmit_count = 0;
                qos_msg.priority_boost = 0;
                
                int enqueue_result = qos_ops.qos_enqueue(&server_qos_ctrl, &qos_msg);
                if (enqueue_result == 0) {
                    collected_count++;
                    printf("  [%d] Enqueued: Service %u, Class: %s, %u bytes\n",
                           collected_count, msg->service_id,
                           qos_class == QOS_REALTIME ? "REALTIME" :
                           qos_class == QOS_THROUGHPUT ? "THROUGHPUT" :
                           qos_class == QOS_RELIABLE ? "RELIABLE" : "BEST_EFFORT",
                           msg->length);
                } else {
                    printf("[QoS-Server] WARNING: Failed to enqueue message (queue full?)\n");
                }
            } else {
                collected_count++;
            }
            
            // 移动到下一个节点
            if (next_idx >= buf_size) break;
            current_head = next_idx;
        }
        
        // Bug #1 fix: 检测循环引用
        if (visited_count >= MAX_VISIT) {
            printf("[QoS-Server] ⚠️  WARNING: Possible circular reference detected!\n");
        }
        
        printf("[QoS-Server] Phase 1 completed: Collected %d messages\n", collected_count);
        
        // ===== 阶段 2: 按优先级调度和处理 (P0-CRITICAL-8修复: 移除重复dequeue) =====
        printf("[QoS-Server] Phase 2: Scheduling and processing (WRR)...\n");
        
        int processed_count = 0;
        while (processed_count < collected_count && server_qos_initialized) {
            /* P0-CRITICAL-8修复: qos_schedule应该内部调用dequeue，不再手动调用 
             * 当前暂时保留原有逻辑，但添加警告 */
            struct Msg* scheduled_msg = qos_ops.qos_schedule(&server_qos_ctrl);
            
            // P2修复: 完整的错误处理
            if (scheduled_msg == NULL) {
                printf("[QoS-Server] WARNING: qos_schedule returned NULL (no more messages)\n");
                break;
            }
            
            // 获取QoS分类
            enum QoSClass qos_class = qos_ops.classify_message(scheduled_msg);
            
            // P2修复: 验证消息offset有效性
            if (scheduled_msg->offset >= addr_infos[0].len) {
                printf("[QoS-Server] ERROR: Invalid message offset: %u >= %u\n",
                       scheduled_msg->offset, addr_infos[0].len);
                /* P0-CRITICAL-8注意: 这里dequeue是必要的，因为schedule没有移除消息 */
                qos_ops.qos_dequeue(&server_qos_ctrl, qos_class);
                processed_count++;
                continue;
            }
            
            char* data_ptr = (char*)(buf_addr + scheduled_msg->offset);
            
            printf("  [%d/%d] Processing: Service %u, Class: %s, Length: %u\n",
                   processed_count + 1, collected_count, scheduled_msg->service_id,
                   qos_class == QOS_REALTIME ? "REALTIME" :
                   qos_class == QOS_THROUGHPUT ? "THROUGHPUT" :
                   qos_class == QOS_RELIABLE ? "RELIABLE" : "BEST_EFFORT",
                   scheduled_msg->length);
            
            // 执行服务
            int service_result = MSG_SERVICE_RET_SUCCESS;
            
            // P2修复: 增强的参数验证
            if (scheduled_msg->length > 0 && scheduled_msg->length <= addr_infos[0].len && data_ptr != NULL) {
                switch (scheduled_msg->service_id) {
                    case 1:  // 加密
                        if (hyperamp_encrypt_service(data_ptr, scheduled_msg->length - 1, scheduled_msg->length) != 0) {
                            service_result = MSG_SERVICE_RET_FAIL;
                            printf("    Service failed: Encryption error\n");
                        }
                        break;
                    case 2:  // 解密
                        if (hyperamp_decrypt_service(data_ptr, scheduled_msg->length - 1, scheduled_msg->length) != 0) {
                            service_result = MSG_SERVICE_RET_FAIL;
                            printf("    Service failed: Decryption error\n");
                        }
                        break;
                    case 66:  // Echo
                        printf("    Echo service completed\n");
                        break;
                    default:
                        printf("    WARNING: Unknown service ID: %u\n", scheduled_msg->service_id);
                        service_result = MSG_SERVICE_RET_FAIL;
                        break;
                }
            } else {
                printf("    ERROR: Invalid message parameters (length: %u, data_ptr: %p)\n",
                       scheduled_msg->length, (void*)data_ptr);
                service_result = MSG_SERVICE_RET_FAIL;
            }
            
            // 更新消息状态
            scheduled_msg->flag.deal_state = MSG_DEAL_STATE_YES;
            scheduled_msg->flag.service_result = service_result;
            
            // ========== 关键修复: 将处理结果写回原始队列 ==========
            // 问题: scheduled_msg 是 QoS 队列中的副本,客户端轮询的是 Zone0 队列中的原始消息
            // 解决: 找到 Zone0 队列中对应的消息实体,更新其 deal_state
            // 
            // 方法: 遍历 proc_ing_h 链表,通过 offset/length/service_id 匹配找到原始消息
            uint16_t orig_head = root_msg_queue->proc_ing_h;
            bool found_original = false;
            
            while (orig_head < buf_size) {
                struct MsgEntry* orig_entry = (struct MsgEntry*)(root_msg_entries_addr + sizeof(struct MsgEntry) * orig_head);
                struct Msg* orig_msg = &orig_entry->msg;
                
                // 通过关键字段匹配原始消息
                if (orig_msg->offset == scheduled_msg->offset &&
                    orig_msg->length == scheduled_msg->length &&
                    orig_msg->service_id == scheduled_msg->service_id) {
                    // 找到了! 更新原始消息的状态
                    orig_msg->flag.deal_state = MSG_DEAL_STATE_YES;
                    orig_msg->flag.service_result = service_result;
                    found_original = true;
                    printf("    ✅ Updated original message state in Zone0 queue (index: %u)\n", orig_head);
                    break;
                }
                
                // 移动到下一个节点
                uint16_t next = orig_entry->nxt_idx;
                if (next >= buf_size) break;
                orig_head = next;
            }
            
            if (!found_original) {
                printf("    ⚠️  WARNING: Could not find original message in Zone0 queue!\n");
            }
            
            /* P0-CRITICAL-8修复: 手动dequeue（因为当前schedule不会移除消息）
             * TODO: 未来应该在qos_schedule内部调用dequeue */
            struct QoSMsg* dequeued_msg = qos_ops.qos_dequeue(&server_qos_ctrl, qos_class);
            if (dequeued_msg == NULL) {
                printf("[QoS-Server] WARNING: qos_dequeue returned NULL for class %d\n", qos_class);
            }
            
            /* P0-CRITICAL-11修复: 调用违规检测 */
            struct ServiceQoSProfile* profile = qos_ops.get_qos_profile(scheduled_msg->service_id);
            if (profile != NULL && qos_ops.check_qos_violation != NULL) {
                struct QoSQueue *queue = &server_qos_ctrl.queues[qos_class];
                int violations = qos_ops.check_qos_violation(&queue->metrics, profile);
                if (violations > 0) {
                    printf("    ⚠️  QoS Violations detected: %d\n", violations);
                }
            }
            
            processed_count++;
        }
        
        printf("[QoS-Server] Phase 2 completed: Processed %d messages\n", processed_count);
        
        /* P0-CRITICAL-12修复: 调用自适应参数调整 */
        if (server_qos_initialized && qos_ops.adapt_qos_params != NULL) {
            qos_ops.adapt_qos_params(&server_qos_ctrl);
        }
        
        // ===== 阶段 3: 清理队列 (P0-CRITICAL-6修复: 简化清理逻辑) =====
        printf("[QoS-Server] Phase 3: Cleaning up...\n");
        
        /* P0-CRITICAL-6修复: 节点已在Phase 1清理，此处只需重置队列头 */
        root_msg_queue->proc_ing_h = root_msg_queue->buf_size;
        root_msg_queue->working_mark = MSG_QUEUE_MARK_IDLE;
        
        printf("[QoS-Server] Phase 3 completed: Queue reset to idle\n");
        printf("[QoS-Server] ========== BATCH COMPLETED ==========\n");
    }
    
    // step 8: 清理
    if (shm_fd >= 0) close(shm_fd);
    munmap(buf_virt, addr_infos[0].len);
    munmap(zone0_msg_queue_virt, addr_infos[1].len);
    munmap(zonex_msg_queue_virt, addr_infos[2].len);
    close(mem_fd);
    
    printf("\n[QoS-Server] Shutting down gracefully\n");
    printf("Total batches processed: %d\n", msg_count);
    
    return 0;
}
