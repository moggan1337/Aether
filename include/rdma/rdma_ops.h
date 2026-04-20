/**
 * Aether - RDMA Operations
 * One-sided and two-sided RDMA operations
 *
 * Copyright (c) 2024 Aether Authors
 */

#ifndef AETHER_RDMA_OPS_H
#define AETHER_RDMA_OPS_H

#include <aether/aether_common.h>
#include <rdma/rdma_qp.h>
#include <rdma/rdma_memory.h>
#include <rdma/rdma_cq.h>
#include <infiniband/verbs.h>

#ifdef __cplusplus
extern "C" {
#endif

// Scatter/Gather Element
typedef struct ibv_sge aether_sge_t;

// Work Request
typedef struct ibv_wr aether_wr_t;

// RDMA Operation Handle (for tracking operations)
typedef struct aether_op_handle {
    uint64_t wr_id;
    uint32_t byte_len;
    aether_wc_status_t status;
    bool signaled;
    bool completed;
    pthread_cond_t cond;
    pthread_mutex_t mutex;
} aether_op_handle_t;

// RDMA Read Parameters
typedef struct {
    uint64_t remote_addr;
    uint32_t rkey;
    void* local_addr;
    uint32_t size;
    uint32_t lkey;
    uint64_t wr_id;
    int signaled;
    uint8_t opcode;
} aether_rdma_read_params_t;

// RDMA Write Parameters
typedef struct {
    uint64_t remote_addr;
    uint32_t rkey;
    void* local_addr;
    uint32_t size;
    uint32_t lkey;
    uint64_t wr_id;
    int signaled;
    int imm_enable;
    uint32_t imm_data;
} aether_rdma_write_params_t;

// Atomic Parameters
typedef struct {
    uint64_t remote_addr;
    uint32_t rkey;
    uint64_t compare_add;
    uint64_t swap;
    void* local_addr;
    uint32_t lkey;
    uint64_t wr_id;
    int signaled;
} aether_atomic_params_t;

// Send/Recv Parameters
typedef struct {
    void* local_addr;
    uint32_t size;
    uint32_t lkey;
    uint64_t wr_id;
    int signaled;
    int imm_enable;
    uint32_t imm_data;
} aether_send_params_t;

typedef struct {
    void* local_addr;
    uint32_t size;
    uint32_t lkey;
    uint64_t wr_id;
    uint32_t recv_flags;
} aether_recv_params_t;

// Batch Operation
typedef struct {
    aether_wr_t* wrs;
    aether_sge_t* sges;
    int count;
    int max_sge;
} aether_batch_op_t;

// RDMA Operations
aether_result_t aether_rdma_read(aether_qp_t* qp, const aether_rdma_read_params_t* params);
aether_result_t aether_rdma_write(aether_qp_t* qp, const aether_rdma_write_params_t* params);
aether_result_t aether_rdma_write_imm(aether_qp_t* qp, const aether_rdma_write_params_t* params,
                                       uint32_t imm_data);

aether_result_t aether_rdma_read_async(aether_qp_t* qp, const aether_rdma_read_params_t* params,
                                        aether_op_handle_t* handle);
aether_result_t aether_rdma_write_async(aether_qp_t* qp, const aether_rdma_write_params_t* params,
                                         aether_op_handle_t* handle);

// Atomic Operations
aether_result_t aether_atomic_cmp_swap(aether_qp_t* qp, const aether_atomic_params_t* params);
aether_result_t aether_atomic_fetch_add(aether_qp_t* qp, const aether_atomic_params_t* params);
aether_result_t aether_atomic_swap(aether_qp_t* qp, const aether_atomic_params_t* params);
aether_result_t aether_atomic_fetch_and(aether_qp_t* qp, const aether_atomic_params_t* params);
aether_result_t aether_atomic_fetch_or(aether_qp_t* qp, const aether_atomic_params_t* params);
aether_result_t aether_atomic_fetch_xor(aether_qp_t* qp, const aether_atomic_params_t* params);

// Send/Recv Operations
aether_result_t aether_send(aether_qp_t* qp, const aether_send_params_t* params);
aether_result_t aether_send_imm(aether_qp_t* qp, const aether_send_params_t* params,
                                 uint32_t imm_data);
aether_result_t aether_recv(aether_qp_t* qp, const aether_recv_params_t* params);

// Synchronous variants (wait for completion)
aether_result_t aether_rdma_read_sync(aether_qp_t* qp, const aether_rdma_read_params_t* params,
                                       uint32_t timeout_ms);
aether_result_t aether_rdma_write_sync(aether_qp_t* qp, const aether_rdma_write_params_t* params,
                                        uint32_t timeout_ms);
aether_result_t aether_atomic_sync(aether_qp_t* qp, const aether_atomic_params_t* params,
                                    uint32_t timeout_ms);

// Operation handle management
aether_op_handle_t* aether_create_op_handle(void);
aether_result_t aether_wait_op_handle(aether_op_handle_t* handle, uint32_t timeout_ms);
aether_result_t aether_destroy_op_handle(aether_op_handle_t* handle);
aether_result_t aether_cancel_op(aether_op_handle_t* handle);

// Batch/Multi-operation support
aether_result_t aether_post_batch(aether_qp_t* qp, aether_batch_op_t* batch);
aether_result_t aether_post_send_list(aether_qp_t* qp, aether_wr_t* wr, int count);
aether_result_t aether_post_recv_list(aether_qp_t* qp, aether_wr_t* wr, int count);

// Memory fencing
aether_result_t aether_post_send_fence(aether_qp_t* qp);
aether_result_t aether_rdma_fence(aether_qp_t* qp, uint64_t wr_id);

// Invalidate remote keys
aether_result_t aether_post_send_inv(aether_qp_t* qp, uint32_t rkey, uint64_t wr_id, int signaled);

// Local invalidate
aether_result_t aether_post_local_inv(aether_qp_t* qp, uint32_t lkey, uint64_t wr_id, int signaled);

// Memory window operations
aether_result_t aether_rdma_write_with_mw(aether_qp_t* qp, aether_mw_t* mw,
                                           const aether_rdma_write_params_t* params);
aether_result_t aether_rdma_bind_mw(aether_qp_t* qp, aether_mw_t* mw,
                                      aether_mr_t* mr, uint64_t addr,
                                      uint64_t size, uint64_t access);

// Utility functions
void aether_init_sge(aether_sge_t* sge, void* addr, uint32_t length, uint32_t lkey);
void aether_init_send_wr(aether_wr_t* wr, aether_sge_t* sges, int num_sge,
                          uint64_t wr_id, int opcode, int signaled, int send_flags);
void aether_init_recv_wr(aether_wr_t* wr, aether_sge_t* sges, int num_sge, uint64_t wr_id);

// Gather/Scatter helpers
aether_result_t aether_gather_data(void* dst, const aether_sge_t* sges, int num_sge,
                                    size_t total_len);
aether_result_t aether_scatter_data(aether_sge_t* sges, int num_sge, const void* src,
                                     size_t total_len);

#ifdef __cplusplus
}
#endif

#endif // AETHER_RDMA_OPS_H
