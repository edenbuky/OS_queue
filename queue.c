#include <stdio.h>
#include <stdlib.h>
#include <threads.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct node {
    void* value;
    struct node* next;
} node_t;

typedef struct {
    node_t dummy; // Dummy node to simplify enqueue and dequeue operations
    node_t* head;
    node_t* tail;
    mtx_t lock;
    cnd_t not_empty;
    atomic_size_t size;
    atomic_size_t waiting; 
    atomic_size_t visited; 
} queue_t;

static queue_t queue;

void initQueue(void) {
    queue.head = &queue.dummy;
    queue.tail = &queue.dummy;
    mtx_init(&queue.lock, mtx_plain);
    cnd_init(&queue.not_empty);
    atomic_store(&queue.size, 0);
    atomic_store(&queue.waiting, 0);
    atomic_store(&queue.visited, 0);
}

void destroyQueue(void) {
    // Release all resources correctly
    mtx_lock(&queue.lock);
    node_t* current = queue.head;
    while (current != NULL) {
        node_t* temp = current;
        current = current->next;
        if (temp != &queue.dummy) { // Skip dummy node to avoid freeing static memory
            free(temp);
        }
    }
    mtx_unlock(&queue.lock);
    mtx_destroy(&queue.lock);
    cnd_destroy(&queue.not_empty);
}

void enqueue(void* value) {
    node_t* temp = malloc(sizeof(node_t));
    temp->value = value;
    temp->next = NULL;

    mtx_lock(&queue.lock);
    queue.tail->next = temp;
    queue.tail = temp;
    atomic_fetch_add_explicit(&queue.size, 1, memory_order_release);
    
    mtx_unlock(&queue.lock);
    // Signal only if there's potentially waiting threads
    if (atomic_load_explicit(&queue.waiting, memory_order_acquire) > 0) {
        cnd_signal(&queue.not_empty);
    }
    atomic_fetch_add_explicit(&queue.visited, 1, memory_order_relaxed);
}

void* dequeue(void) {
    mtx_lock(&queue.lock);
    while (queue.head == queue.tail) {
        atomic_fetch_add_explicit(&queue.waiting, 1, memory_order_relaxed);
        cnd_wait(&queue.not_empty, &queue.lock);
        atomic_fetch_sub_explicit(&queue.waiting, 1, memory_order_relaxed);
    }

    node_t* temp = queue.head->next;
    void* value = temp->value;
    queue.head->next = temp->next;
    if (queue.tail == temp) {
        queue.tail = &queue.dummy; // Adjust tail if we're removing the last node
    }
    atomic_fetch_sub_explicit(&queue.size, 1, memory_order_release);
    mtx_unlock(&queue.lock);
    free(temp);
    return value;
}

bool tryDequeue(void** value) {
    mtx_lock(&queue.lock);
    if (queue.head == queue.tail) { // or if queue.head->next == NULL for the dummy node implementation
        mtx_unlock(&queue.lock);
        return false; // Queue is empty
    }
    node_t* temp = queue.head->next;
    *value = temp->value;
    queue.head->next = temp->next;
    if (queue.tail == temp) {
        queue.tail = &queue.dummy;
    }
    atomic_fetch_sub_explicit(&queue.size, 1, memory_order_release);
    mtx_unlock(&queue.lock);
    free(temp);
    return true;
}

size_t size(void) {
    return atomic_load_explicit(&queue.size, memory_order_acquire);
}

size_t waiting(void) {
    return atomic_load_explicit(&queue.waiting, memory_order_acquire);
}

size_t visited(void) {
    return atomic_load_explicit(&queue.visited, memory_order_acquire);
}
