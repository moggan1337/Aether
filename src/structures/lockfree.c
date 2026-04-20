/**
 * Aether - Lock-Free Data Structures Implementation
 * RCU and MVRLU implementations
 *
 * Copyright (c) 2024 Aether Authors
 */

#include <structures/lockfree.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

// ============================================================================
// RCU Implementation
// ============================================================================

aether_rcu_domain_t* aether_rcu_domain_create(const aether_rcu_config_t* config) {
    aether_rcu_domain_t* domain = (aether_rcu_domain_t*)calloc(1, sizeof(aether_rcu_domain_t));
    if (!domain) {
        return NULL;
    }
    
    domain->num_nodes = config ? config->num_domains : 1;
    domain->callback_batch_size = config ? config->callback_batch_size : 32;
    domain->current_batch = 0;
    
    // Allocate grace period counters
    domain->gp_counters = (uint64_t*)calloc(domain->num_nodes, sizeof(uint64_t));
    domain->scan_counters = (uint64_t*)calloc(domain->num_nodes, sizeof(uint64_t));
    
    if (!domain->gp_counters || !domain->scan_counters) {
        free(domain->gp_counters);
        free(domain->scan_counters);
        free(domain);
        return NULL;
    }
    
    pthread_mutex_init(&domain->lock, NULL);
    pthread_cond_init(&domain->cond, NULL);
    
    return domain;
}

void aether_rcu_domain_destroy(aether_rcu_domain_t* domain) {
    if (!domain) {
        return;
    }
    
    // Process remaining callbacks
    pthread_mutex_destroy(&domain->lock);
    pthread_cond_destroy(&domain->cond);
    free(domain->gp_counters);
    free(domain->scan_counters);
    free(domain);
}

void aether_rcu_reader_lock(aether_rcu_reader_t* reader, aether_rcu_domain_t* domain) {
    reader->seq = atomic_load_explicit(&domain->current_batch, memory_order_acquire);
    reader->critical = true;
}

void aether_rcu_reader_unlock(aether_rcu_reader_t* reader) {
    reader->critical = false;
}

bool aether_rcu_reader_quiescent(aether_rcu_reader_t* reader) {
    if (!reader || !reader->critical) {
        return true;
    }
    
    // Check if grace period has advanced
    // This is a simplified check
    return false;
}

void aether_rcu_assign_pointer(aether_rcu_ptr_t* dst, void* src, aether_rcu_domain_t* domain) {
    // Store barrier before publish
    atomic_thread_fence(memory_order_release);
    
    // Update pointer with version
    uint64_t new_version = atomic_fetch_add_explicit(&dst->version, 1, 
                                                       memory_order_relaxed) + 1;
    atomic_store_explicit(&dst->ptr, src, memory_order_release);
    
    // Full barrier after publish
    atomic_thread_fence(memory_order_seq_cst);
    
    (void)domain;
}

void* aether_rcu_dereference(aether_rcu_ptr_t* src, aether_rcu_domain_t* domain) {
    uint64_t seq = atomic_load_explicit(&src->version, memory_order_acquire);
    
    // Load with acquire barrier
    void* ptr = atomic_load_explicit(&src->ptr, memory_order_acquire);
    
    // Re-check version
    if (seq != atomic_load_explicit(&src->version, memory_order_acquire)) {
        // Retry
        return aether_rcu_dereference(src, domain);
    }
    
    (void)domain;
    return ptr;
}

void* aether_rcu_xchg_pointer(aether_rcu_ptr_t* dst, void* src, aether_rcu_domain_t* domain) {
    // Full barrier
    atomic_thread_fence(memory_order_seq_cst);
    
    uint64_t old_version = atomic_fetch_add_explicit(&dst->version, 1, 
                                                       memory_order_relaxed) + 1;
    void* old_ptr = atomic_exchange_explicit(&dst->ptr, src, memory_order_release);
    
    // Full barrier
    atomic_thread_fence(memory_order_seq_cst);
    
    (void)domain;
    (void)old_version;
    return old_ptr;
}

