/**
 * Aether - Distributed Shared Memory with RDMA
 * Common types and definitions
 *
 * Copyright (c) 2024 Aether Authors
 * Licensed under the MIT License
 */

#ifndef AETHER_COMMON_H
#define AETHER_COMMON_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdalign.h>

#ifdef __cplusplus
extern "C" {
#endif

// Configuration constants
#define AETHER_MAX_NODES 256
#define AETHER_MAX_QP_PER_NODE 64
#define AETHER_PAGE_SIZE 4096
#define AETHER_HUGEPAGE_SIZE (2 * 1024 * 1024)
#define AETHER_MAX_REGIONS 1024
#define AETHER_RPC_BUFFER_SIZE 8192
#define AETHER_CACHELINE_SIZE 64

// RDMA Configuration
#define AETHER_MAX_SGE 16
#define AETHER_MAX_WR 1024
#define AETHER_TIMEOUT_MS 14
#define AETHER_RETRY_COUNT 7
#define AETHER_RNR_RETRY 7
#define AETHER_PSN_MASK 0xFFFFFF

// Memory Region Types
typedef enum {
    AETHER_MR_READONLY = 0x01,
    AETHER_MR_READWRITE = 0x02,
    AETHER_MR_EXCLUSIVE = 0x04,
    AETHER_MR_SHARED = 0x08,
    AETHER_MR_PERSISTENT = 0x10
} aether_mr_flags_t;

// Node Types
typedef enum {
    AETHER_NODE_TYPE_SERVER = 0x01,
    AETHER_NODE_TYPE_CLIENT = 0x02,
    AETHER_NODE_TYPE_HYBRID = 0x03
} aether_node_type_t;

// Connection State
typedef enum {
    AETHER_STATE_DISCONNECTED = 0x00,
    AETHER_STATE_CONNECTING = 0x01,
    AETHER_STATE_CONNECTED = 0x02,
    AETHER_STATE_ERROR = 0x03,
    AETHER_STATE_SHUTDOWN = 0x04
} aether_state_t;

// Operation Types
typedef enum {
    AETHER_OP_READ = 0x01,
    AETHER_OP_WRITE = 0x02,
    AETHER_OP_ATOMIC_CMP_SWAP = 0x03,
    AETHER_OP_ATOMIC_FETCH_ADD = 0x04,
    AETHER_OP_SEND = 0x05,
    AETHER_OP_RECV = 0x06,
    AETHER_OP_BARRIER = 0x07,
    AETHER_OP_FENCE = 0x08
} aether_op_type_t;

// DSM Page States
typedef enum {
    AETHER_PAGE_INVALID = 0x00,
    AETHER_PAGE_VALID = 0x01,
    AETHER_PAGE_DIRTY = 0x02,
    AETHER_PAGE_EXCLUSIVE = 0x03,
    AETHER_PAGE_SHARED = 0x04,
    AETHER_PAGE_UNOWNED = 0x05
} aether_page_state_t;

// DSM Coherence Protocols
typedef enum {
    AETHER_COHERENCE_MESI = 0x01,
    AETHER_COHERENCE_MOESI = 0x02,
    AETHER_COHERENCE_DIRECTORY = 0x03,
    AETHER_COHERENCE_TL = 0x04  // Token-based coherence
} aether_coherence_t;

// DSM Access Modes
typedef enum {
    AETHER_ACCESS_NONE = 0x00,
    AETHER_ACCESS_READ = 0x01,
    AETHER_ACCESS_WRITE = 0x02,
    AETHER_ACCESS_RW = 0x03,
    AETHER_ACCESS_EXCLUSIVE = 0x04
} aether_access_t;

// Node Identifier
typedef struct {
    uint16_t node_id;
    uint16_t lid;
    uint32_t qpn;
    uint64_t guid;
    char hostname[256];
    uint16_t port;
} aether_node_id_t;

// RDMA Address Vector
typedef struct {
    uint16_t lid;
    uint32_t qpn;
    uint32_t psn;
    uint64_t guid;
    uint8_t sl;
    uint8_t traffic_class;
    uint8_t hop_limit;
    uint16_t pkey;
} aether_addr_t;

// Memory Region Descriptor
typedef struct {
    uint64_t addr;
    uint64_t size;
    uint32_t lkey;
    uint32_t rkey;
    uint8_t flags;
    uint8_t node_id;
    uint16_t padding;
} aether_mr_t;

// RPC Header
typedef struct {
    uint32_t magic;
    uint32_t opcode;
    uint32_t payload_size;
    uint32_t flags;
    uint64_t request_id;
    uint64_t timestamp;
    uint16_t source_node;
    uint16_t target_node;
    uint32_t checksum;
} aether_rpc_header_t;

// DSM Page Descriptor
typedef struct {
    uint64_t vpn;                    // Virtual Page Number
    uint64_t physical_addr;         // Local physical address
    uint64_t remote_addr;            // Remote RDMA address
    uint32_t remote_rkey;            // Remote RKey
    uint8_t state;                   // Page state
    uint8_t owner;                   // Owner node ID
    uint8_t sharers;                 // Number of sharers (bitmask)
    uint16_t refcount;               // Reference count
    uint64_t version;               // Version for coherence
    uint64_t last_access;           // Last access timestamp
    uint64_t padding;
} aether_page_t;

// Performance Counter Structure
typedef struct {
    uint64_t rdma_reads;
    uint64_t rdma_writes;
    uint64_t rdma_atomics;
    uint64_t rpc_calls;
    uint64_t rpc_replies;
    uint64_t page_faults;
    uint64_t cache_hits;
    uint64_t cache_misses;
    uint64_t lock_contentions;
    uint64_t bytes_sent;
    uint64_t bytes_received;
    uint64_t cycles_spent;
} aether_perf_counters_t;

// Configuration
typedef struct {
    uint16_t node_id;
    uint16_t num_nodes;
    aether_node_id_t* nodes;
    char* ib_dev_name;
    uint8_t ib_port;
    uint32_t qp_timeout;
    uint32_t max_inline_data;
    uint32_t ooo_resources;
    uint32_t cq_size;
    size_t gaddr_space_size;
    size_t max_mr_size;
    uint8_t coherence_protocol;
    uint8_t cache_policy;
    uint16_t cache_size_mb;
    uint32_t page_fault_timeout_ms;
    uint32_t heartbeat_interval_ms;
    uint32_t reconnect_delay_ms;
} aether_config_t;

// Memory fence types
typedef enum {
    AETHER_FENCE_NONE = 0x00,
    AETHER_FENCE_RDMA = 0x01,
    AETHER_FENCE_RPC = 0x02,
    AETHER_FENCE_ALL = 0x03
} aether_fence_type_t;

// Atomic operations
typedef enum {
    AETHER_ATOMIC_CMP_SWAP = 0x01,
    AETHER_ATOMIC_FETCH_ADD = 0x02,
    AETHER_ATOMIC_SWAP = 0x03,
    AETHER_ATOMIC_FETCH_AND = 0x04,
    AETHER_ATOMIC_FETCH_OR = 0x05,
    AETHER_ATOMIC_FETCH_XOR = 0x06
} aether_atomic_op_t;

// Result codes
typedef enum {
    AETHER_SUCCESS = 0,
    AETHER_ERR_NO_DEVICE = -1,
    AETHER_ERR_NO_PORTS = -2,
    AETHER_ERR_CTX_CREATE = -3,
    AETHER_ERR_PD_CREATE = -4,
    AETHER_ERR_CQ_CREATE = -5,
    AETHER_ERR_QP_CREATE = -6,
    AETHER_ERR_MR_ALLOC = -7,
    AETHER_ERR_CONN_FAILED = -8,
    AETHER_ERR_TIMEOUT = -9,
    AETHER_ERR_INVALID_PARAM = -10,
    AETHER_ERR_OUT_OF_RESOURCES = -11,
    AETHER_ERR_PAGE_FAULT = -12,
    AETHER_ERR_PROTECTION = -13,
    AETHER_ERR_VERSION_MISMATCH = -14,
    AETHER_ERR_NODE_UNREACHABLE = -15,
    AETHER_ERR_PROTOCOL = -16,
    AETHER_ERR_OOM = -17,
    AETHER_ERR_NOT_CONNECTED = -18,
    AETHER_ERR_SHUTDOWN = -19,
    AETHER_ERR_WC_ERROR = -20,
    AETHER_ERR_POLL_ERROR = -21
} aether_result_t;

// Version information
#define AETHER_VERSION_MAJOR 1
#define AETHER_VERSION_MINOR 0
#define AETHER_VERSION_PATCH 0
#define AETHER_MAGIC 0xAETHER2024

// Utility macros
#define AETHER_ALIGN(x, a) (((x) + ((a) - 1)) & ~((a) - 1))
#define AETHER_IS_POWER_OF_TWO(x) (((x) != 0) && (((x) & ((x) - 1)) == 0))
#define AETHER_MIN(a, b) ((a) < (b) ? (a) : (b))
#define AETHER_MAX(a, b) ((a) > (b) ? (a) : (b))

// For C++
#ifdef __cplusplus
}
#endif

#endif // AETHER_COMMON_H
