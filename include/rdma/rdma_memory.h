/**
 * Aether - RDMA Memory Management
 * Memory regions, protection domains, and address translation
 *
 * Copyright (c) 2024 Aether Authors
 */

#ifndef AETHER_RDMA_MEMORY_H
#define AETHER_RDMA_MEMORY_H

#include <aether/aether_common.h>
#include <rdma/rdma_device.h>
#include <infiniband/verbs.h>

#ifdef __cplusplus
extern "C" {
#endif

// Memory region access permissions
#define AETHER_MR_PERMISSION_LOCAL_WRITE   IBV_ACCESS_LOCAL_WRITE
#define AETHER_MR_PERMISSION_REMOTE_READ   IBV_ACCESS_REMOTE_READ
#define AETHER_MR_PERMISSION_REMOTE_WRITE  IBV_ACCESS_REMOTE_WRITE
#define AETHER_MR_PERMISSION_ATOMIC        IBV_ACCESS_REMOTE_ATOMIC
#define AETHER_MR_PERMISSION_MW_BIND       IBV_ACCESS_MW_BIND
#define AETHER_MR_PERMISSION_ZERO_BASED    IBV_ACCESS_ZERO_BASED
#define AETHER_MR_PERMISSION_ALLOC         IBV_ACCESS_ALLOCATE_MR

// Memory Window for one-sided access
typedef struct aether_mw {
    uint64_t addr;
    uint64_t size;
    uint32_t rkey;
    uint32_t lkey;
    uint8_t type;          // IBV_MW_TYPE_1 or IBV_MW_TYPE_2
    uint16_t pd_id;
    struct ibv_mw* ibv_mw;
} aether_mw_t;

// Memory Region
typedef struct aether_mr {
    void* addr;
    size_t size;
    uint32_t lkey;
    uint32_t rkey;
    uint64_t pd_handle;
    struct ibv_mr* ibv_mr;
    
    // Extended metadata
    uint16_t owner_node;
    uint8_t access_flags;
    bool is_registered;
    bool is_pinned;
    bool is_dmabuf;
    
    // Statistics
    uint64_t total_accesses;
    uint64_t total_bytes;
} aether_mr_t;

// Protection Domain
typedef struct aether_pd {
    struct ibv_pd* ibv_pd;
    uint32_t pd_id;
    uint32_t refcount;
    
    // Resource tracking
    aether_mr_t** mrs;
    size_t mr_count;
    size_t mr_capacity;
    
    // Memory windows
    aether_mw_t** mws;
    size_t mw_count;
    size_t mw_capacity;
    
    aether_device_t* device;
} aether_pd_t;

// Global Address Space Entry
typedef struct {
    uint64_t va;           // Virtual address in global space
    uint64_t pa;           // Physical address (for IOMMU)
    uint32_t rkey;         // Remote key
    uint32_t size;         // Region size
    uint16_t owner_node;   // Owner node ID
    uint8_t type;          // Region type
    uint8_t access;        // Access permissions
    uint64_t timestamp;    // Registration time
} aether_gaddr_entry_t;

// Global Address Space
typedef struct {
    uint64_t base_addr;
    uint64_t total_size;
    uint32_t entry_count;
    uint32_t page_size;
    
    aether_gaddr_entry_t* entries;
    void* bitmap;          // Free page bitmap
    uint64_t bitmap_size;
    
    pthread_rwlock_t lock;
} aether_gaddr_space_t;

// DMA Buffer (for large allocations)
typedef struct {
    void* addr;
    size_t size;
    uint32_t lkey;
    uint32_t rkey;
    int fd;                // File descriptor for hugepages
    bool use_hugepages;
    bool is_pinned;
} aether_dmabuf_t;

// Memory registration parameters
typedef struct {
    void* addr;
    size_t size;
    uint64_t access_flags;
    bool use_hugepages;
    bool zero_based;
    bool allow_remote_access;
    bool create_mw;
} aether_mem_reg_params_t;

// PD Operations
aether_pd_t* aether_create_pd(aether_device_t* dev);
aether_result_t aether_destroy_pd(aether_pd_t* pd);

// Memory Region Operations
aether_mr_t* aether_reg_mr(aether_pd_t* pd, const aether_mem_reg_params_t* params);
aether_result_t aether_dereg_mr(aether_mr_t* mr);

aether_mr_t* aether_reg_dmabuf(aether_device_t* dev, aether_pd_t* pd,
                                int fd, uint64_t offset, size_t size,
                                uint64_t access);

aether_result_t aether_sync_mr(aether_mr_t* mr, uint64_t offset, size_t length, int dir);

// Memory Window Operations
aether_mw_t* aether_alloc_mw(aether_pd_t* pd, uint8_t type);
aether_result_t aether_dealloc_mw(aether_mw_t* mw);
aether_result_t aether_bind_mw(aether_mw_t* mw, aether_mr_t* mr,
                                uint64_t addr, uint64_t size, uint64_t access);

// DMA Buffer Operations
aether_dmabuf_t* aether_alloc_dmabuf(aether_device_t* dev, aether_pd_t* pd,
                                     size_t size, bool hugepages);
aether_result_t aether_free_dmabuf(aether_dmabuf_t* buf);
aether_result_t aether_sync_dmabuf(aether_dmabuf_t* buf, size_t offset, 
                                     size_t length, int dir);

// Global Address Space Operations
aether_gaddr_space_t* aether_create_gaddr_space(uint64_t base, uint64_t size);
aether_result_t aether_destroy_gaddr_space(aether_gaddr_space_t* gas);
aether_result_t aether_gaddr_register(aether_gaddr_space_t* gas, aether_mr_t* mr,
                                        uint16_t owner_node, uint8_t access,
                                        uint64_t* out_gaddr);
aether_result_t aether_gaddr_unregister(aether_gaddr_space_t* gas, uint64_t gaddr);
aether_result_t aether_gaddr_lookup(aether_gaddr_space_t* gas, uint64_t gaddr,
                                     aether_gaddr_entry_t* entry);
uint64_t aether_gaddr_hash(uint64_t gaddr);

// Prefetching hints
aether_result_t aether_prefetch_mr(aether_mr_t* mr, uint64_t offset, size_t length);

#ifdef __cplusplus
}
#endif

#endif // AETHER_RDMA_MEMORY_H