void aether_rcu_synchronize(aether_rcu_domain_t* domain) {
    if (!domain) {
        return;
    }
    
    pthread_mutex_lock(&domain->lock);
    
    // Advance grace period
    uint64_t current_gp = atomic_load_explicit(&domain->current_batch, 
                                                memory_order_relaxed);
    atomic_store_explicit(&domain->current_batch, current_gp + 1,
                          memory_order_release);
    
    // Full barrier
    atomic_thread_fence(memory_order_seq_cst);
    
    // Wait for all readers to observe new grace period
    bool all_quiescent = false;
    int iterations = 0;
    
    while (!all_quiescent && iterations < 1000) {
        all_quiescent = true;
        for (uint32_t i = 0; i < domain->num_nodes; i++) {
            if (domain->scan_counters[i] < current_gp) {
                all_quiescent = false;
                break;
            }
        }
        iterations++;
    }
    
    pthread_mutex_unlock(&domain->lock);
}

void aether_rcu_call(aether_rcu_domain_t* domain, aether_rcu_callback_t callback, void* arg) {
    if (!domain || !callback) {
        return;
    }
    
    // Call callback immediately (simplified)
    callback(arg);
}

// ============================================================================
// MVRLU Implementation
// ============================================================================

aether_mrvlu_object_t* aether_mrvlu_create(const aether_mrvlu_config_t* config) {
    aether_mrvlu_object_t* obj = (aether_mrvlu_object_t*)calloc(1, sizeof(aether_mrvlu_object_t));
    if (!obj) {
        return NULL;
    }
    
    obj->data_size = config ? config->data_size : 64;
    obj->max_versions = config ? config->max_versions : 8;
    obj->destructor = config ? config->destructor : NULL;
    
    pthread_mutex_init(&obj->header.write_lock, NULL);
    
    obj->data = malloc(obj->data_size);
    if (!obj->data) {
        pthread_mutex_destroy(&obj->header.write_lock);
        free(obj);
        return NULL;
    }
    
    return obj;
}

void aether_mrvlu_destroy(aether_mrvlu_object_t* obj) {
    if (!obj) {
        return;
    }
    
    if (obj->destructor && obj->data) {
        obj->destructor(obj->data);
    }
    
    free(obj->data);
    pthread_mutex_destroy(&obj->header.write_lock);
    free(obj);
}

aether_mrvlu_read_handle_t* aether_mrvlu_read_begin(aether_mrvlu_object_t* obj) {
    if (!obj) {
        return NULL;
    }
    
    aether_mrvlu_read_handle_t* handle = (aether_mrvlu_read_handle_t*)calloc(
        1, sizeof(aether_mrvlu_read_handle_t));
    if (!handle) {
        return NULL;
    }
    
    handle->obj = obj;
    
    // Increment read count
    atomic_fetch_add_explicit(&obj->header.read_count, 1, memory_order_acq_rel);
    
    // Capture version
    handle->version = atomic_load_explicit(&obj->header.version, memory_order_acquire);
    
    // Copy data
    handle->snapshot = malloc(obj->data_size);
    if (handle->snapshot) {
        memcpy(handle->snapshot, obj->data, obj->data_size);
        handle->valid = true;
    }
    
    return handle;
}

void* aether_mrvlu_read_get(aether_mrvlu_read_handle_t* handle) {
    if (!handle || !handle->valid) {
        return NULL;
    }
    
    // Verify version hasn't changed
    uint64_t current_version = atomic_load_explicit(
        &handle->obj->header.version, memory_order_acquire);
    
    if (current_version != handle->version) {
        // Version changed, data might be stale
        // In a full implementation, would retry or signal
        return NULL;
    }
    
    return handle->snapshot;
}

