/**
 * Aether - RDMA Completion Queue Management
 * CQ polling, async events, and completion handling
 *
 * Copyright (c) 2024 Aether Authors
 */

#ifndef AETHER_RDMA_CQ_H
#define AETHER_RDMA_CQ_H

#include <aether/aether_common.h>
#include <rdma/rdma_device.h>
#include <infiniband/verbs.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

// Work Completion Status
typedef enum {
    AETHER_WC_SUCCESS = IBV_WC_SUCCESS,
    AETHER_WC_LOC_LEN_ERR = IBV_WC_LOC_LEN_ERR,
    AETHER_WC_LOC_QP_OP_ERR = IBV_WC_LOC_QP_OP_ERR,
    AETHER_WC_LOC_EEC_OP_ERR = IBV_WC_LOC_EEC_OP_ERR,
    AETHER_WC_LOC_PROT_ERR = IBV_WC_LOC_PROT_ERR,
    AETHER_WC_WR_FLUSH_ERR = IBV_WC_WR_FLUSH_ERR,
    AETHER_WC_MW_BIND_ERR = IBV_WC_MW_BIND_ERR,
    AETHER_WC_BAD_RESP_ERR = IBV_WC_BAD_RESP_ERR,
    AETHER_WC_LOC_ACCESS_ERR = IBV_WC_LOC_ACCESS_ERR,
    AETHER_WC_REM_INV_REQ_ERR = IBV_WC_REM_INV_REQ_ERR,
    AETHER_WC_REM_ACCESS_ERR = IBV_WC_REM_ACCESS_ERR,
    AETHER_WC_REM_OP_ERR = IBV_WC_REM_OP_ERR,
    AETHER_WC_RETRY_EXC_ERR = IBV_WC_RETRY_EXC_ERR,
    AETHER_WC_RNR_RETRY_EXC_ERR = IBV_WC_RNR_RETRY_EXC_ERR,
    AETHER_WC_LOC_RDD_VIOL_ERR = IBV_WC_LOC_RDD_VIOL_ERR,
    AETHER_WC_REM_INV_RD_REQ_ERR = IBV_WC_REM_INV_RD_REQ_ERR,
    AETHER_WC_REM_ABORT_ERR = IBV_WC_REM_ABORT_ERR,
    AETHER_WC_INV_REQ_ERR = IBV_WC_INV_REQ_ERR,
    AETHER_WC_CONN_FORM_ERR = IBV_WC_CONN_FORM_ERR,
    AETHER_WC_QP_OPERATION_ERR = IBV_WC_QP_OPERATION_ERR,
    AETHER_WC_RDMA_PSN_SEQ_ERR = IBV_WC_RDMA_PSN_SEQ_ERR,
    AETHER_WC_RDMA_OPERATION_ERR = IBV_WC_RDMA_OPERATION_ERR,
    AETHER_WC_CANCELED_ERR = IBV_WC_CANCELED_ERR,
    AETHER_WC_DUP_REQ_ERR = IBV_WC_DUP_REQ_ERR,
    AETHER_WC_JAMMING_ERR = IBV_WC_JAMMING_ERR,
    AETHER_WC_RESP_TIMEOUT_ERR = IBV_WC_RESP_TIMEOUT_ERR,
    AETHER_WC_GENERAL_ERR = IBV_WC_GENERAL_ERR
} aether_wc_status_t;

// Work Completion OpCode
typedef enum {
    AETHER_WC_SEND = IBV_WC_SEND,
    AETHER_WC_RDMA_READ = IBV_WC_RDMA_READ,
    AETHER_WC_RDMA_WRITE = IBV_WC_RDMA_WRITE,
    AETHER_WC_COMP_SWAP = IBV_WC_COMP_SWAP,
    AETHER_WC_FETCH_ADD = IBV_WC_FETCH_ADD,
    AETHER_WC_LOCAL_INV = IBV_WC_LOCAL_INV,
    AETHER_WC_BIND_MW = IBV_WC_BIND_MW,
    AETHER_WC_RECV = IBV_WC_RECV,
    AETHER_WC_RECV_RDMA_WITH_IMM = IBV_WC_RECV_RDMA_WITH_IMM
} aether_wc_opcode_t;

