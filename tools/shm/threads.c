#include "shm/threads.h"
#include "shm/requests.h"
#include "shm/client.h"
#include "hvisor.h"

ThreadPool *init_thread_pool(int num_threads, struct Client *client) {
  ThreadPool* pool = (ThreadPool*)malloc(sizeof(ThreadPool));
  if (pool == NULL) {
    perror("Failed to allocate thread pool");
    return NULL;
  }

  pthread_mutex_init(&pool->lock, NULL);
  pthread_cond_init(&pool->cond, NULL);
  pool->task_queue = NULL;
  pool->num_threads = num_threads;
  pool->stop = 0;

  pool->threads = (pthread_t*)malloc(sizeof(pthread_t) * num_threads);
  if (pool->threads == NULL) {
      perror("Failed to allocate threads");
      free(pool);
      return NULL;
  }

  for (int i = 0; i < num_threads; i++) {
    WorkerArgs* args = (WorkerArgs*)malloc(sizeof(WorkerArgs));
    if (args == NULL) {
        perror("Failed to allocate worker arguments");
        free(pool->threads);
        free(pool);
        return NULL;
    }
    args->pool = pool;
    args->thread_id = i;
    args->client = client;
    pthread_create(&pool->threads[i], NULL, worker, args);
  }

  return pool;
}

void destroy_thread_pool(ThreadPool* pool) {
  pthread_mutex_lock(&pool->lock);
  pool->stop = 1;
  pthread_cond_broadcast(&pool->cond);
  pthread_mutex_unlock(&pool->lock);

  for (int i = 0; i < pool->num_threads; i++) {
    pthread_join(pool->threads[i], NULL);
  }

  pthread_mutex_destroy(&pool->lock);
  pthread_cond_destroy(&pool->cond);
  free(pool->threads);
  free(pool);
}

void add_task(ThreadPool* pool, void (*func)(void*, void*), void* arg1) {
  Task* task = (Task*)malloc(sizeof(Task));
  if (task == NULL) {
    perror("Failed to allocate task");
    return;
  }
  task->func = func;
  task->arg1 = arg1;
  // task->arg2 = arg2;// passed by execute_task (msg)
  task->next = NULL;

  pthread_mutex_lock(&pool->lock);
  if (pool->task_queue == NULL) {
      pool->task_queue = task;
  } else {
      Task* current = pool->task_queue;
      while (current->next != NULL) {
        current = current->next;
      }
      current->next = task;
  }
  pthread_cond_signal(&pool->cond);
  pthread_mutex_unlock(&pool->lock);
}

void execute_task(void* arg, void* arg2) {
  Task* task = (Task*)arg;
  task->func(task->arg1, arg2);
  free(task);
}

int task_queue_is_empty(ThreadPool* pool) {
  pthread_mutex_lock(&pool->lock);
  int result = (pool->task_queue == NULL);
  pthread_mutex_unlock(&pool->lock);
  return result;
}

void* worker(void* arg) {
  WorkerArgs* args = (WorkerArgs*)arg;
  ThreadPool* pool = args->pool;
  struct Client* amp_client = args->client;
  int thread_id = args->thread_id;

  Task* task;
  struct Msg *msg = client_ops.empty_msg_get(amp_client, 0);// modify service_id later
  if (msg == NULL)
  {
    printf("[Error] empty msg get \n");
    while(1) {}
  }

  while (1) {
      pthread_mutex_lock(&pool->lock);
      while (pool->task_queue == NULL && !pool->stop) {
          pthread_cond_wait(&pool->cond, &pool->lock);
      }
      if (pool->stop) {
          free(args);
          printf("Thread [%d] exit\n", thread_id);
          client_ops.empty_msg_put(amp_client, msg);
          
          pthread_mutex_unlock(&pool->lock);
          pthread_exit(NULL);
      }
      task = pool->task_queue;
      pool->task_queue = task->next;
      pthread_mutex_unlock(&pool->lock);
      printf("thread %d executing task, ", thread_id);
      execute_task(task, msg);
  }
}

void adjust_thread_pool(ThreadPool* pool, int new_num_threads) {
  if (new_num_threads < pool->num_threads) {
      pthread_mutex_lock(&pool->lock);
      pool->stop = 1;
      pthread_cond_broadcast(&pool->cond);
      pthread_mutex_unlock(&pool->lock);

      for (int i = new_num_threads; i < pool->num_threads; i++) {
          pthread_join(pool->threads[i], NULL);
      }
  }

  pool->threads = (pthread_t*)realloc(pool->threads, sizeof(pthread_t) * new_num_threads);
  if (pool->threads == NULL) {
      perror("Failed to reallocate threads");
      return;
  }

  pool->num_threads = new_num_threads;
  pool->stop = 0;

  for (int i = 0; i < new_num_threads; i++) {
      pthread_create(&pool->threads[i], NULL, worker, pool);
  }
}

void handle_request(void* arg1, void* arg2) {
  Request* request = (Request*)arg1;
  struct Msg *msg = (struct Msg*)arg2;
  
  printf("request : %u, id: %u, data_string: %s, size: %u, output_dir: %s\n", 
    request->request_id, request->service_id, request->data_string, request->size, request->output_dir);

  // TODO: add handle requst
  // do safe service request
  // modify msg->service_id
  msg->service_id = request->service_id;
  
  // int ret = general_safe_service_request(request->client, msg, request->request_id, 
  //   request->data_string, request->size, request->output_dir);
  // if (ret != 0) {
  //     printf("[Error] safe service request failed\n");
  //     return -1;
  // }

  free(request->output_dir);
  free(request->data_string);
  free(request);
}

// int test_threads_pool(int number) {
//   ThreadPool* pool = init_thread_pool(number);
//   if (pool == NULL) {
//       perror("Failed to create thread pool");
//       return 1;
//   }

//   for (int i = 0; i < number; i++) {
//       int* thread_num = (int*)malloc(sizeof(int));
//       int* type = (int*)malloc(sizeof(int));
//       *thread_num = i;
//       *type = i % 2 + 1;
//       add_task(pool, example_task, thread_num, type);
//   }

//   adjust_thread_pool(pool, number * 2);

//   for (int i = number; i < number * 2; i++) {
//       int* thread_num = (int*)malloc(sizeof(int));
//       int* type = (int*)malloc(sizeof(int));
//       *thread_num = i;
//       *type = i % 2 + 1; // Just an example type
//       add_task(pool, example_task, thread_num, type);
//   }

//   adjust_thread_pool(pool, number / 2);

//   for (int i = number * 2; i < number * 3; i++) {
//       int* thread_num = (int*)malloc(sizeof(int));
//       int* type = (int*)malloc(sizeof(int));
//       *thread_num = i;
//       *type = i % 2 + 1;
//       add_task(pool, example_task, thread_num, type);
//   }

//   destroy_thread_pool(pool);
//   printf("Thread pool destroyed\n");

//   return 0;
// }

ThreadPool* setup_threads_pool(int number, struct Client* amp_client) {
  ThreadPool* pool = init_thread_pool(number, amp_client);
  if (pool == NULL) {
      perror("Failed to create thread pool");
      return NULL;
  }
  return pool;
}