void aether_mrvlu_read_end(aether_mrvlu_read_handle_t* handle) {
    if (!handle) {
        return;
    }
    
    // Decrement read count
    atomic_fetch_sub_explicit(&handle->obj->header.read_count, 1, memory_order_acq_rel);
    
    if (handle->snapshot) {
        free(handle->snapshot);
    }
    
    free(handle);
}

aether_mrvlu_write_handle_t* aether_mrvlu_write_begin(aether_mrvlu_object_t* obj) {
    if (!obj) {
        return NULL;
    }
    
    aether_mrvlu_write_handle_t* handle = (aether_mrvlu_write_handle_t*)calloc(
        1, sizeof(aether_mrvlu_write_handle_t));
    if (!handle) {
        return NULL;
    }
    
    handle->obj = obj;
    handle->committed = false;
    
    // Acquire write lock
    pthread_mutex_lock(&obj->header.write_lock);
    
    // Wait for readers
    while (atomic_load_explicit(&obj->header.read_count, memory_order_acquire) > 0) {
        // Spin wait
    }
    
    // Allocate new data buffer
    handle->new_data = malloc(obj->data_size);
    if (!handle->new_data) {
        pthread_mutex_unlock(&obj->header.write_lock);
        free(handle);
        return NULL;
    }
    
    // Copy current data
    memcpy(handle->new_data, obj->data, obj->data_size);
    
    return handle;
}

aether_result_t aether_mrvlu_write_set(aether_mrvlu_write_handle_t* handle, const void* data) {
    if (!handle || !data) {
        return AETHER_ERR_INVALID_PARAM;
    }
    
    memcpy(handle->new_data, data, handle->obj->data_size);
    return AETHER_SUCCESS;
}

aether_result_t aether_mrvlu_write_commit(aether_mrvlu_write_handle_t* handle) {
    if (!handle || handle->committed) {
        return AETHER_ERR_INVALID_PARAM;
    }
    
    aether_mrvlu_object_t* obj = handle->obj;
    
    // Copy new data to object
    memcpy(obj->data, handle->new_data, obj->data_size);
    
    // Increment version
    atomic_fetch_add_explicit(&obj->header.version, 1, memory_order_release);
    
    handle->committed = true;
    
    // Release write lock
    pthread_mutex_unlock(&obj->header.write_lock);
    
    return AETHER_SUCCESS;
}

void aether_mrvlu_write_abort(aether_mrvlu_write_handle_t* handle) {
    if (!handle) {
        return;
    }
    
    if (handle->new_data) {
        free(handle->new_data);
    }
    
    if (handle->obj) {
        pthread_mutex_unlock(&handle->obj->header.write_lock);
    }
    
    free(handle);
}

// ============================================================================
// Lock-Free Queue (Michael-Scott Queue)
// ============================================================================

aether_lf_queue_t* aether_lf_queue_create(void) {
    aether_lf_queue_t* queue = (aether_lf_queue_t*)calloc(1, sizeof(aether_lf_queue_t));
    if (!queue) {
        return NULL;
    }
    
    // Create dummy node
    aether_lf_queue_node_t* dummy = (aether_lf_queue_node_t*)calloc(
        1, sizeof(aether_lf_queue_node_t));
    if (!dummy) {
        free(queue);
        return NULL;
    }
    
    dummy->enqueued_seq = 0;
    atomic_store_explicit(&dummy->next, NULL, memory_order_relaxed);
    
    atomic_store_explicit(&queue->head, dummy, memory_order_relaxed);
    atomic_store_explicit(&queue->tail, dummy, memory_order_relaxed);
    atomic_store_explicit(&queue->count, 0, memory_order_relaxed);
    
    return queue;
}

void aether_lf_queue_destroy(aether_lf_queue_t* queue) {
    if (!queue) {
        return;
    }
    
    void* data;
    while (aether_lf_queue_dequeue(queue, &data) == AETHER_SUCCESS) {
        // Free data if needed
    }
    
    aether_lf_queue_node_t* head = atomic_load_explicit(&queue->head, 
                                                          memory_order_relaxed);
    free(head);
    free(queue);
}

