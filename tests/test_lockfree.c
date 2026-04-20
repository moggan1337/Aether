/**
 * Aether - Lock-Free Data Structures Test
 */

#include <structures/lockfree.h>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>
#include <unistd.h>

#define NUM_THREADS 4
#define NUM_OPS 10000

// Test lock-free queue
void test_lf_queue(void) {
    printf("Testing Lock-Free Queue...\n");
    
    aether_lf_queue_t* queue = aether_lf_queue_create();
    assert(queue != NULL);
    
    // Enqueue items
    for (int i = 0; i < 100; i++) {
        int* data = malloc(sizeof(int));
        *data = i;
        aether_result_t ret = aether_lf_queue_enqueue(queue, data);
        assert(ret == AETHER_SUCCESS);
    }
    
    // Dequeue and verify
    for (int i = 0; i < 100; i++) {
        void* data;
        aether_result_t ret = aether_lf_queue_dequeue(queue, &data);
        assert(ret == AETHER_SUCCESS);
        assert(*(int*)data == i);
        free(data);
    }
    
    assert(aether_lf_queue_is_empty(queue));
    
    aether_lf_queue_destroy(queue);
    printf("  ✓ Queue test passed\n");
}

// Test lock-free stack
void test_lf_stack(void) {
    printf("Testing Lock-Free Stack...\n");
    
    aether_lf_stack_t* stack = aether_lf_stack_create();
    assert(stack != NULL);
    
    // Push items
    for (int i = 0; i < 100; i++) {
        int* data = malloc(sizeof(int));
        *data = i;
        aether_result_t ret = aether_lf_stack_push(stack, data);
        assert(ret == AETHER_SUCCESS);
    }
    
    // Pop and verify (LIFO order)
    for (int i = 99; i >= 0; i--) {
        void* data;
        aether_result_t ret = aether_lf_stack_pop(stack, &data);
        assert(ret == AETHER_SUCCESS);
        assert(*(int*)data == i);
        free(data);
    }
    
    assert(aether_lf_stack_is_empty(stack));
    
    aether_lf_stack_destroy(stack);
    printf("  ✓ Stack test passed\n");
}

// Test lock-free deque
void test_lf_deque(void) {
    printf("Testing Lock-Free Deque...\n");
    
    aether_lf_deque_t* deque = aether_lf_deque_create(1024);
    assert(deque != NULL);
    
    // Push left
    for (int i = 0; i < 50; i++) {
        aether_result_t ret = aether_lf_deque_push_left(deque, (void*)(intptr_t)i);
        assert(ret == AETHER_SUCCESS);
    }
    
    // Push right
    for (int i = 0; i < 50; i++) {
        aether_result_t ret = aether_lf_deque_push_right(deque, (void*)(intptr_t)(i + 1000));
        assert(ret == AETHER_SUCCESS);
    }
    
    // Pop from both ends
    for (int i = 49; i >= 0; i--) {
        void* data;
        aether_result_t ret = aether_lf_deque_pop_left(deque, &data);
        assert(ret == AETHER_SUCCESS);
        assert((intptr_t)data == i);
    }
    
    for (int i = 0; i < 50; i++) {
        void* data;
        aether_result_t ret = aether_lf_deque_pop_right(deque, &data);
        assert(ret == AETHER_SUCCESS);
        assert((intptr_t)data == i + 1000);
    }
    
    assert(aether_lf_deque_is_empty(deque));
    
    aether_lf_deque_destroy(deque);
    printf("  ✓ Deque test passed\n");
}

// Test lock-free hash map
void test_lf_hashmap(void) {
    printf("Testing Lock-Free Hash Map...\n");
    
    aether_lf_hashmap_t* map = aether_lf_hashmap_create(64);
    assert(map != NULL);
    
    // Insert items
    for (uint64_t i = 0; i < 1000; i++) {
        aether_result_t ret = aether_lf_hashmap_insert(map, i, (void*)(i * 2));
        assert(ret == AETHER_SUCCESS);
    }
    
    // Lookup
    for (uint64_t i = 0; i < 1000; i++) {
        void* value;
        aether_result_t ret = aether_lf_hashmap_lookup(map, i, &value);
        assert(ret == AETHER_SUCCESS);
        assert((uint64_t)value == i * 2);
    }
    
    // Size check
    assert(aether_lf_hashmap_size(map) == 1000);
    
    // Delete
    for (uint64_t i = 0; i < 500; i++) {
        aether_result_t ret = aether_lf_hashmap_delete(map, i);
        assert(ret == AETHER_SUCCESS);
    }
    
    assert(aether_lf_hashmap_size(map) == 500);
    
    aether_lf_hashmap_destroy(map);
    printf("  ✓ Hash map test passed\n");
}

// Test RCU
void test_rcu(void) {
    printf("Testing RCU...\n");
    
    aether_rcu_config_t config = {
        .num_domains = 1,
        .callback_batch_size = 32
    };
    
    aether_rcu_domain_t* domain = aether_rcu_domain_create(&config);
    assert(domain != NULL);
    
    aether_rcu_ptr_t ptr;
    atomic_store_explicit(&ptr.ptr, NULL, memory_order_relaxed);
    atomic_store_explicit(&ptr.version, 0, memory_order_relaxed);
    
    // Allocate
    int* data = malloc(sizeof(int));
    *data = 42;
    
    aether_rcu_reader_t reader;
    aether_rcu_reader_lock(&reader, domain);
    
    aether_rcu_assign_pointer(&ptr, data, domain);
    
    void* read_val = aether_rcu_dereference(&ptr, domain);
    assert(*(int*)read_val == 42);
    
    aether_rcu_reader_unlock(&reader);
    
    // Synchronize before free
    aether_rcu_synchronize(domain);
    
    free(data);
    
    aether_rcu_domain_destroy(domain);
    printf("  ✓ RCU test passed\n");
}

// Test MVRLU
void test_mrvlu(void) {
    printf("Testing MVRLU...\n");
    
    aether_mrvlu_config_t config = {
        .data_size = sizeof(int),
        .max_versions = 8
    };
    
    aether_mrvlu_object_t* obj = aether_mrvlu_create(&config);
    assert(obj != NULL);
    
    // Initial write
    int initial_val = 100;
    aether_mrvlu_write_handle_t* wh = aether_mrvlu_write_begin(obj);
    assert(wh != NULL);
    aether_mrvlu_write_set(wh, &initial_val);
    aether_mrvlu_write_commit(wh);
    
    // Concurrent read
    aether_mrvlu_read_handle_t* rh = aether_mrvlu_read_begin(obj);
    assert(rh != NULL);
    int* read_val = (int*)aether_mrvlu_read_get(rh);
    assert(*read_val == 100);
    aether_mrvlu_read_end(rh);
    
    // Another write
    int new_val = 200;
    wh = aether_mrvlu_write_begin(obj);
    aether_mrvlu_write_set(wh, &new_val);
    aether_mrvlu_write_commit(wh);
    
    // Read new value
    rh = aether_mrvlu_read_begin(obj);
    read_val = (int*)aether_mrvlu_read_get(rh);
    assert(*read_val == 200);
    aether_mrvlu_read_end(rh);
    
    aether_mrvlu_destroy(obj);
    printf("  ✓ MVRLU test passed\n");
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    
    printf("=== Aether Lock-Free Data Structures Tests ===\n\n");
    
    test_lf_queue();
    test_lf_stack();
    test_lf_deque();
    test_lf_hashmap();
    test_rcu();
    test_mrvlu();
    
    printf("\n✓ All tests passed!\n");
    
    return 0;
}
