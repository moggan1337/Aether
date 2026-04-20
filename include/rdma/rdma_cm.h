/**
 * Aether - RDMA Connection Manager
 * CM-based connection establishment and management
 *
 * Copyright (c) 2024 Aether Authors
 */

#ifndef AETHER_RDMA_CM_H
#define AETHER_RDMA_CM_H

#include <aether/aether_common.h>
#include <rdma/rdma_device.h>
#include <rdma/rdma_qp.h>
#include <rdma/rdma_memory.h>
#include <infiniband/verbs_exp.h>

#ifdef __cplusplus
extern "C" {
#endif

// Connection event types
typedef enum {
    AETHER_CM_EVENT_CONNECT_REQUEST = 0x01,
    AETHER_CM_EVENT_CONNECTION_ESTABLISHED = 0x02,
    AETHER_CM_EVENT_CONNECTION_LOST = 0x03,
    AETHER_CM_EVENT_CONNECTION_REJECTED = 0x04,
    AETHER_CM_EVENT_DISCONNECTED = 0x05,
    AETHER_CM_EVENT_DEVICE_REMOVAL = 0x06,
    AETHER_CM_EVENT_ADDRESS_RESOLVED = 0x07,
    AETHER_CM_EVENT_ROUTE_RESOLVED = 0x08,
    AETHER_CM_EVENT_ADDRESS_ERROR = 0x09,
    AETHER_CM_EVENT_ROUTE_ERROR = 0x0A,
    AETHER_CM_EVENT_MULTICAST_JOIN = 0x0B,
    AETHER_CM_EVENT_MULTICAST_ERROR = 0x0C,
    AETHER_CM_EVENT_DREQ_RECEIVED = 0x0D,
    AETHER_CM_EVENT_DREP_RECEIVED = 0x0E,
    AETHER_CM_EVENT_ANY = 0xFF
} aether_cm_event_type_t;

// CM Event
typedef struct {
    aether_cm_event_type_t event_type;
    void* private_data;
    size_t private_data_len;
    int status;
    uint32_t id;
    uint32_t provider_context;
} aether_cm_event_t;

// CM Connection Data
typedef struct {
    uint32_t qp_num;
    uint32_t rq_psn;
    uint32_t sq_psn;
    uint32_t target_ack_delay;
    uint8_t initiator_depth;
    uint8_t responder_resources;
    uint8_t local_responder_resources;
    uint8_t remote_responder_resources;
    uint8_t retry_count;
    uint8_t rnr_retry_count;
    uint8_t max_rd_atomic;
    uint8_t max_dest_rd_atomic;
    uint32_t min_rnr_timer;
    uint32_t primary_port;
    uint32_t alternate_port;
    uint8_t primary_path_timeout;
    uint8_t alternate_path_timeout;
    uint16_t primary_traffic_class;
    uint16_t alternate_traffic_class;
    uint8_t primary_service_level;
    uint8_t alternate_service_level;
    uint16_t primary_pkey_index;
    uint16_t alternate_pkey_index;
    uint32_t primary_hop_limit;
    uint32_t alternate_hop_limit;
} aether_cm_conn_param_t;

// RDMA CM Channel
typedef struct aether_cm_channel aether_cm_channel_t;

// CM Handler
typedef struct aether_cm aether_cm_t;

// Connection state machine
typedef enum {
    AETHER_CONN_STATE_IDLE = 0x00,
    AETHER_CONN_STATE_LISTENING = 0x01,
    AETHER_CONN_STATE_CONNECTING = 0x02,
    AETHER_CONN_STATE_ESTABLISHED = 0x03,
    AETHER_CONN_STATE_DISCONNECTING = 0x04,
    AETHER_CONN_STATE_ERROR = 0x05
} aether_conn_state_t;

// Connection Context
typedef struct aether_conn {
    uint32_t conn_id;
    uint16_t remote_node_id;
    aether_qp_t* qp;
    aether_conn_state_t state;
    
    // Endpoint info
    char remote_hostname[256];
    uint16_t remote_port;
    uint16_t local_port;
    
    // CM-specific
    uint32_t cm_id;
    
    // Memory regions shared with this node
    aether_mr_t** shared_mrs;
    size_t shared_mr_count;
    
    // Statistics
    uint64_t bytes_sent;
    uint64_t bytes_received;
    uint64_t operations;
    uint64_t latency_ns;
    
    pthread_mutex_t lock;
    pthread_cond_t cond;
    
    // Linked list
    struct aether_conn* next;
} aether_conn_t;

// CM Event Callback
typedef void (*aether_cm_event_callback_t)(aether_cm_event_t* event, void* arg);

// CM Configuration
typedef struct {
    char* node_name;
    char* service_name;
    uint16_t port;
    uint8_t reuse_addr;
    uint8_t backlog;
    uint8_t async_events;
    uint8_t : 8;
} aether_cm_config_t;

// CM Operations
aether_cm_t* aether_create_cm(aether_device_t* dev, const aether_cm_config_t* config);
aether_result_t aether_destroy_cm(aether_cm_t* cm);

// Server-side operations
aether_result_t aether_cm_listen(aether_cm_t* cm, uint16_t port);
aether_result_t aether_cm_accept(aether_cm_t* cm, uint32_t conn_id, 
                                  const aether_cm_conn_param_t* params);
aether_result_t aether_cm_reject(aether_cm_t* cm, uint32_t conn_id,
                                  const void* private_data, size_t len);

// Client-side operations
aether_result_t aether_cm_connect(aether_cm_t* cm, const char* hostname, uint16_t port,
                                   const aether_cm_conn_param_t* params);
aether_result_t aether_cm_resolve_addr(aether_cm_t* cm, const char* hostname, uint16_t port);
aether_result_t aether_cm_resolve_route(aether_cm_t* cm);

// Disconnect
aether_result_t aether_cm_disconnect(aether_cm_t* cm, uint32_t conn_id, int flush);

// Event handling
aether_result_t aether_cm_get_event(aether_cm_t* cm, aether_cm_event_t* event, uint32_t timeout_ms);
aether_result_t aether_cm_ack_event(aether_cm_t* cm, aether_cm_event_t* event);
aether_result_t aether_cm_set_callback(aether_cm_t* cm, aether_cm_event_callback_t cb, void* arg);

// Connection management
aether_conn_t* aether_cm_get_conn(aether_cm_t* cm, uint32_t conn_id);
aether_conn_t** aether_cm_get_all_connections(aether_cm_t* cm, size_t* count);
aether_result_t aether_cm_remove_connection(aether_cm_t* cm, uint32_t conn_id);

// Multicast
aether_result_t aether_cm_join_multicast(aether_cm_t* cm, const union ibv_gid* gid);
aether_result_t aether_cm_leave_multicast(aether_cm_t* cm, const union ibv_gid* gid);

// Utility
const char* aether_cm_event_str(aether_cm_event_type_t type);
const char* aether_conn_state_str(aether_conn_state_t state);

#ifdef __cplusplus
}
#endif

#endif // AETHER_RDMA_CM_H
