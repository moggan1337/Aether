/**
 * Aether - RDMA Queue Pair Management
 * QP creation, state transitions, and connection establishment
 *
 * Copyright (c) 2024 Aether Authors
 */

#ifndef AETHER_RDMA_QP_H
#define AETHER_RDMA_QP_H

#include <aether/aether_common.h>
#include <rdma/rdma_device.h>
#include <rdma/rdma_memory.h>
#include <infiniband/verbs.h>

#ifdef __cplusplus
extern "C" {
#endif

// QP Types
typedef enum {
    AETHER_QP_TYPE_RC = IBV_QPT_RC,      // Reliable Connected
    AETHER_QP_TYPE_UC = IBV_QPT_UC,      // Unreliable Connected
    AETHER_QP_TYPE_UD = IBV_QPT_UD,      // Unreliable Datagram
    AETHER_QP_TYPE_RAW_PACKET = IBV_QPT_RAW_PACKET,
    AETHER_QP_TYPE_XRC = IBV_QPT_XRC     // Extended Reliable Connected
} aether_qp_type_t;

// QP State
typedef enum {
    AETHER_QP_STATE_RESET = 0,
    AETHER_QP_STATE_INIT = 1,
    AETHER_QP_STATE_RTR = 2,   // Ready to Receive
    AETHER_QP_STATE_RTS = 3,   // Ready to Send
    AETHER_QP_STATE_SQE = 4,   // Send Queue Empty
    AETHER_QP_STATE_ERR = 5,
    AETHER_QP_STATE_SUD = 6,
    AETHER_QP_STATE_BAD = 7
} aether_qp_state_t;

// Queue Pair
typedef struct aether_qp {
    struct ibv_qp* ibv_qp;
    aether_qp_type_t type;
    aether_qp_state_t state;
    
    // Queue configuration
    uint32_t qp_num;
    uint32_t max_send_wr;
    uint32_t max_recv_wr;
    uint32_t max_send_sge;
    uint32_t max_recv_sge;
    uint32_t max_inline_data;
    
    // Completion queues
    struct ibv_cq* send_cq;
    struct ibv_cq* recv_cq;
    uint32_t cq_size;
    
    // Protection domain
    aether_pd_t* pd;
    
    // Connection info
    uint16_t remote_lid;
    uint32_t remote_qpn;
    uint32_t remote_psn;
    union ibv_gid remote_gid;
    
    // Local info
    uint16_t local_lid;
    union ibv_gid local_gid;
    
    // Statistics
    uint64_t send_count;
    uint64_t recv_count;
    uint64_t send_bytes;
    uint64_t recv_bytes;
    
    pthread_mutex_t lock;
} aether_qp_t;

// QP Initialization Parameters
typedef struct {
    aether_qp_type_t type;
    uint32_t max_send_wr;
    uint32_t max_recv_wr;
    uint32_t max_send_sge;
    uint32_t max_recv_sge;
    uint32_t max_inline_data;
    uint32_t cq_size;
    uint8_t sq_sig_all;
    uint8_t rv : 1;
    uint8_t : 7;
} aether_qp_init_attr_t;

// QP Modification Attributes
typedef struct {
    uint32_t qp_state;
    uint32_t cur_qp_state;
    uint32_t en_sqd_async_notify;
    uint32_t sqd_async_stall_drain_atomic;
    uint32_t qp_access_flags;
    uint16_t pkey_index;
    uint16_t port_num;
    uint8_t sq_draining;
    uint8_t max_rd_atomic;
    uint8_t max_dest_rd_atomic;
    uint8_t min_rnr_timer;
    uint32_t timeout;
    uint32_t retry_cnt;
    uint32_t rnr_retry;
    uint32_t alt_timeout;
    uint16_t alt_port_num;
    uint16_t alt_pkey_index;
    uint8_t alt_timeout;
    uint8_t : 7;
} aether_qp_attr_t;

// CM Connection Request
typedef struct {
    uint32_t qp_num;
    uint32_t qkey;
    uint16_t pkey;
    uint8_t port_num;
    uint8_t sl;
} aether_cm_req_t;

// CM Connection Response
typedef struct {
    uint32_t qp_num;
    uint32_t qkey;
    uint32_t rq_psn;
    uint8_t port_num;
    uint8_t sl;
    uint8_t initiator_depth;
    uint8_t responder_resources;
} aether_cm_resp_t;

// Queue Pair Operations
aether_qp_t* aether_create_qp(aether_pd_t* pd, const aether_qp_init_attr_t* attr);
aether_result_t aether_destroy_qp(aether_qp_t* qp);

// QP State Transitions
aether_result_t aether_qp_init(aether_qp_t* qp, uint16_t port_num, uint16_t pkey_index);
aether_result_t aether_qp_rtr(aether_qp_t* qp, const aether_addr_t* remote);
aether_result_t aether_qp_rts(aether_qp_t* qp);
aether_result_t aether_qp_reset(aether_qp_t* qp);
aether_result_t aether_qp_modify(aether_qp_t* qp, const aether_qp_attr_t* attr);

// Connection Establishment (using CM)
aether_result_t aether_qp_connect(aether_qp_t* qp, const aether_addr_t* remote);
aether_result_t aether_qp_disconnect(aether_qp_t* qp);

// UD QP operations
aether_result_t aether_qp_set_ud_addr(aether_qp_t* qp, uint16_t dlid, 
                                       uint32_t dqpn, uint32_t qkey);

// QP Query
aether_result_t aether_qp_query(aether_qp_t* qp, struct ibv_qp_attr* attr, 
                                 int attr_mask);

// Error handling
aether_result_t aether_qp_to_err(aether_qp_t* qp);
aether_result_t aether_qp_get_state(aether_qp_t* qp, aether_qp_state_t* state);

// Multicast operations
aether_result_t aether_qp_attach_mcast(aether_qp_t* qp, union ibv_gid* gid, uint16_t lid);
aether_result_t aether_qp_detach_mcast(aether_qp_t* qp, union ibv_gid* gid, uint16_t lid);

#ifdef __cplusplus
}
#endif

#endif // AETHER_RDMA_QP_H
