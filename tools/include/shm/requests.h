#ifndef _REQUEST_H_
#define _REQUEST_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <stdint.h>
#include "shm/client.h"

typedef struct {
  uint32_t request_id;
  uint32_t service_id;
  uint8_t* data_string;
  uint32_t size;
  char *output_dir;
  struct Client* client;
} Request;

// typedef struct RequestNode {
//   Request request;
//   struct RequestNode* next;
// } RequestNode;

// typedef struct {
//   RequestNode* front;
//   RequestNode* rear;
//   int size;
//   pthread_mutex_t lock;
//   pthread_cond_t cond;
// } RequestQueue;

// RequestQueue *create_request_queue();
// void enqueue(RequestQueue *queue, Request request);
// Request dequeue(RequestQueue *queue);
// int is_empty(RequestQueue *queue);
// void free_request_queue(RequestQueue *queue);


#endif // _REQUEST_H_