#ifndef _THREADS_H_
#define _THREADS_H_

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include "shm/client.h"

typedef struct Task {
  void (*func)(void*, void*);  
  void* arg1;  
  void* arg2;  
  struct Task* next;
} Task;

typedef struct ThreadPool {
  pthread_mutex_t lock;
  pthread_cond_t cond;
  Task* task_queue;
  int num_threads;
  pthread_t* threads;
  int stop;
} ThreadPool;

typedef struct {
  struct Client* client;
  ThreadPool* pool;
  int thread_id;
} WorkerArgs;

ThreadPool *init_thread_pool(int num_threads, struct Client *client);
void destroy_thread_pool(ThreadPool *pool);

void execute_task(void *arg, void *arg2);
void *worker(void *arg);
void add_task(ThreadPool *pool, void (*func)(void *, void*), void* arg1);

int task_queue_is_empty(ThreadPool *pool);
void handle_request(void *arg1, void *arg2);
ThreadPool *setup_threads_pool(int number, struct Client *amp_client);

#endif // _THREADS_H_