aether_result_t aether_lf_queue_enqueue(aether_lf_queue_t* queue, void* data) {
    if (!queue) {
        return AETHER_ERR_INVALID_PARAM;
    }
    
    aether_lf_queue_node_t* node = (aether_lf_queue_node_t*)calloc(
        1, sizeof(aether_lf_queue_node_t));
    if (!node) {
        return AETHER_ERR_OOM;
    }
    
    node->data = data;
    atomic_store_explicit(&node->next, NULL, memory_order_relaxed);
    
    while (1) {
        aether_lf_queue_node_t* tail = atomic_load_explicit(&queue->tail,
                                                             memory_order_acquire);
        aether_lf_queue_node_t* next = atomic_load_explicit(&tail->next,
                                                            memory_order_acquire);
        
        if (tail == atomic_load_explicit(&queue->tail, memory_order_acquire)) {
            if (next == NULL) {
                // Try to append
                aether_lf_queue_node_t* expected = NULL;
                if (atomic_compare_exchange_weak_explicit(
                        &tail->next, &expected, node,
                        memory_order_release, memory_order_relaxed)) {
                    // Success - try to advance tail
                    atomic_compare_exchange_weak_explicit(
                        &queue->tail, &tail, node,
                        memory_order_release, memory_order_relaxed);
                    atomic_fetch_add_explicit(&queue->count, 1, memory_order_relaxed);
                    return AETHER_SUCCESS;
                }
            } else {
                // Tail fell behind - try to advance it
                atomic_compare_exchange_weak_explicit(
                    &queue->tail, &tail, next,
                    memory_order_release, memory_order_relaxed);
            }
        }
    }
}

aether_result_t aether_lf_queue_dequeue(aether_lf_queue_t* queue, void** data) {
    if (!queue || !data) {
        return AETHER_ERR_INVALID_PARAM;
    }
    
    while (1) {
        aether_lf_queue_node_t* head = atomic_load_explicit(&queue->head,
                                                              memory_order_acquire);
        aether_lf_queue_node_t* tail = atomic_load_explicit(&queue->tail,
                                                             memory_order_acquire);
        aether_lf_queue_node_t* next = atomic_load_explicit(&head->next,
                                                            memory_order_acquire);
        
        if (head == atomic_load_explicit(&queue->head, memory_order_acquire)) {
            if (head == tail) {
                if (next == NULL) {
                    // Queue is empty
                    *data = NULL;
                    return AETHER_ERR_INVALID_PARAM;
                }
                // Tail fell behind - try to advance
                atomic_compare_exchange_weak_explicit(
                    &queue->tail, &tail, next,
                    memory_order_release, memory_order_relaxed);
            } else {
                // Has a valid node
                *data = next->data;
                if (atomic_compare_exchange_weak_explicit(
                        &queue->head, &head, next,
                        memory_order_release, memory_order_relaxed)) {
                    atomic_fetch_sub_explicit(&queue->count, 1, memory_order_relaxed);
                    free(head);
                    return AETHER_SUCCESS;
                }
            }
        }
    }
}

bool aether_lf_queue_is_empty(aether_lf_queue_t* queue) {
    if (!queue) {
        return true;
    }
    
    aether_lf_queue_node_t* head = atomic_load_explicit(&queue->head,
                                                          memory_order_acquire);
    aether_lf_queue_node_t* next = atomic_load_explicit(&head->next,
                                                         memory_order_acquire);
    
    return next == NULL;
}

// ============================================================================
// Lock-Free Stack (Treiber Stack)
// ============================================================================

aether_lf_stack_t* aether_lf_stack_create(void) {
    aether_lf_stack_t* stack = (aether_lf_stack_t*)calloc(1, sizeof(aether_lf_stack_t));
    if (!stack) {
        return NULL;
    }
    
    atomic_store_explicit(&stack->head, NULL, memory_order_relaxed);
    atomic_store_explicit(&stack->count, 0, memory_order_relaxed);
    
    return stack;
}

