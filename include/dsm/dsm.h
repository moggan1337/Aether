/**
 * Aether - Distributed Shared Memory
 * Page-based DSM with coherence protocols
 *
 * Copyright (c) 2024 Aether Authors
 */

#ifndef AETHER_DSM_H
#define AETHER_DSM_H

#include <aether/aether_common.h>
#include <rdma/rdma_cm.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

// DSM Region
typedef struct aether_dsm_region aether_dsm_region_t;

// DSM Configuration
typedef struct {
    uint16_t node_id;
    uint16_t num_nodes;
    uint32_t page_size;
    size_t total_memory;
    size_t max_regions;
    uint8_t coherence_protocol;
    uint8_t cache_enabled;
    uint32_t cache_size_mb;
    uint32_t page_fault_timeout_ms;
    uint32_t heartbeat_interval_ms;
    uint32_t coherence_hop_limit;
    bool eager_write_mode;
    bool prefetch_enabled;
    bool write_d_notify;
    bool adaptive_coherence;
} aether_dsm_config_t;

// DSM Page Fault Types
typedef enum {
    AETHER_FAULT_READ = 0x01,
    AETHER_FAULT_WRITE = 0x02,
    AETHER_FAULT_EXEC = 0x03,
    AETHER_FAULT_PROTECTION = 0x04
} aether_fault_type_t;

// Page Fault Information
typedef struct {
    uint64_t vpn;
    uint64_t address;
    aether_fault_type_t type;
    uint16_t requesting_node;
    uint16_t owner_node;
    uint8_t sharers;
    uint8_t state;
    bool writable;
} aether_page_fault_t;

// Coherence Message Types
typedef enum {
    AETHER_COHERENCE_REQUEST = 0x01,
    AETHER_COHERENCE_GRANT = 0x02,
    AETHER_COHERENCE_REVOKE = 0x03,
    AETHER_COHERENCE_INVALIDATE = 0x04,
    AETHER_COherence_UPGRADE = 0x05,
    AETHER_COHERENCE_DOWNGRADE = 0x06,
    AETHER_COHERENCE_ACK = 0x07,
    AETHER_COHERENCE_NACK = 0x08,
    AETHER_COHERENCE_WB = 0x09,    // Writeback
    AETHER_COHERENCE_FETCH = 0x0A,
    AETHER_COHERENCE_UPDATE = 0x0B
} aether_coherence_msg_type_t;

// Directory Entry (for directory-based coherence)
typedef struct {
    uint64_t vpn;
    uint8_t owner;
    uint64_t sharers_bitmap;        // Which nodes have sharers
    uint8_t state;
    uint8_t padding[6];
} aether_directory_entry_t;

// Global Virtual Address
typedef struct {
    uint64_t gva;                    // Global virtual address
    uint16_t owner_node;
    uint8_t state;
    uint8_t access;
    uint32_t offset;
} aether_gva_t;

// DSM Region Operations
aether_dsm_region_t* aether_dsm_create_region(size_t size, uint32_t access);
aether_result_t aether_dsm_destroy_region(aether_dsm_region_t* region);

// DSM Initialization
aether_cm_t* aether_dsm_init(const aether_dsm_config_t* config);
aether_result_t aether_dsm_shutdown(void);

// Memory access
aether_result_t aether_dsm_read(aether_dsm_region_t* region, uint64_t offset, 
                                  void* buf, size_t size);
aether_result_t aether_dsm_write(aether_dsm_region_t* region, uint64_t offset,
                                   const void* buf, size_t size);

// Atomic operations on DSM pages
aether_result_t aether_dsm_atomic_fetch_add(aether_dsm_region_t* region, uint64_t offset,
                                              uint64_t add, uint64_t* result);
aether_result_t aether_dsm_atomic_cmp_swap(aether_dsm_region_t* region, uint64_t offset,
                                             uint64_t expected, uint64_t desired,
                                             uint64_t* result);

// Page fault handling
aether_result_t aether_dsm_handle_fault(aether_page_fault_t* fault);
aether_result_t aether_dsm_prefetch(uint64_t gva, size_t size);

// Synchronization
aether_result_t aether_dsm_barrier(void);
aether_result_t aether_dsm_fence(void);

// Page state management
aether_result_t aether_dsm_set_page_owner(uint64_t vpn, uint16_t owner);
aether_result_t aether_dsm_grant_page(uint64_t vpn, uint16_t requester, uint8_t access);
aether_result_t aether_dsm_revoke_page(uint64_t vpn, uint16_t node);
aether_result_t aether_dsm_invalidate_page(uint64_t vpn);

// Cache operations
aether_result_t aether_dsm_flush_cache(void);
aether_result_t aether_dsm_invalidate_cache(uint64_t gva, size_t size);
aether_result_t aether_dsm_prefetch_range(uint64_t gva, size_t size);

// Statistics
aether_result_t aether_dsm_get_stats(aether_perf_counters_t* stats);
aether_result_t aether_dsm_reset_stats(void);

// DSM handle for direct access
typedef struct aether_dsm aether_dsm_t;
aether_dsm_t* aether_dsm_get_handle(void);

#ifdef __cplusplus
}
#endif

#endif // AETHER_DSM_H
