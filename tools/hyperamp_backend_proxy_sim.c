/**
 * @file hyperamp_backend_proxy_sim.c
 * @brief Linux 端后端协议栈模拟器 - 用于测试
 * 
 * 功能：
 * 1. 监听 TX Queue (seL4 → Linux)，接收前端请求
 * 2. 模拟处理 SESSION 消息（分配后端会话 ID）
 * 3. 模拟处理 HTTP 请求（生成假的 HTTP 响应）
 * 4. 将响应写入 RX Queue (Linux → seL4)
 * 
 * 编译: gcc -o hyperamp_backend hyperamp_backend_proxy_sim.c shm/hyperamp_linux_shm.c -I./include -lpthread
 * 
 * SPDX-License-Identifier: BSD-2-Clause
 */

#define _POSIX_C_SOURCE 199309L
#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <signal.h>

#include "shm/hyperamp_shm_queue.h"

/* 引入协议定义 */
#define PROXY_PROTO_TCP     6
#define PROXY_PROTO_UDP     17
#define PROXY_STATE_SYN_SENT    1
#define PROXY_STATE_ESTABLISHED 2

typedef struct {
    uint8_t  protocol;
    uint8_t  state;
    uint16_t src_port;
    uint32_t src_ip;
    uint16_t dst_port;
    uint16_t reserved;
    uint32_t dst_ip;
} __attribute__((packed)) SessionCreatePayload;

typedef struct {
    char method[8];
    char uri[256];
    char host[128];
    uint16_t content_length;
    uint16_t reserved;
} __attribute__((packed)) HttpRequestHeader;

static inline void ip_to_str(uint32_t ip_net, char *buf) {
    uint8_t *bytes = (uint8_t *)&ip_net;
    sprintf(buf, "%u.%u.%u.%u", bytes[0], bytes[1], bytes[2], bytes[3]);
}

/* HyperAMP Linux 接口 */
extern int hyperamp_linux_init(uint64_t phys_addr, int is_creator);
extern int hyperamp_linux_send(uint8_t msg_type, uint16_t frontend_sess_id,
                                uint16_t backend_sess_id, const void *payload,
                                uint16_t payload_len);
extern int hyperamp_linux_recv(HyperampMsgHeader *hdr, void *payload,
                                uint16_t max_payload_len, uint16_t *actual_payload_len);
extern void hyperamp_linux_cleanup(void);
extern void hyperamp_linux_get_status(void);

/* 全局状态 */
static volatile int g_running = 1;
static uint16_t g_next_backend_sess = 2000;  // 后端会话 ID 从 2000 开始

/**
 * @brief 信号处理：Ctrl+C 退出
 */
static void signal_handler(int sig)
{
    (void)sig;
    printf("\n[Backend] Shutting down...\n");
    g_running = 0;
}

/**
 * @brief 处理 SESSION 消息：模拟建立连接
 */
static void handle_session_message(HyperampMsgHeader *req_hdr, void *payload, uint16_t payload_len)
{
    printf("\n[Backend] ========== SESSION Message ==========\n");
    
    if (payload_len < sizeof(SessionCreatePayload)) {
        printf("[Backend] ERROR: SESSION payload too short\n");
        return;
    }
    
    SessionCreatePayload *sess = (SessionCreatePayload *)payload;
    
    const char *proto_name = (sess->protocol == PROXY_PROTO_TCP) ? "TCP" : "UDP";
    char src_ip[16], dst_ip[16];
    ip_to_str(sess->src_ip, src_ip);
    ip_to_str(sess->dst_ip, dst_ip);
    
    printf("[Backend] Connection Request:\n");
    printf("[Backend]   Protocol: %s\n", proto_name);
    printf("[Backend]   Source: %s:%u\n", src_ip, sess->src_port);
    printf("[Backend]   Destination: %s:%u\n", dst_ip, sess->dst_port);
    printf("[Backend]   Frontend Session: %u\n", req_hdr->frontend_sess_id);
    
    // 模拟：分配后端会话 ID（真实场景中会调用 socket() + connect()）
    uint16_t backend_sess = g_next_backend_sess++;
    printf("[Backend] ✓ Connection established (simulated)\n");
    printf("[Backend]   Backend Session: %u\n", backend_sess);
    
    // 发送 SESSION 响应（状态改为 ESTABLISHED）
    SessionCreatePayload resp_sess = *sess;
    resp_sess.state = PROXY_STATE_ESTABLISHED;
    
    if (hyperamp_linux_send(HYPERAMP_MSG_TYPE_SESS, 
                           req_hdr->frontend_sess_id,
                           backend_sess,
                           &resp_sess,
                           sizeof(resp_sess)) == HYPERAMP_OK) {
        printf("[Backend] ✓ SESSION response sent\n");
    } else {
        printf("[Backend] ✗ Failed to send SESSION response\n");
    }
    
    printf("[Backend] ==========================================\n");
}

/**
 * @brief 处理 DATA 消息：模拟 HTTP 请求/响应
 */
