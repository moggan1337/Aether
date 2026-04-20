/**
 * Aether - RDMA-Aware RPC Framework Implementation
 * High-performance RPC with RDMA transport
 *
 * Copyright (c) 2024 Aether Authors
 */

#include <rpc/rpc.h>
#include <structures/lockfree.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

// Request ID counter
static _Atomic uint32_t g_request_id = 0;

// RPC Server
struct aether_rpc_server {
    aether_cm_t* cm;
    aether_rpc_config_t config;
    
    // Registered handlers
    struct {
        aether_rpc_handler_t handler;
        void* arg;
    } handlers[256];
    
    // Request queue
    aether_lf_queue_t* request_queue;
    
    // Worker threads
    pthread_t* workers;
    size_t num_workers;
    bool running;
    
    pthread_mutex_t lock;
};

// RPC Client
struct aether_rpc_client {
    aether_cm_t* cm;
    aether_rpc_config_t config;
    
    // Connections to other nodes
    struct {
        uint16_t node_id;
        aether_qp_t* qp;
        aether_conn_t* conn;
        bool connected;
    }* connections;
    size_t max_nodes;
    
    // Pending requests
    aether_lf_hashmap_t* pending_requests;
    
    // Completion CQ
    aether_cq_t* cq;
    
    pthread_mutex_t lock;
    pthread_t dispatch_thread;
    bool running;
};

// RPC Server Implementation
aether_rpc_server_t* aether_rpc_server_create(aether_cm_t* cm, const aether_rpc_config_t* config) {
    if (!cm) {
        return NULL;
    }
    
    aether_rpc_server_t* server = (aether_rpc_server_t*)calloc(1, sizeof(aether_rpc_server_t));
    if (!server) {
        return NULL;
    }
    
    server->cm = cm;
    if (config) {
        server->config = *config;
    } else {
        // Default config
        server->config.max_pending_requests = 1024;
        server->config.max_inflight_requests = 256;
        server->config.request_timeout_ms = 5000;
        server->config.response_buffer_size = 8192;
        server->config.retry_attempts = 3;
    }
    
    pthread_mutex_init(&server->lock, NULL);
    
    server->request_queue = aether_lf_queue_create();
    if (!server->request_queue) {
        pthread_mutex_destroy(&server->lock);
        free(server);
        return NULL;
    }
    
    return server;
}

aether_result_t aether_rpc_server_start(aether_rpc_server_t* server) {
    if (!server) {
        return AETHER_ERR_INVALID_PARAM;
    }
    
    server->running = true;
    server->num_workers = 4;
    server->workers = (pthread_t*)calloc(server->num_workers, sizeof(pthread_t));
    
    if (!server->workers) {
        return AETHER_ERR_OOM;
    }
    
    // Start worker threads
    for (size_t i = 0; i < server->num_workers; i++) {
        // In a real implementation, would create actual worker threads
    }
    
    return AETHER_SUCCESS;
}

aether_result_t aether_rpc_server_stop(aether_rpc_server_t* server) {
    if (!server) {
        return AETHER_ERR_INVALID_PARAM;
    }
    
    server->running = false;
    
    // Join worker threads
    for (size_t i = 0; i < server->num_workers; i++) {
        if (server->workers && server->workers[i]) {
            pthread_join(server->workers[i], NULL);
        }
    }
    
    free(server->workers);
    server->workers = NULL;
    
    return AETHER_SUCCESS;
}

aether_result_t aether_rpc_server_destroy(aether_rpc_server_t* server) {
    if (!server) {
        return AETHER_ERR_INVALID_PARAM;
    }
    
    aether_rpc_server_stop(server);
    pthread_mutex_destroy(&server->lock);
    aether_lf_queue_destroy(server->request_queue);
    free(server);
    
    return AETHER_SUCCESS;
}

aether_result_t aether_rpc_register_handler(aether_rpc_server_t* server,
                                              aether_rpc_opcode_t opcode,
                                              aether_rpc_handler_t handler) {
    if (!server || !handler || opcode >= 256) {
        return AETHER_ERR_INVALID_PARAM;
    }
    
    pthread_mutex_lock(&server->lock);
    server->handlers[opcode].handler = handler;
    pthread_mutex_unlock(&server->lock);
    
    return AETHER_SUCCESS;
}

aether_result_t aether_rpc_unregister_handler(aether_rpc_server_t* server,
                                               aether_rpc_opcode_t opcode) {
    if (!server || opcode >= 256) {
        return AETHER_ERR_INVALID_PARAM;
    }
    
    pthread_mutex_lock(&server->lock);
    server->handlers[opcode].handler = NULL;
    pthread_mutex_unlock(&server->lock);
    
    return AETHER_SUCCESS;
}