void aether_lf_stack_destroy(aether_lf_stack_t* stack) {
    if (!stack) {
        return;
    }
    
    void* data;
    while (aether_lf_stack_pop(stack, &data) == AETHER_SUCCESS) {
        // Free data if needed
    }
    
    free(stack);
}

aether_result_t aether_lf_stack_push(aether_lf_stack_t* stack, void* data) {
    if (!stack) {
        return AETHER_ERR_INVALID_PARAM;
    }
    
    aether_lf_stack_node_t* node = (aether_lf_stack_node_t*)calloc(
        1, sizeof(aether_lf_stack_node_t));
    if (!node) {
        return AETHER_ERR_OOM;
    }
    
    node->data = data;
    
    aether_lf_stack_node_t* head = atomic_load_explicit(&stack->head,
                                                          memory_order_acquire);
    
    while (1) {
        atomic_store_explicit(&node->next, head, memory_order_relaxed);
        
        if (atomic_compare_exchange_weak_explicit(
                &stack->head, &head, node,
                memory_order_release, memory_order_acquire)) {
            atomic_fetch_add_explicit(&stack->count, 1, memory_order_relaxed);
            return AETHER_SUCCESS;
        }
    }
}

aether_result_t aether_lf_stack_pop(aether_lf_stack_t* stack, void** data) {
    if (!stack || !data) {
        return AETHER_ERR_INVALID_PARAM;
    }
    
    while (1) {
        aether_lf_stack_node_t* head = atomic_load_explicit(&stack->head,
                                                             memory_order_acquire);
        
        if (head == NULL) {
            *data = NULL;
            return AETHER_ERR_INVALID_PARAM;
        }
        
        aether_lf_stack_node_t* next = atomic_load_explicit(&head->next,
                                                             memory_order_acquire);
        
        if (atomic_compare_exchange_weak_explicit(
                &stack->head, &head, next,
                memory_order_release, memory_order_acquire)) {
            *data = head->data;
            atomic_fetch_sub_explicit(&stack->count, 1, memory_order_relaxed);
            free(head);
            return AETHER_SUCCESS;
        }
    }
}

bool aether_lf_stack_is_empty(aether_lf_stack_t* stack) {
    if (!stack) {
        return true;
    }
    
    return atomic_load_explicit(&stack->head, memory_order_acquire) == NULL;
}

// ============================================================================
// Lock-Free Deque (Bounded)
// ============================================================================

aether_lf_deque_t* aether_lf_deque_create(size_t capacity) {
    // Round up capacity to power of 2
    size_t actual_capacity = 1;
    while (actual_capacity < capacity) {
        actual_capacity *= 2;
    }
    
    aether_lf_deque_t* deque = (aether_lf_deque_t*)calloc(1, sizeof(aether_lf_deque_t));
    if (!deque) {
        return NULL;
    }
    
    deque->buffer = (void**)calloc(actual_capacity, sizeof(void*));
    if (!deque->buffer) {
        free(deque);
        return NULL;
    }
    
    deque->capacity = actual_capacity;
    deque->mask = actual_capacity - 1;
    
    atomic_store_explicit(&deque->head, 0, memory_order_relaxed);
    atomic_store_explicit(&deque->tail, 0, memory_order_relaxed);
    
    return deque;
}

void aether_lf_deque_destroy(aether_lf_deque_t* deque) {
    if (!deque) {
        return;
    }
    
    free(deque->buffer);
    free(deque);
}

