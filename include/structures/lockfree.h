/**
 * Aether - Lock-Free Data Structures
 * RCU (Read-Copy-Update) and MVRLU implementations
 *
 * Copyright (c) 2024 Aether Authors
 */

#ifndef AETHER_LOCKFREE_H
#define AETHER_LOCKFREE_H

#include <aether/aether_common.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// RCU (Read-Copy-Update) Implementation
// ============================================================================

// RCU Reader Critical Section
typedef struct {
    uint64_t seq;
    bool critical;
} aether_rcu_reader_t;

// RCU Callback
typedef void (*aether_rcu_callback_t)(void* ptr);

// RCU Synchronization
typedef struct aether_rcu_domain {
    // Grace period tracking
    uint64_t* gp_counters;
    uint64_t* scan_counters;
    uint32_t num_nodes;
    uint32_t current_batch;
    
    // Callback queue
    struct {
        void (*callback)(void*);
        void* arg;
        struct aether_rcu_callback_node* next;
    }* callbacks;
    
    pthread_mutex_t lock;
    pthread_cond_t cond;
} aether_rcu_domain_t;

// RCU Pointers
typedef struct {
    void* ptr;
    _Atomic uint64_t version;
} aether_rcu_ptr_t;

// RCU Configuration
typedef struct {
    uint32_t num_domains;
    uint32_t callback_batch_size;
    uint32_t quiescent_bound;
    uint32_t gp_sync_timeout_ms;
} aether_rcu_config_t;

// RCU Domain Operations
aether_rcu_domain_t* aether_rcu_domain_create(const aether_rcu_config_t* config);
void aether_rcu_domain_destroy(aether_rcu_domain_t* domain);

// RCU Reader Operations
void aether_rcu_reader_lock(aether_rcu_reader_t* reader, aether_rcu_domain_t* domain);
void aether_rcu_reader_unlock(aether_rcu_reader_t* reader);
bool aether_rcu_reader_quiescent(aether_rcu_reader_t* reader);

// RCU Pointer Operations
void aether_rcu_assign_pointer(aether_rcu_ptr_t* dst, void* src, aether_rcu_domain_t* domain);
void* aether_rcu_dereference(aether_rcu_ptr_t* src, aether_rcu_domain_t* domain);
void* aether_rcu_xchg_pointer(aether_rcu_ptr_t* dst, void* src, aether_rcu_domain_t* domain);

// RCU Synchronization
void aether_rcu_synchronize(aether_rcu_domain_t* domain);
void aether_rcu_call(aether_rcu_domain_t* domain, aether_rcu_callback_t callback, void* arg);

// ============================================================================
// MVRLU (Multi-Version Read-Lock-Unlock) Implementation
// ============================================================================

// MVRLU Object Header
typedef struct {
    _Atomic uint64_t version;
    _Atomic uint32_t read_count;
    pthread_mutex_t write_lock;
    uint32_t max_versions;
} aether_mrvlu_header_t;

// MVRLU Object
typedef struct {
    aether_mrvlu_header_t header;
    void* data;
    size_t data_size;
    void (*destructor)(void*);
} aether_mrvlu_object_t;

// MVRLU Read Handle
typedef struct {
    aether_mrvlu_object_t* obj;
    uint64_t version;
    void* snapshot;
    bool valid;
} aether_mrvlu_read_handle_t;

// MVRLU Write Handle
typedef struct {
    aether_mrvlu_object_t* obj;
    void* new_data;
    void* old_data;
    bool committed;
} aether_mrvlu_write_handle_t;

// MVRLU Configuration
typedef struct {
    size_t data_size;
    uint32_t max_versions;
    bool copy_on_write;
    void (*destructor)(void*);
} aether_mrvlu_config_t;

// MVRLU Object Operations
aether_mrvlu_object_t* aether_mrvlu_create(const aether_mrvlu_config_t* config);
void aether_mrvlu_destroy(aether_mrvlu_object_t* obj);

// Read Operations
aether_mrvlu_read_handle_t* aether_mrvlu_read_begin(aether_mrvlu_object_t* obj);
void* aether_mrvlu_read_get(aether_mrvlu_read_handle_t* handle);
void aether_mrvlu_read_end(aether_mrvlu_read_handle_t* handle);

