// #include "shm/requests.h"

// RequestQueue* create_request_queue() {
//     RequestQueue* queue = (RequestQueue*)malloc(sizeof(RequestQueue));
//     if (queue == NULL) {
//         perror("Failed to allocate memory for request queue");
//         return NULL;
//     }
//     queue->front = queue->rear = NULL;
//     queue->size = 0;
//     pthread_mutex_init(&queue->lock, NULL);
//     pthread_cond_init(&queue->cond, NULL);
//     return queue;
// }

// void enqueue(RequestQueue* queue, Request request) {
//     pthread_mutex_lock(&queue->lock);

//     RequestNode* new_node = (RequestNode*)malloc(sizeof(RequestNode));
//     if (new_node == NULL) {
//         perror("Failed to allocate memory for new request node");
//         pthread_mutex_unlock(&queue->lock);
//         return;
//     }
//     new_node->request = request;
//     new_node->next = NULL;

//     if (queue->rear == NULL) {
//         queue->front = queue->rear = new_node;
//     } else {
//         queue->rear->next = new_node;
//         queue->rear = new_node;
//     }
//     queue->size++;

//     pthread_cond_signal(&queue->cond);
//     pthread_mutex_unlock(&queue->lock);
// }

// Request dequeue(RequestQueue* queue) {
//     pthread_mutex_lock(&queue->lock);

//     while (is_empty(queue)) {
//         pthread_cond_wait(&queue->cond, &queue->lock);
//     }

//     RequestNode* temp = queue->front;
//     Request request = temp->request;
//     queue->front = queue->front->next;

//     if (queue->front == NULL) {
//         queue->rear = NULL;
//     }

//     queue->size--;
//     pthread_mutex_unlock(&queue->lock);

//     free(temp);
//     return request;
// }

// int is_empty(RequestQueue* queue) {
//     return (queue->front == NULL);
// }

// void free_request_queue(RequestQueue* queue) {
//     while (!is_empty(queue)) {
//         RequestNode* temp = queue->front;
//         queue->front = queue->front->next;
//         free(temp->request.data_string);
//         free(temp);
//     }
//     pthread_mutex_destroy(&queue->lock);
//     pthread_cond_destroy(&queue->cond);
//     free(queue);
// }