aether_result_t aether_lf_deque_push_left(aether_lf_deque_t* deque, void* data) {
    if (!deque) {
        return AETHER_ERR_INVALID_PARAM;
    }
    
    while (1) {
        uint64_t head = atomic_load_explicit(&deque->head, memory_order_acquire);
        uint64_t tail = atomic_load_explicit(&deque->tail, memory_order_acquire);
        
        if ((head - tail) >= deque->capacity - 1) {
            return AETHER_ERR_OUT_OF_RESOURCES;
        }
        
        if (atomic_compare_exchange_weak_explicit(
                &deque->head, &head, head + 1,
                memory_order_release, memory_order_acquire)) {
            deque->buffer[head & deque->mask] = data;
            return AETHER_SUCCESS;
        }
    }
}

aether_result_t aether_lf_deque_push_right(aether_lf_deque_t* deque, void* data) {
    if (!deque) {
        return AETHER_ERR_INVALID_PARAM;
    }
    
    while (1) {
        uint64_t tail = atomic_load_explicit(&deque->tail, memory_order_acquire);
        uint64_t head = atomic_load_explicit(&deque->head, memory_order_acquire);
        
        if ((head - tail) >= deque->capacity - 1) {
            return AETHER_ERR_OUT_OF_RESOURCES;
        }
        
        if (atomic_compare_exchange_weak_explicit(
                &deque->tail, &tail, tail + 1,
                memory_order_release, memory_order_acquire)) {
            deque->buffer[tail & deque->mask] = data;
            return AETHER_SUCCESS;
        }
    }
}

aether_result_t aether_lf_deque_pop_left(aether_lf_deque_t* deque, void** data) {
    if (!deque || !data) {
        return AETHER_ERR_INVALID_PARAM;
    }
    
    while (1) {
        uint64_t head = atomic_load_explicit(&deque->head, memory_order_acquire);
        uint64_t tail = atomic_load_explicit(&deque->tail, memory_order_acquire);
        
        if (head == tail) {
            *data = NULL;
            return AETHER_ERR_INVALID_PARAM;
        }
        
        head -= 1;
        void* item = deque->buffer[head & deque->mask];
        
        if (atomic_compare_exchange_weak_explicit(
                &deque->head, &head, head,
                memory_order_release, memory_order_acquire)) {
            *data = item;
            return AETHER_SUCCESS;
        }
    }
}

aether_result_t aether_lf_deque_pop_right(aether_lf_deque_t* deque, void** data) {
    if (!deque || !data) {
        return AETHER_ERR_INVALID_PARAM;
    }
    
    while (1) {
        uint64_t head = atomic_load_explicit(&deque->head, memory_order_acquire);
        uint64_t tail = atomic_load_explicit(&deque->tail, memory_order_acquire);
        
        if (head == tail) {
            *data = NULL;
            return AETHER_ERR_INVALID_PARAM;
        }
        
        void* item = deque->buffer[tail & deque->mask];
        
        if (atomic_compare_exchange_weak_explicit(
                &deque->tail, &tail, tail + 1,
                memory_order_release, memory_order_acquire)) {
            *data = item;
            return AETHER_SUCCESS;
        }
    }
}

bool aether_lf_deque_is_empty(aether_lf_deque_t* deque) {
    if (!deque) {
        return true;
    }
    
    return atomic_load_explicit(&deque->head, memory_order_acquire) ==
           atomic_load_explicit(&deque->tail, memory_order_acquire);
}

// ============================================================================
// Lock-Free Hash Map
// ============================================================================

aether_lf_hashmap_t* aether_lf_hashmap_create(size_t capacity) {
    aether_lf_hashmap_t* map = (aether_lf_hashmap_t*)calloc(1, sizeof(aether_lf_hashmap_t));
    if (!map) {
        return NULL;
    }
    
    map->num_buckets = capacity;
    map->buckets = (aether_lf_hash_bucket_t**)calloc(capacity, 
                                                       sizeof(aether_lf_hash_bucket_t*));
    if (!map->buckets) {
        free(map);
        return NULL;
    }
    
    return map;
}