// Write Operations
aether_mrvlu_write_handle_t* aether_mrvlu_write_begin(aether_mrvlu_object_t* obj);
aether_result_t aether_mrvlu_write_set(aether_mrvlu_write_handle_t* handle, const void* data);
aether_result_t aether_mrvlu_write_commit(aether_mrvlu_write_handle_t* handle);
void aether_mrvlu_write_abort(aether_mrvlu_write_handle_t* handle);

// ============================================================================
// Lock-Free Data Structures
// ============================================================================

// Lock-Free Queue (Michael-Scott Queue)
typedef struct aether_lf_queue_node {
    _Atomic(struct aether_lf_queue_node*) next;
    void* data;
    uint64_t enqueued_seq;
} aether_lf_queue_node_t;

typedef struct {
    _Atomic(aether_lf_queue_node_t*) head;
    _Atomic(aether_lf_queue_node_t*) tail;
    _Atomic(uint64_t) count;
} aether_lf_queue_t;

// Lock-Free Stack (Treiber Stack)
typedef struct aether_lf_stack_node {
    _Atomic(struct aether_lf_stack_node*) next;
    void* data;
} aether_lf_stack_node_t;

typedef struct {
    _Atomic(aether_lf_stack_node_t*) head;
    _Atomic(uint64_t) count;
} aether_lf_stack_t;

// Lock-Free Deque (Bounded)
typedef struct {
    _Atomic(uint64_t) head;
    _Atomic(uint64_t) tail;
    void** buffer;
    size_t capacity;
    uint64_t mask;
} aether_lf_deque_t;

// Hash Map (Lazy List)
typedef struct aether_lf_hash_bucket {
    _Atomic(struct aether_lf_hash_bucket*) next;
    uint64_t key;
    void* value;
    _Atomic(bool) marked;
} aether_lf_hash_bucket_t;

typedef struct {
    aether_lf_hash_bucket_t** buckets;
    size_t num_buckets;
    _Atomic(size_t) count;
} aether_lf_hashmap_t;

// ============================================================================
// Queue Operations
// ============================================================================
aether_lf_queue_t* aether_lf_queue_create(void);
void aether_lf_queue_destroy(aether_lf_queue_t* queue);
aether_result_t aether_lf_queue_enqueue(aether_lf_queue_t* queue, void* data);
aether_result_t aether_lf_queue_dequeue(aether_lf_queue_t* queue, void** data);
bool aether_lf_queue_is_empty(aether_lf_queue_t* queue);

// ============================================================================
// Stack Operations
// ============================================================================
aether_lf_stack_t* aether_lf_stack_create(void);
void aether_lf_stack_destroy(aether_lf_stack_t* stack);
aether_result_t aether_lf_stack_push(aether_lf_stack_t* stack, void* data);
aether_result_t aether_lf_stack_pop(aether_lf_stack_t* stack, void** data);
bool aether_lf_stack_is_empty(aether_lf_stack_t* stack);

// ============================================================================
// Deque Operations
// ============================================================================
aether_lf_deque_t* aether_lf_deque_create(size_t capacity);
void aether_lf_deque_destroy(aether_lf_deque_t* deque);
aether_result_t aether_lf_deque_push_left(aether_lf_deque_t* deque, void* data);
aether_result_t aether_lf_deque_push_right(aether_lf_deque_t* deque, void* data);
aether_result_t aether_lf_deque_pop_left(aether_lf_deque_t* deque, void** data);
aether_result_t aether_lf_deque_pop_right(aether_lf_deque_t* deque, void** data);
bool aether_lf_deque_is_empty(aether_lf_deque_t* deque);

// ============================================================================
// Hash Map Operations
// ============================================================================
aether_lf_hashmap_t* aether_lf_hashmap_create(size_t capacity);
void aether_lf_hashmap_destroy(aether_lf_hashmap_t* map);
aether_result_t aether_lf_hashmap_insert(aether_lf_hashmap_t* map, uint64_t key, void* value);
aether_result_t aether_lf_hashmap_lookup(aether_lf_hashmap_t* map, uint64_t key, void** value);
aether_result_t aether_lf_hashmap_delete(aether_lf_hashmap_t* map, uint64_t key);
size_t aether_lf_hashmap_size(aether_lf_hashmap_t* map);

#ifdef __cplusplus
}
#endif

#endif // AETHER_LOCKFREE_H