// RPC Client Implementation
aether_rpc_client_t* aether_rpc_client_create(aether_cm_t* cm, const aether_rpc_config_t* config) {
    if (!cm) {
        return NULL;
    }
    
    aether_rpc_client_t* client = (aether_rpc_client_t*)calloc(1, sizeof(aether_rpc_client_t));
    if (!client) {
        return NULL;
    }
    
    client->cm = cm;
    client->max_nodes = AETHER_MAX_NODES;
    
    if (config) {
        client->config = *config;
    } else {
        client->config.max_pending_requests = 1024;
        client->config.max_inflight_requests = 256;
        client->config.request_timeout_ms = 5000;
        client->config.retry_attempts = 3;
    }
    
    client->connections = (void*)calloc(client->max_nodes, sizeof(*client->connections));
    if (!client->connections) {
        free(client);
        return NULL;
    }
    
    for (size_t i = 0; i < client->max_nodes; i++) {
        client->connections[i].node_id = (uint16_t)i;
        client->connections[i].connected = false;
    }
    
    client->pending_requests = aether_lf_hashmap_create(1024);
    if (!client->pending_requests) {
        free(client->connections);
        free(client);
        return NULL;
    }
    
    pthread_mutex_init(&client->lock, NULL);
    client->running = false;
    
    return client;
}

aether_result_t aether_rpc_client_connect(aether_rpc_client_t* client, uint16_t node_id) {
    if (!client || node_id >= client->max_nodes) {
        return AETHER_ERR_INVALID_PARAM;
    }
    
    pthread_mutex_lock(&client->lock);
    client->connections[node_id].connected = true;
    pthread_mutex_unlock(&client->lock);
    
    return AETHER_SUCCESS;
}

aether_result_t aether_rpc_client_disconnect(aether_rpc_client_t* client, uint16_t node_id) {
    if (!client || node_id >= client->max_nodes) {
        return AETHER_ERR_INVALID_PARAM;
    }
    
    pthread_mutex_lock(&client->lock);
    client->connections[node_id].connected = false;
    if (client->connections[node_id].qp) {
        aether_destroy_qp(client->connections[node_id].qp);
        client->connections[node_id].qp = NULL;
    }
    pthread_mutex_unlock(&client->lock);
    
    return AETHER_SUCCESS;
}

aether_result_t aether_rpc_client_destroy(aether_rpc_client_t* client) {
    if (!client) {
        return AETHER_ERR_INVALID_PARAM;
    }
    
    // Disconnect all connections
    for (size_t i = 0; i < client->max_nodes; i++) {
        if (client->connections[i].connected) {
            aether_rpc_client_disconnect(client, (uint16_t)i);
        }
    }
    
    if (client->cq) {
        aether_destroy_cq(client->cq);
    }
    
    pthread_mutex_destroy(&client->lock);
    aether_lf_hashmap_destroy(client->pending_requests);
    free(client->connections);
    free(client);
    
    return AETHER_SUCCESS;
}

// RPC Call Implementation
aether_result_t aether_rpc_call(aether_rpc_client_t* client, uint16_t target_node,
                                  aether_rpc_opcode_t opcode,
                                  const void* request, size_t request_size,
                                  void* response, size_t* response_size,
                                  uint32_t timeout_ms) {
    if (!client || !request) {
        return AETHER_ERR_INVALID_PARAM;
    }
    
    // Create async context
    aether_rpc_ctx_t* ctx = aether_rpc_call_async(client, target_node, opcode,
                                                    request, request_size, NULL, NULL);
    if (!ctx) {
        return AETHER_ERR_FAILED;
    }
    
    // Wait for completion
    aether_result_t result = aether_rpc_wait(ctx, timeout_ms);
    
    if (result == AETHER_SUCCESS && response && response_size) {
        memcpy(response, ctx->response_payload, 
               AETHER_MIN(*response_size, ctx->response_size));
        *response_size = ctx->response_size;
    }
    
    free(ctx->request_payload);
    free(ctx->response_payload);
    free(ctx);
    
    return result;
}