static void handle_data_message(HyperampMsgHeader *req_hdr, void *payload, uint16_t payload_len)
{
    printf("\n[Backend] ========== DATA Message ==========\n");
    
    // 尝试解析为 HTTP 请求
    if (payload_len >= sizeof(HttpRequestHeader)) {
        HttpRequestHeader *http_req = (HttpRequestHeader *)payload;
        
        if (http_req->method[0] >= 'A' && http_req->method[0] <= 'Z') {
            printf("[Backend] HTTP Request:\n");
            printf("[Backend]   Method: %.8s\n", http_req->method);
            printf("[Backend]   URI: %.256s\n", http_req->uri);
            printf("[Backend]   Host: %.128s\n", http_req->host);
            printf("[Backend]   Content-Length: %u\n", http_req->content_length);
            
            // 模拟：通过 Linux socket 发送请求并接收响应
            printf("[Backend] Simulating network request (not actually connecting)...\n");
            
            // 生成假的 HTTP 响应
            const char *fake_response = 
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/html\r\n"
                "Content-Length: 65\r\n"
                "\r\n"
                "<html><body><h1>Hello from Backend!</h1></body></html>";
            
            printf("[Backend] ✓ Simulated HTTP response generated\n");
            
            // 发送响应到 RX Queue
            if (hyperamp_linux_send(HYPERAMP_MSG_TYPE_DATA,
                                   req_hdr->frontend_sess_id,
                                   req_hdr->backend_sess_id,
                                   fake_response,
                                   strlen(fake_response)) == HYPERAMP_OK) {
                printf("[Backend] ✓ HTTP response sent to frontend\n");
            } else {
                printf("[Backend] ✗ Failed to send HTTP response\n");
            }
        } else {
            printf("[Backend] Raw data (not HTTP): %u bytes\n", payload_len);
            
            // 原样回显
            if (hyperamp_linux_send(HYPERAMP_MSG_TYPE_DATA,
                                   req_hdr->frontend_sess_id,
                                   req_hdr->backend_sess_id,
                                   payload,
                                   payload_len) == HYPERAMP_OK) {
                printf("[Backend] ✓ Data echoed back\n");
            }
        }
    } else {
        printf("[Backend] Short data packet: %u bytes\n", payload_len);
    }
    
    printf("[Backend] ==========================================\n");
}

/**
 * @brief 主循环：监听 TX Queue，处理请求
 */
int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    
    printf("\n");
    printf("================================================\n");
    printf("  HyperAMP Backend Proxy Simulator (Linux)\n");
    printf("================================================\n\n");
    
    // 注册信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // 初始化 HyperAMP（连接 seL4 已初始化的队列）
    printf("[Backend] Initializing HyperAMP as CONNECTOR (waiting for seL4 to initialize queues)...\n");
    if (hyperamp_linux_init(0, 0) != HYPERAMP_OK) {  // is_creator=0（等待 seL4 初始化）
        printf("[Backend] ERROR: Failed to initialize HyperAMP\n");
        printf("[Backend] Make sure you have root permissions and /dev/mem access!\n");
        return 1;
    }
    
    printf("[Backend] ✓ Queues initialized\n");
    printf("[Backend] Ready to listen for requests from seL4\n");
    printf("[Backend] Press Ctrl+C to exit\n\n");
    
    // 统计信息
    uint32_t messages_processed = 0;
    uint32_t sessions_created = 0;
    uint32_t http_requests = 0;
    
    // 主循环：简单轮询 TX Queue
    while (g_running) {
        HyperampMsgHeader req_hdr;
        uint8_t payload[4096];
        uint16_t payload_len;
        
        // 从 TX Queue 接收消息（非阻塞）
        int ret = hyperamp_linux_recv(&req_hdr, payload, sizeof(payload) - 1, &payload_len);
        
        if (ret == HYPERAMP_OK) {
            messages_processed++;
            
            // 根据消息类型分发
            switch (req_hdr.proxy_msg_type) {
                case HYPERAMP_MSG_TYPE_SESS:  // 会话管理
                    sessions_created++;
                    handle_session_message(&req_hdr, payload, payload_len);
                    break;
                    
                case HYPERAMP_MSG_TYPE_DATA:  // 数据传输
                    http_requests++;
                    handle_data_message(&req_hdr, payload, payload_len);
                    break;
                    
                case HYPERAMP_MSG_TYPE_DEV:   // 设备控制
                case HYPERAMP_MSG_TYPE_STRGY: // 策略配置
                    payload[payload_len < 4095 ? payload_len : 4095] = '\0';
                    printf("[Backend] Received %s message: %s\n",
                           req_hdr.proxy_msg_type == HYPERAMP_MSG_TYPE_DEV ? "DEVICE" : "STRATEGY",
                           (char *)payload);
                    break;
                    
                default:
                    printf("[Backend] Unknown message type: %u\n", req_hdr.proxy_msg_type);
                    break;
            }
        } else {
            // 队列空，短暂休眠
            usleep(10000);  // 10ms
        }
        
        // 每处理 10 条消息，显示统计信息
        if (messages_processed > 0 && messages_processed % 10 == 0) {
            printf("\n[Backend] --- Statistics ---\n");
            printf("[Backend]   Total messages: %u\n", messages_processed);
            printf("[Backend]   Sessions created: %u\n", sessions_created);
            printf("[Backend]   HTTP requests: %u\n", http_requests);
            printf("[Backend] -------------------\n\n");
        }
    }
    
    // 显示最终统计
    printf("\n[Backend] Final Statistics:\n");
    printf("[Backend]   Total messages processed: %u\n", messages_processed);
    printf("[Backend]   Sessions created: %u\n", sessions_created);
    printf("[Backend]   HTTP requests: %u\n", http_requests);
    
    hyperamp_linux_get_status();
    hyperamp_linux_cleanup();
    
    printf("[Backend] Shutdown complete\n");
    return 0;
}
