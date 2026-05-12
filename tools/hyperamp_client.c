#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "hyperamp_client.h"
#include "log.h" // 日志系统
#include "service/safe_service.h"
#include "shm/addr.h"
#include "shm/channel.h"
#include "shm/client.h"
#include "shm/config/config_addr.h"
#include "shm/config/config_zone.h"
#include "shm/msg.h"
#include "shm/msgqueue.h"
#include "shm/precision_timer.h"
#include "shm/shm.h"
#include "shm/time_utils.h"

int hyperamp_client(int argc, char *argv[]) {

    // 获取计数器频率
    uint64_t timer_freq = get_cntfrq();
    uint64_t ticks_start = get_cntpct();

    // 参数检查
    if (argc < 3) {
        printf("Usage: ./hvisor shm hyperamp_client <shm_json_path> "
               "<data|@filename> <service_id>\n");
        printf("Examples:\n");
        printf("  ./hvisor shm hyperamp_client shm_config.json \"hello world\" "
               "1\n");
        printf("  ./hvisor shm hyperamp_client shm_config.json @data.txt 2\n");
        printf("  ./hvisor shm hyperamp_client shm_config.json hex:48656c6c6f "
               "2  (hex input)\n");
        return -1;
    }

    char *shm_json_path = argv[0];
    char *data_input = argv[1];
    uint32_t service_id =
        (argc >= 3) ? strtoul(argv[2], NULL, 10) : NPUCore_SERVICE_ECHO_ID;

    // 数据处理：支持直接字符串或从文件读取
    char *data_buffer = NULL;
    int data_size = 0;

    // 直接使用字符串
    data_size = strlen(data_input);
    data_buffer = malloc(data_size + 1);
    if (data_buffer == NULL) {
        printf("Error: Memory allocation failed\n");
        return -1;
    }
    strcpy(data_buffer, data_input);
    //一个奇怪的bug，这个printf不能注释掉否侧会报总线错误 (核心已转储)
    printf("Using input string: \"%s\" (%d bytes)\n", data_buffer, data_size);

    parse_global_addr(shm_json_path);

    // 初始化客户端
    struct Client amp_client = {0};
    if (client_ops.client_init(&amp_client, ZONE_NPUcore_ID) != 0) {
        printf("error: client init failed\n");
        free(data_buffer);
        return -1;
    }
    // printf("info: client init success\n");

    // 获取空闲消息
    struct Msg *msg = client_ops.empty_msg_get(&amp_client, service_id);
    if (msg == NULL) {
        printf("error : empty msg get [service_id = %u]\n", service_id);
        free(data_buffer);
        return -1;
    }

    // 重置消息
    msg_ops.msg_reset(msg);

    // 分配共享内存
    char *shm_data = (char *)client_ops.shm_malloc(&amp_client, data_size + 1,
                                                   MALLOC_TYPE_P);
    if (shm_data == NULL) {
        // printf("info: MALLOC_TYPE_P failed, trying MALLOC_TYPE_V...\n");
        shm_data = (char *)client_ops.shm_malloc(&amp_client, data_size + 1,
                                                 MALLOC_TYPE_V);
    }
    if (shm_data == NULL) {
        printf("error : shm malloc failed [size = %u]\n", data_size + 1);
        free(data_buffer);
        return -1;
    }

    // 复制数据到共享内存
    // memcpy(shm_data, data_buffer, data_size);
    // shm_data[data_size] = '\0';
    uint64_t ticks_copy_start = get_cntpct();

    for (int j = 0; j < data_size; j++) {
        shm_data[j] = data_buffer[j];
    }
    // shm_data[data_size] = '\0';
    __sync_synchronize();
    uint64_t ticks_copy_end = get_cntpct();
    uint64_t copy_latency_ns =
        ticks_to_ns(ticks_copy_start, ticks_copy_end, timer_freq);
    printf("[Performance] Memory Copy (Size: %d bytes): %lu ns (%.3f us)\n",
           data_size, copy_latency_ns, copy_latency_ns / 1000.0);
    // 设置消息
    msg->offset = client_ops.shm_addr_to_offset(shm_data);
    msg->length = data_size;

    // 发送消息并通知
    uint64_t ticks_send_start = get_cntpct();
    // printf("[Linux] irq send start: 0x%016lx (%lu)\n", ticks_send_start,
    // ticks_send_start);

    if (client_ops.msg_send_and_notify(&amp_client, msg) != 0) {
        printf("error : msg send failed [offset = 0x%x, length = %u]\n",
               msg->offset, msg->length);
        free(data_buffer);
        return -1;
    }

    // 记录发送完成的时间点
    uint64_t ticks_send_done = get_cntpct();
    // printf("[Linux] irq send end: 0x%016lx (%lu)\n", ticks_send_done,
    // ticks_send_done);
    uint64_t irq_copy_latency_ns =
        ticks_to_ns(ticks_send_start, ticks_send_done, timer_freq);

    printf("[Performance] irq: %lu ns (%.3f us)\n", irq_copy_latency_ns,
           irq_copy_latency_ns / 1000.0);
    // 轮询等待响应
    int poll_count = 0;

    while (client_ops.msg_poll(msg) != 0) {
        poll_count++;
    }

    // 记录响应接收完成的时间戳
    uint64_t ticks_end = get_cntpct();

    // 计算各项延迟
    uint64_t total_latency_us = ticks_to_us(ticks_start, ticks_end, timer_freq);
    uint64_t send_time_us =
        ticks_to_us(ticks_start, ticks_send_done, timer_freq);
    uint64_t wait_time_us = ticks_to_us(ticks_send_done, ticks_end, timer_freq);
    uint64_t total_latency_ns = ticks_to_ns(ticks_start, ticks_end, timer_freq);

    printf("\n=== HYPERAMP PERFORMANCE RESULTS ===\n");
    printf("Data Size: %d bytes\n", data_size);
    printf("Total Round-Trip Latency: %lu μs (%.3f ms) [%lu ns]\n",
           total_latency_us, total_latency_us / 1000.0, total_latency_ns);
    printf("  └─ Send Time: %lu μs (%.3f ms)\n", send_time_us,
           send_time_us / 1000.0);
    printf("  └─ Wait + Receive Time: %lu μs (%.3f ms)\n", wait_time_us,
           wait_time_us / 1000.0);
    printf("Poll Count: %d iterations\n", poll_count);

    // 计算吞吐量（基于总往返时间）
    if (total_latency_us > 0) {
        double throughput_mbps = (data_size * 8.0 * 1000000.0) /
                                 (total_latency_us * 1024.0 * 1024.0);
        printf("Throughput: %.2f KB/s (%.2f Mbps)\n",
               data_size * 1000.0 / total_latency_us, throughput_mbps);
    }

    // 性能评估
    if (total_latency_us <= 10000) {
        printf("Status: EXCELLENT (≤10ms)\n");
    } else if (total_latency_us <= 50000) {
        printf("Status: GOOD (≤50ms)\n");
    } else if (total_latency_us <= 100000) {
        printf("Status: ACCEPTABLE (≤100ms)\n");
    } else {
        printf("Status: NEEDS OPTIMIZATION (>100ms)\n");
    }
    printf("====================================\n\n");

    // 读取处理后的结果数据
    if (msg->flag.service_result == MSG_SERVICE_RET_SUCCESS) {
        printf("=== Service Result ===\n");

        // 获取处理后的实际数据大小
        int result_data_size = msg->length > 0 ? msg->length : 0;
        if (result_data_size <= 0) {
            result_data_size = data_size;
        }

        if (service_id == 1) {
            printf("Encryption completed:\n");
        } else if (service_id == 2) {
            printf("Decryption completed:\n");
        } else {
            printf("Service %u completed:\n", service_id);
        }

        // 确定显示长度
        int display_length = result_data_size;
        bool truncated = false;
        if (result_data_size > 256) {
            display_length = 64;
            truncated = true;
        }

        // 生成输出文件名
        char output_filename[256];
        if (service_id == 1) {
            snprintf(output_filename, sizeof(output_filename),
                     "encrypted_result.txt");
        } else if (service_id == 2) {
            snprintf(output_filename, sizeof(output_filename),
                     "decrypted_result.txt");
        } else {
            snprintf(output_filename, sizeof(output_filename),
                     "service_%u_result.txt", service_id);
        }

        // 打开文件准备保存
        FILE *output_file = fopen(output_filename, "wb");

        // 显示处理后的数据
        printf("Result: [");
        for (int i = 0; i < display_length; i++) {
            if (output_file != NULL) {
                fputc(shm_data[i], output_file);
            }

            if (shm_data[i] >= 32 && shm_data[i] <= 126) {
                printf("%c", shm_data[i]);
            } else if (shm_data[i] == '\n') {
                printf("\\n");
            } else if (shm_data[i] == '\r') {
                printf("\\r");
            } else if (shm_data[i] == '\t') {
                printf("\\t");
            } else {
                printf("\\x%02x", (unsigned char)shm_data[i]);
            }
        }

        if (truncated) {
            printf("... (showing first %d of %d bytes)", display_length,
                   result_data_size);
        }
        printf("] (%d bytes)\n", result_data_size);

        // 显示十六进制格式
        printf("Hex: ");
        for (int i = 0; i < display_length; i++) {
            printf("%02x", (unsigned char)shm_data[i]);
        }
        if (truncated) {
            printf("... (showing first %d of %d bytes)", display_length,
                   result_data_size);
        }
        printf("\n");

        if (output_file != NULL) {
            fclose(output_file);
        }

        if (truncated) {
            printf("Note: Large data truncated. Full data saved to %s\n",
                   output_filename);
        }

        // 解密命令提示
        if (service_id == 1 && result_data_size > 0 && result_data_size <= 64) {
            printf("\nTo decrypt: ./hvisor shm hyperamp_client %s hex:",
                   shm_json_path);
            for (int i = 0; i < result_data_size; i++) {
                printf("%02x", (unsigned char)shm_data[i]);
            }
            printf(" 2\n");
        } else if (service_id == 1 && result_data_size > 64) {
            printf("\nTo decrypt: ./hvisor shm hyperamp_client %s @%s 2\n",
                   shm_json_path, output_filename);
        }

        // printf("======================\n");
    } else {
        printf("error : HyperAMP service failed [service_id = %u]\n",
               service_id);
    }

    // 清理资源
    client_ops.empty_msg_put(&amp_client, msg);
    client_ops.client_destory(&amp_client);
    free(data_buffer);

    return 0;
}