aether_rpc_ctx_t* aether_rpc_call_async(aether_rpc_client_t* client, uint16_t target_node,
                                          aether_rpc_opcode_t opcode,
                                          const void* request, size_t request_size,
                                          void (*callback)(aether_rpc_ctx_t*, void*),
                                          void* callback_arg) {
    if (!client || !request) {
        return NULL;
    }
    
    aether_rpc_ctx_t* ctx = (aether_rpc_ctx_t*)calloc(1, sizeof(aether_rpc_ctx_t));
    if (!ctx) {
        return NULL;
    }
    
    ctx->request_id = aether_rpc_new_request_id();
    ctx->opcode = opcode;
    ctx->target_node = target_node;
    ctx->request_payload = malloc(request_size);
    if (!ctx->request_payload) {
        free(ctx);
        return NULL;
    }
    
    memcpy(ctx->request_payload, request, request_size);
    ctx->request_size = request_size;
    
    ctx->response_payload = malloc(client->config.response_buffer_size);
    if (!ctx->response_payload) {
        free(ctx->request_payload);
        free(ctx);
        return NULL;
    }
    
    ctx->response_size = client->config.response_buffer_size;
    ctx->timestamp = 0;
    ctx->deadline = 0;
    ctx->callback = callback;
    ctx->callback_arg = callback_arg;
    
    pthread_mutex_init(&ctx->mutex, NULL);
    pthread_cond_init(&ctx->cond, NULL);
    
    // Add to pending requests
    aether_lf_hashmap_insert(client->pending_requests, ctx->request_id, ctx);
    
    // Build and send RPC message
    // In a real implementation, would use RDMA SEND/RECV or RDMA write with immediate
    
    return ctx;
}

aether_result_t aether_rpc_wait(aether_rpc_ctx_t* ctx, uint32_t timeout_ms) {
    if (!ctx) {
        return AETHER_ERR_INVALID_PARAM;
    }
    
    pthread_mutex_lock(&ctx->mutex);
    
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_ms / 1000;
    ts.tv_nsec += (timeout_ms % 1000) * 1000000;
    
    while (!ctx->completed) {
        int ret = pthread_cond_timedwait(&ctx->cond, &ctx->mutex, &ts);
        if (ret == ETIMEDOUT) {
            ctx->status = AETHER_RPC_ERR_TIMEOUT;
            pthread_mutex_unlock(&ctx->mutex);
            return AETHER_ERR_TIMEOUT;
        }
    }
    
    pthread_mutex_unlock(&ctx->mutex);
    
    return ctx->status == AETHER_RPC_OK ? AETHER_SUCCESS : AETHER_ERR_FAILED;
}

aether_result_t aether_rpc_cancel(aether_rpc_ctx_t* ctx) {
    if (!ctx) {
        return AETHER_ERR_INVALID_PARAM;
    }
    
    pthread_mutex_lock(&ctx->mutex);
    ctx->completed = true;
    ctx->status = AETHER_RPC_ERR_CANCELED;
    pthread_cond_signal(&ctx->cond);
    pthread_mutex_unlock(&ctx->mutex);
    
    return AETHER_SUCCESS;
}

// RDMA-optimized RPC
aether_result_t aether_rpc_rdma_call(aether_rpc_client_t* client, uint16_t target_node,
                                       aether_rpc_opcode_t opcode,
                                       uint64_t local_addr, uint32_t lkey,
                                       uint64_t remote_addr, uint32_t rkey,
                                       size_t size, uint32_t timeout_ms) {
    if (!client) {
        return AETHER_ERR_INVALID_PARAM;
    }
    
    // Create RDMA write operation
    pthread_mutex_lock(&client->lock);
    
    if (!client->connections[target_node].connected ||
        !client->connections[target_node].qp) {
        pthread_mutex_unlock(&client->lock);
        return AETHER_ERR_NOT_CONNECTED;
    }
    
    aether_qp_t* qp = client->connections[target_node].qp;
    pthread_mutex_unlock(&client->lock);
    
    // Perform RDMA write
    aether_rdma_write_params_t params = {
        .remote_addr = remote_addr,
        .rkey = rkey,
        .local_addr = (void*)local_addr,
        .size = size,
        .lkey = lkey,
        .wr_id = aether_rpc_new_request_id(),
        .signaled = 1,
        .imm_enable = 1,
        .imm_data = opcode
    };
    
    aether_result_t result = aether_rdma_write_sync(qp, &params, timeout_ms);
    
    return result;
}

// Batch Operations
aether_result_t aether_rpc_batch_init(aether_rpc_batch_t* batch, size_t max_count) {
    if (!batch) {
        return AETHER_ERR_INVALID_PARAM;
    }
    
    batch->max_count = max_count;
    batch->requests = (aether_rpc_ctx_t**)calloc(max_count, sizeof(aether_rpc_ctx_t*));
    if (!batch->requests) {
        return AETHER_ERR_OOM;
    }
    
    batch->count = 0;
    return AETHER_SUCCESS;
}

void aether_rpc_batch_destroy(aether_rpc_batch_t* batch) {
    if (!batch) {
        return;
    }
    
    free(batch->requests);
    batch->requests = NULL;
    batch->count = 0;
}

aether_result_t aether_rpc_batch_add(aether_rpc_batch_t* batch, aether_rpc_ctx_t* ctx) {
    if (!batch || !ctx || batch->count >= batch->max_count) {
        return AETHER_ERR_INVALID_PARAM;
    }
    
    batch->requests[batch->count++] = ctx;
    return AETHER_SUCCESS;
}

