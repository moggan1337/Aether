/**
 * Aether - Memory-Mapped Distributed Arrays
 * Distributed arrays with RDMA-backed memory mapping
 *
 * Copyright (c) 2024 Aether Authors
 */

#ifndef AETHER_DSM_ARRAY_H
#define AETHER_DSM_ARRAY_H

#include <aether/aether_common.h>
#include <dsm/dsm.h>

#ifdef __cplusplus
extern "C" {
#endif

// Array element types
typedef enum {
    AETHER_ARRAY_TYPE_INT8 = 0,
    AETHER_ARRAY_TYPE_INT16,
    AETHER_ARRAY_TYPE_INT32,
    AETHER_ARRAY_TYPE_INT64,
    AETHER_ARRAY_TYPE_UINT8,
    AETHER_ARRAY_TYPE_UINT16,
    AETHER_ARRAY_TYPE_UINT32,
    AETHER_ARRAY_TYPE_UINT64,
    AETHER_ARRAY_TYPE_FLOAT32,
    AETHER_ARRAY_TYPE_FLOAT64,
    AETHER_ARRAY_TYPE_CFLOAT32,
    AETHER_ARRAY_TYPE_CFLOAT64,
    AETHER_ARRAY_TYPE_BOOL
} aether_array_type_t;

// Array descriptor
typedef struct aether_array aether_array_t;

// Array configuration
typedef struct {
    size_t num_elements;
    aether_array_type_t dtype;
    uint32_t access_mode;
    uint8_t owner;
    uint8_t partitioning_scheme;    // 0 = block, 1 = cyclic, 2 = hybrid
    uint8_t replication_factor;
    uint8_t : 8;
} aether_array_config_t;

// Partition descriptor
typedef struct {
    uint16_t node_id;
    uint64_t local_offset;          // Offset in local memory
    uint64_t num_elements;
    uint64_t gaddr;                 // Global address in DSM
    uint32_t lkey;
    uint32_t rkey;
} aether_partition_t;

// Array slicing
typedef struct {
    uint64_t start;
    uint64_t stop;
    uint64_t step;
} aether_array_slice_t;

// Array iterator
typedef struct aether_array_iter {
    aether_array_t* array;
    uint64_t current;
    uint64_t end;
    uint64_t step;
} aether_array_iter_t;

// Array operations
aether_array_t* aether_array_create(const aether_array_config_t* config);
aether_result_t aether_array_destroy(aether_array_t* arr);

// Element access
void* aether_array_get(aether_array_t* arr, uint64_t index);
void* aether_array_get_local(aether_array_t* arr, uint64_t index);
aether_result_t aether_array_set(aether_array_t* arr, uint64_t index, const void* value);

// Bulk operations
aether_result_t aether_array_copy_from(aether_array_t* arr, uint64_t offset,
                                        const void* src, size_t count);
aether_result_t aether_array_copy_to(aether_array_t* arr, uint64_t offset,
                                      void* dst, size_t count);
aether_result_t aether_array_fill(aether_array_t* arr, const void* value);

// Slicing
aether_array_t* aether_array_slice(aether_array_t* arr, const aether_array_slice_t* slice);
aether_array_t* aether_array_view(aether_array_t* arr);

// Element-wise operations
aether_result_t aether_array_add_scalar(aether_array_t* arr, const void* scalar);
aether_result_t aether_array_mul_scalar(aether_array_t* arr, const void* scalar);
aether_result_t aether_array_add_array(aether_array_t* dst, const aether_array_t* src);
aether_result_t aether_array_mul_array(aether_array_t* dst, const aether_array_t* src);

// Reduction operations
aether_result_t aether_array_reduce_sum(aether_array_t* arr, void* result);
aether_result_t aether_array_reduce_prod(aether_array_t* arr, void* result);
aether_result_t aether_array_reduce_min(aether_array_t* arr, void* result);
aether_result_t aether_array_reduce_max(aether_array_t* arr, void* result);

// BLAS-like operations
aether_result_t aether_array_dot(const aether_array_t* a, const aether_array_t* b, void* result);
aether_result_t aether_array_gemv(aether_array_t* A, const aether_array_t* x,
                                   aether_array_t* y, float alpha, float beta);
aether_result_t aether_array_gemm(aether_array_t* A, aether_array_t* B,
                                   aether_array_t* C, float alpha, float beta);

// Array info
size_t aether_array_element_size(aether_array_type_t dtype);
size_t aether_array_total_size(const aether_array_t* arr);
uint64_t aether_array_num_elements(const aether_array_t* arr);
aether_array_type_t aether_array_dtype(const aether_array_t* arr);

// Partitioning
aether_partition_t* aether_array_get_partition(aether_array_t* arr, uint16_t node_id);
uint16_t aether_array_owner(aether_array_t* arr, uint64_t index);
aether_partition_t** aether_array_get_all_partitions(aether_array_t* arr, size_t* count);

// Iterator
aether_array_iter_t* aether_array_iter_create(aether_array_t* arr);
void aether_array_iter_destroy(aether_array_iter_t* iter);
bool aether_array_iter_next(aether_array_iter_t* iter, void** value);
void* aether_array_iter_value(aether_array_iter_t* iter);

// RDMA access
aether_result_t aether_array_rdma_put(aether_array_t* arr, uint64_t index,
                                       const void* local_buf, uint16_t target_node);
aether_result_t aether_array_rdma_get(aether_array_t* arr, uint64_t index,
                                       void* local_buf, uint16_t source_node);
aether_result_t aether_array_rdma_put_bulk(aether_array_t* arr, uint64_t offset,
                                            const void* local_buf, size_t count,
                                            uint16_t target_node);
aether_result_t aether_array_rdma_get_bulk(aether_array_t* arr, uint64_t offset,
                                            void* local_buf, size_t count,
                                            uint16_t source_node);

// Collective operations
aether_result_t aether_array_broadcast(aether_array_t* arr, uint64_t root);
aether_result_t aether_array_allreduce(aether_array_t* arr, int op);
aether_result_t aether_array_allgather(aether_array_t* arr);

// Reshaping
aether_result_t aether_array_reshape(aether_array_t* arr, size_t* new_shape, int ndim);

// Strides
int64_t* aether_array_strides(const aether_array_t* arr);
int64_t* aether_array_cstrides(const aether_array_t* arr);

// Contiguity check
bool aether_array_is_contiguous(const aether_array_t* arr);
aether_result_t aether_array_make_contiguous(aether_array_t* arr);

#ifdef __cplusplus
}
#endif

#endif // AETHER_DSM_ARRAY_H