// Work Request ID types
#define AETHER_WQE_ID_TYPE_MASK 0xFF00000000000000ULL
#define AETHER_WQE_ID_DATA_MASK 0x00FFFFFFFFFFFFFFULL

typedef enum {
    AETHER_WQE_TYPE_RDMA_READ = 0x01,
    AETHER_WQE_TYPE_RDMA_WRITE = 0x02,
    AETHER_WQE_TYPE_SEND = 0x03,
    AETHER_WQE_TYPE_RECV = 0x04,
    AETHER_WQE_TYPE_ATOMIC = 0x05,
    AETHER_WQE_TYPE_BARRIER = 0x06,
    AETHER_WQE_TYPE_RPC = 0x07,
    AETHER_WQE_TYPE_DSM = 0x08
} aether_wqe_type_t;

// Extended Work Completion
typedef struct {
    uint64_t wr_id;
    aether_wc_status_t status;
    aether_wc_opcode_t opcode;
    uint32_t vendor_err;
    uint32_t byte_len;
    uint32_t imm_data;
    uint32_t qp_num;
    uint32_t src_qp;
    uint8_t wc_flags;
    uint16_t pkey_index;
    uint8_t slid;
    uint8_t sl;
    uint16_t dlid_path_bits;
    
    // Extended fields
    uint64_t timestamp;
    uint16_t src_node;
    uint8_t priority;
    uint8_t : 8;
} aether_wc_t;

// Completion Queue
typedef struct aether_cq {
    struct ibv_cq* ibv_cq;
    uint32_t cq_num;
    uint32_t cqe;
    uint32_t comp_mask;
    
    // Event channel for async notifications
    struct ibv_comp_channel* channel;
    int event_fd;
    
    // Polling state
    pthread_t poll_thread;
    bool poll_thread_running;
    bool async_events_enabled;
    
    // Statistics
    uint64_t total_completions;
    uint64_t successful_completions;
    uint64_t failed_completions;
    
    pthread_mutex_t lock;
} aether_cq_t;

// CQ Callback
typedef void (*aether_cq_callback_t)(aether_wc_t* wc, void* arg);

// Completion Queue Operations
aether_cq_t* aether_create_cq(aether_device_t* dev, uint32_t cqe, 
                              void* cq_context, int comp_vector);
aether_result_t aether_destroy_cq(aether_cq_t* cq);

// CQ Polling
aether_result_t aether_poll_cq(aether_cq_t* cq, int num_entries, aether_wc_t* wcs);
aether_result_t aether_poll_cq_single(aether_cq_t* cq, aether_wc_t* wc);
int aether_peek_cq(aether_cq_t* cq);

// CQ Notification
aether_result_t aether_arm_cq(aether_cq_t* cq, int solicited_only);
aether_result_t aether_req_notify_cq(aether_cq_t* cq, int solicited_only);

// Async Event Handling
aether_result_t aether_cq_enable_async(aether_cq_t* cq);
void* aether_cq_event_dispatch(void* arg);

// Callback registration
aether_result_t aether_cq_set_callback(aether_cq_t* cq, aether_cq_callback_t cb, void* arg);

// Request/Response ID helpers
static inline uint64_t aether_make_wqe_id(aether_wqe_type_t type, uint64_t data) {
    return ((uint64_t)type << 56) | (data & AETHER_WQE_ID_DATA_MASK);
}

static inline aether_wqe_type_t aether_wqe_id_type(uint64_t id) {
    return (aether_wqe_type_t)((id >> 56) & 0xFF);
}

static inline uint64_t aether_wqe_id_data(uint64_t id) {
    return id & AETHER_WQE_ID_DATA_MASK;
}

// Status string helpers
const char* aether_wc_status_str(aether_wc_status_t status);
const char* aether_wc_opcode_str(aether_wc_opcode_t opcode);

#ifdef __cplusplus
}
#endif

#endif // AETHER_RDMA_CQ_H