aether_result_t aether_rpc_batch_execute(aether_rpc_client_t* client, aether_rpc_batch_t* batch) {
    if (!client || !batch) {
        return AETHER_ERR_INVALID_PARAM;
    }
    
    // Execute all RPCs in batch
    // In a real implementation, would post all WRs together
    
    for (size_t i = 0; i < batch->count; i++) {
        aether_rpc_ctx_t* ctx = batch->requests[i];
        // Would initiate RPC
        (void)ctx;
    }
    
    return AETHER_SUCCESS;
}

// Collective RPC
aether_result_t aether_rpc_barrier(aether_rpc_client_t** clients, size_t num_nodes) {
    if (!clients || num_nodes == 0) {
        return AETHER_ERR_INVALID_PARAM;
    }
    
    // Simple barrier using counter
    static _Atomic int barrier_count = 0;
    
    int expected = atomic_fetch_add(&barrier_count, 1) + 1;
    
    if ((size_t)expected == num_nodes) {
        // Last node - reset and wake everyone
        atomic_store(&barrier_count, 0);
    } else {
        // Wait for others
        while (atomic_load(&barrier_count) != 0) {
            // Spin
        }
    }
    
    return AETHER_SUCCESS;
}

aether_result_t aether_rpc_broadcast(aether_rpc_client_t* client,
                                      aether_rpc_opcode_t opcode,
                                      const void* data, size_t size) {
    if (!client || !data) {
        return AETHER_ERR_INVALID_PARAM;
    }
    
    // Broadcast to all connected nodes
    for (size_t i = 0; i < client->max_nodes; i++) {
        if (client->connections[i].connected) {
            aether_rpc_call(client, (uint16_t)i, opcode, data, size, NULL, NULL, 5000);
        }
    }
    
    return AETHER_SUCCESS;
}

aether_result_t aether_rpc_reduce(aether_rpc_client_t* client,
                                   aether_rpc_opcode_t opcode,
                                   void* inout, size_t size, int reduce_op) {
    if (!client || !inout) {
        return AETHER_ERR_INVALID_PARAM;
    }
    
    (void)reduce_op;
    
    // Gather from all nodes and reduce
    for (size_t i = 0; i < client->max_nodes; i++) {
        if (client->connections[i].connected) {
            aether_rpc_call(client, (uint16_t)i, opcode, inout, size, inout, &size, 5000);
        }
    }
    
    return AETHER_SUCCESS;
}

// Utility
uint32_t aether_rpc_new_request_id(void) {
    return atomic_fetch_add(&g_request_id, 1) + 1;
}

const char* aether_rpc_opcode_str(aether_rpc_opcode_t opcode) {
    switch (opcode) {
        case AETHER_RPC_DSM_READ: return "DSM_READ";
        case AETHER_RPC_DSM_WRITE: return "DSM_WRITE";
        case AETHER_RPC_DSM_ATOMIC_FETCH_ADD: return "DSM_ATOMIC_FETCH_ADD";
        case AETHER_RPC_DSM_ATOMIC_CMP_SWAP: return "DSM_ATOMIC_CMP_SWAP";
        case AETHER_RPC_DSM_PAGE_FAULT: return "DSM_PAGE_FAULT";
        case AETHER_RPC_DSM_PAGE_GRANT: return "DSM_PAGE_GRANT";
        case AETHER_RPC_DSM_PAGE_REVOKE: return "DSM_PAGE_REVOKE";
        case AETHER_RPC_SYNC_BARRIER: return "SYNC_BARRIER";
        case AETHER_RPC_SYNC_FENCE: return "SYNC_FENCE";
        case AETHER_RPC_CONN_HANDSHAKE: return "CONN_HANDSHAKE";
        case AETHER_RPC_CONN_KEEPALIVE: return "CONN_KEEPALIVE";
        case AETHER_RPC_CONN_DISCONNECT: return "CONN_DISCONNECT";
        case AETHER_RPC_CTRL_STATS: return "CTRL_STATS";
        case AETHER_RPC_CTRL_HEALTH_CHECK: return "CTRL_HEALTH_CHECK";
        default: return "UNKNOWN";
    }
}

aether_rpc_status_t aether_rpc_status_from_errno(int err) {
    switch (err) {
        case 0: return AETHER_RPC_OK;
        case ETIMEDOUT: return AETHER_RPC_ERR_TIMEOUT;
        case ENOTCONN: return AETHER_RPC_ERR_CONNECTION;
        case EHOSTUNREACH: return AETHER_RPC_ERR_NO_ROUTE;
        default: return AETHER_RPC_ERR_FAILED;
    }
}
