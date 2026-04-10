/*
 * HyperAMP QoS Module - Header File
 * 
 * Provides QoS-aware client and server functions for HyperAMP communication.
 * 
 * Features:
 * - Client: QoS classification, latency tracking, violation detection
 * - Server: Three-phase batch processing with WRR scheduling
 * 
 * Author: HyperAMP Team
 * Date: 2025-10-31
 */

#ifndef HYPER_AMP_QOS_H
#define HYPER_AMP_QOS_H

#include <stdint.h>

/**
 * HyperAMP QoS Client
 * 
 * 发送带QoS分类和性能统计的请求到服务器
 * 
 * @param shm_json_path 共享内存配置文件路径
 * @param data_input 输入数据（字符串或 @filename）
 * @param service_id 服务ID（1=加密, 2=解密, 66=Echo等）
 * @return 0=成功, -1=失败
 * 
 * 示例:
 *   hyper_amp_qos_client("shm_config.json", "hello world", 1);
 *   hyper_amp_qos_client("shm_config.json", "@input.txt", 2);
 */
int hyper_amp_qos_client(char* shm_json_path, char* data_input, uint32_t service_id);

/**
 * HyperAMP QoS Service
 * 
 * 使用三阶段批处理调度处理来自客户端的请求
 * 
 * 三个阶段:
 *   Phase 1: 收集批量消息（带循环引用检测）
 *   Phase 2: WRR优先级调度和处理
 *   Phase 3: 队列清理（带NULL安全检查）
 * 
 * @param shm_json_path 共享内存配置文件路径
 * @return 0=成功, -1=失败
 * 
 * 示例:
 *   hyper_amp_qos_service("shm_config.json");
 */
int hyper_amp_qos_service(char* shm_json_path);

/**
 * HyperAMP QoS Statistics (P0修复)
 * 
 * 打印QoS系统的完整统计信息
 * 包括客户端和服务端的所有QoS队列状态
 * 
 * @return 0=成功, -1=失败
 * 
 * 示例:
 *   hyper_amp_qos_print_stats();
 */
int hyper_amp_qos_print_stats(void);

#endif // HYPER_AMP_QOS_H