void aether_lf_hashmap_destroy(aether_lf_hashmap_t* map) {
    if (!map) {
        return;
    }
    
    for (size_t i = 0; i < map->num_buckets; i++) {
        aether_lf_hash_bucket_t* bucket = map->buckets[i];
        while (bucket) {
            aether_lf_hash_bucket_t* next = atomic_load_explicit(&bucket->next,
                                                                   memory_order_relaxed);
            free(bucket);
            bucket = next;
        }
    }
    
    free(map->buckets);
    free(map);
}

static size_t hash_key(uint64_t key, size_t num_buckets) {
    // Simple hash function
    key ^= key >> 33;
    key *= 0xff51afd7ed558ccdULL;
    key ^= key >> 33;
    return key % num_buckets;
}

aether_result_t aether_lf_hashmap_insert(aether_lf_hashmap_t* map, uint64_t key, void* value) {
    if (!map) {
        return AETHER_ERR_INVALID_PARAM;
    }
    
    size_t idx = hash_key(key, map->num_buckets);
    
    aether_lf_hash_bucket_t* new_bucket = (aether_lf_hash_bucket_t*)calloc(
        1, sizeof(aether_lf_hash_bucket_t));
    if (!new_bucket) {
        return AETHER_ERR_OOM;
    }
    
    new_bucket->key = key;
    new_bucket->value = value;
    atomic_store_explicit(&new_bucket->marked, false, memory_order_relaxed);
    
    while (1) {
        aether_lf_hash_bucket_t* head = map->buckets[idx];
        
        // Check if key exists
        aether_lf_hash_bucket_t* curr = head;
        while (curr) {
            if (curr->key == key && 
                !atomic_load_explicit(&curr->marked, memory_order_acquire)) {
                // Key exists, update value
                curr->value = value;
                free(new_bucket);
                return AETHER_SUCCESS;
            }
            curr = atomic_load_explicit(&curr->next, memory_order_acquire);
        }
        
        // Insert new bucket at head
        atomic_store_explicit(&new_bucket->next, head, memory_order_relaxed);
        
        if (atomic_compare_exchange_weak_explicit(
                &map->buckets[idx], &head, new_bucket,
                memory_order_release, memory_order_acquire)) {
            atomic_fetch_add_explicit(&map->count, 1, memory_order_relaxed);
            return AETHER_SUCCESS;
        }
    }
}

aether_result_t aether_lf_hashmap_lookup(aether_lf_hashmap_t* map, uint64_t key, void** value) {
    if (!map || !value) {
        return AETHER_ERR_INVALID_PARAM;
    }
    
    size_t idx = hash_key(key, map->num_buckets);
    
    aether_lf_hash_bucket_t* curr = map->buckets[idx];
    while (curr) {
        if (curr->key == key && 
            !atomic_load_explicit(&curr->marked, memory_order_acquire)) {
            *value = curr->value;
            return AETHER_SUCCESS;
        }
        curr = atomic_load_explicit(&curr->next, memory_order_acquire);
    }
    
    return AETHER_ERR_INVALID_PARAM;
}

aether_result_t aether_lf_hashmap_delete(aether_lf_hashmap_t* map, uint64_t key) {
    if (!map) {
        return AETHER_ERR_INVALID_PARAM;
    }
    
    size_t idx = hash_key(key, map->num_buckets);
    
    aether_lf_hash_bucket_t* curr = map->buckets[idx];
    while (curr) {
        if (curr->key == key) {
            atomic_store_explicit(&curr->marked, true, memory_order_release);
            atomic_fetch_sub_explicit(&map->count, 1, memory_order_relaxed);
            return AETHER_SUCCESS;
        }
        curr = atomic_load_explicit(&curr->next, memory_order_acquire);
    }
    
    return AETHER_ERR_INVALID_PARAM;
}

size_t aether_lf_hashmap_size(aether_lf_hashmap_t* map) {
    if (!map) {
        return 0;
    }
    
    return atomic_load_explicit(&map->count, memory_order_acquire);
}
