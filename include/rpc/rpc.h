/**
 * Aether - RDMA-Aware RPC Framework
 * High-performance RPC with RDMA transport
 *
 * Copyright (c) 2024 Aether Authors
 */

#ifndef AETHER_RPC_H
#define AETHER_RPC_H

#include <aether/aether_common.h>
#include <rdma/rdma_cm.h>
#include <structures/lockfree.h>

#ifdef __cplusplus
extern "C" {
#endif

// RPC Opcodes
typedef enum {
    // DSM operations
    AETHER_RPC_DSM_READ = 0x1000,
    AETHER_RPC_DSM_WRITE,
    AETHER_RPC_DSM_ATOMIC_FETCH_ADD,
    AETHER_RPC_DSM_ATOMIC_CMP_SWAP,
    AETHER_RPC_DSM_PAGE_FAULT,
    AETHER_RPC_DSM_PAGE_GRANT,
    AETHER_RPC_DSM_PAGE_REVOKE,
    AETHER_RPC_DSM_INVALIDATE,
    AETHER_RPC_DSM_SYNC,
    
    // Memory operations
    AETHER_RPC_MEM_REGISTER = 0x2000,
    AETHER_RPC_MEM_UNREGISTER,
    AETHER_RPC_MEM_PREFETCH,
    AETHER_RPC_MEM_FLUSH,
    
    // Synchronization
    AETHER_RPC_SYNC_BARRIER = 0x3000,
    AETHER_RPC_SYNC_FENCE,
    AETHER_RPC_SYNC_BROADCAST,
    AETHER_RPC_SYNC_REDUCE,
    AETHER_RPC_SYNC_ALLGATHER,
    
    // Connection management
    AETHER_RPC_CONN_HANDSHAKE = 0x4000,
    AETHER_RPC_CONN_KEEPALIVE,
    AETHER_RPC_CONN_DISCONNECT,
    
    // Control operations
    AETHER_RPC_CTRL_STATS = 0x5000,
    AETHER_RPC_CTRL_HEALTH_CHECK,
    AETHER_RPC_CTRL_RECONFIGURE,
    
    // Custom range start
    AETHER_RPC_CUSTOM_START = 0x8000
} aether_rpc_opcode_t;

// RPC Flags
#define AETHER_RPC_FLAG_RESPONSE  0x01
#define AETHER_RPC_FLAG_ONEWAY    0x02
#define AETHER_RPC_FLAG_PRIORITY  0x04
#define AETHER_RPC_FLAG_BATCH     0x08
#define AETHER_RPC_FLAG_RDMA      0x10
#define AETHER_RPC_FLAG_SIGNALED  0x20

// RPC Status
typedef enum {
    AETHER_RPC_OK = 0,
    AETHER_RPC_ERR_TIMEOUT = -1,
    AETHER_RPC_ERR_FAILED = -2,
    AETHER_RPC_ERR_NO_ROUTE = -3,
    AETHER_RPC_ERR_QUEUE_FULL = -4,
    AETHER_RPC_ERR_PROTOCOL = -5,
    AETHER_RPC_ERR_CANCELED = -6,
    AETHER_RPC_ERR_CONNECTION = -7
} aether_rpc_status_t;

// RPC Message
typedef struct {
    aether_rpc_header_t header;
    uint8_t payload[];
} aether_rpc_msg_t;

// RPC Request/Response Buffer
typedef struct {
    uint32_t request_id;
    aether_rpc_opcode_t opcode;
    uint16_t target_node;
    uint16_t source_node;
    uint64_t timestamp;
    uint64_t deadline;
    uint8_t flags;
    uint8_t status;
    uint16_t retry_count;
    
    // Buffers
    void* request_payload;
    size_t request_size;
    void* response_payload;
    size_t response_size;
    
    // Completion handle
    aether_op_handle_t* op_handle;
    pthread_cond_t cond;
    pthread_mutex_t mutex;
    bool completed;
    
    // Callbacks
    void (*callback)(void*);
    void* callback_arg;
} aether_rpc_ctx_t;

// RPC Handler
typedef aether_result_t (*aether_rpc_handler_t)(aether_rpc_ctx_t* ctx, const void* request, void* response);

// RPC Server
typedef struct aether_rpc_server aether_rpc_server_t;

// RPC Client
typedef struct aether_rpc_client aether_rpc_client_t;

// RPC Configuration
typedef struct {
    uint32_t max_pending_requests;
    uint32_t max_inflight_requests;
    uint32_t request_timeout_ms;
    uint32_t response_buffer_size;
    uint32_t max_batch_size;
    uint32_t retry_attempts;
    uint32_t keepalive_interval_ms;
    bool enable_rdma_transport;
    bool enable_backpressure;
} aether_rpc_config_t;

// Batch RPC
typedef struct {
    aether_rpc_ctx_t** requests;
    size_t count;
    size_t max_count;
} aether_rpc_batch_t;

// RPC Server Operations
aether_rpc_server_t* aether_rpc_server_create(aether_cm_t* cm, const aether_rpc_config_t* config);
aether_result_t aether_rpc_server_start(aether_rpc_server_t* server);
aether_result_t aether_rpc_server_stop(aether_rpc_server_t* server);
aether_result_t aether_rpc_server_destroy(aether_rpc_server_t* server);

// RPC Handler Registration
aether_result_t aether_rpc_register_handler(aether_rpc_server_t* server, 
                                              aether_rpc_opcode_t opcode,
                                              aether_rpc_handler_t handler);
aether_result_t aether_rpc_unregister_handler(aether_rpc_server_t* server,
                                               aether_rpc_opcode_t opcode);

// RPC Client Operations
aether_rpc_client_t* aether_rpc_client_create(aether_cm_t* cm, const aether_rpc_config_t* config);
aether_result_t aether_rpc_client_connect(aether_rpc_client_t* client, uint16_t node_id);
aether_result_t aether_rpc_client_disconnect(aether_rpc_client_t* client, uint16_t node_id);
aether_result_t aether_rpc_client_destroy(aether_rpc_client_t* client);

// RPC Call Operations
aether_result_t aether_rpc_call(aether_rpc_client_t* client, uint16_t target_node,
                                  aether_rpc_opcode_t opcode,
                                  const void* request, size_t request_size,
                                  void* response, size_t* response_size,
                                  uint32_t timeout_ms);

aether_rpc_ctx_t* aether_rpc_call_async(aether_rpc_client_t* client, uint16_t target_node,
                                          aether_rpc_opcode_t opcode,
                                          const void* request, size_t request_size,
                                          void (*callback)(aether_rpc_ctx_t*, void*),
                                          void* callback_arg);

aether_result_t aether_rpc_wait(aether_rpc_ctx_t* ctx, uint32_t timeout_ms);
aether_result_t aether_rpc_cancel(aether_rpc_ctx_t* ctx);

// RDMA-optimized RPC
aether_result_t aether_rpc_rdma_call(aether_rpc_client_t* client, uint16_t target_node,
                                       aether_rpc_opcode_t opcode,
                                       uint64_t local_addr, uint32_t lkey,
                                       uint64_t remote_addr, uint32_t rkey,
                                       size_t size, uint32_t timeout_ms);

// Batch Operations
aether_result_t aether_rpc_batch_init(aether_rpc_batch_t* batch, size_t max_count);
void aether_rpc_batch_destroy(aether_rpc_batch_t* batch);
aether_result_t aether_rpc_batch_add(aether_rpc_batch_t* batch, aether_rpc_ctx_t* ctx);
aether_result_t aether_rpc_batch_execute(aether_rpc_client_t* client, aether_rpc_batch_t* batch);

// Collective RPC
aether_result_t aether_rpc_barrier(aether_rpc_client_t** clients, size_t num_nodes);
aether_result_t aether_rpc_broadcast(aether_rpc_client_t* client,
                                      aether_rpc_opcode_t opcode,
                                      const void* data, size_t size);
aether_result_t aether_rpc_reduce(aether_rpc_client_t* client,
                                   aether_rpc_opcode_t opcode,
                                   void* inout, size_t size, int reduce_op);

// Utility
uint32_t aether_rpc_new_request_id(void);
const char* aether_rpc_opcode_str(aether_rpc_opcode_t opcode);
aether_rpc_status_t aether_rpc_status_from_errno(int err);

#ifdef __cplusplus
}
#endif

#endif // AETHER_RPC_H
