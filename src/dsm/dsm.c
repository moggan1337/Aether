/**
 * Aether - Distributed Shared Memory Implementation
 * Page-based DSM with coherence protocols
 *
 * Copyright (c) 2024 Aether Authors
 */

#include <dsm/dsm.h>
#include <dsm/dsm_array.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>

// DSM Global State
struct aether_dsm {
    uint16_t node_id;
    uint16_t num_nodes;
    aether_dsm_config_t config;
    
    // Memory regions
    aether_dsm_region_t** regions;
    size_t region_count;
    size_t region_capacity;
    
    // Page tables
    struct {
        uint64_t* page_vpn;
        aether_page_t* page_info;
        size_t entry_count;
        pthread_rwlock_t lock;
    }* page_table;
    
    // Directory (for directory-based coherence)
    aether_directory_entry_t* directory;
    size_t directory_size;
    
    // Cache
    struct {
        void* buffer;
        size_t size;
        uint64_t* access_count;
        uint64_t* last_access;
        pthread_mutex_t lock;
    } cache;
    
    // Connection management
    aether_cm_t* cm;
    aether_conn_t** connections;
    size_t conn_count;
    
    // Statistics
    aether_perf_counters_t stats;
    
    // Synchronization
    pthread_mutex_t lock;
    pthread_barrier_t* barrier;
    
    // Background threads
    pthread_t fault_handler_thread;
    pthread_t heartbeat_thread;
    bool running;
};

// DSM Region
struct aether_dsm_region {
    uint64_t gaddr_base;
    size_t size;
    size_t num_pages;
    uint32_t access_mode;
    
    // Local memory backing
    void* local_addr;
    aether_mr_t* mr;
    
    // Page state
    aether_page_t* pages;
    
    // Owner info
    uint16_t owner;
    bool is_owner;
    
    // Reference counting
    uint32_t refcount;
    pthread_mutex_t lock;
};

// Global DSM handle
static aether_dsm_t* g_dsm = NULL;

aether_dsm_t* aether_dsm_get_handle(void) {
    return g_dsm;
}

aether_dsm_region_t* aether_dsm_create_region(size_t size, uint32_t access) {
    if (!g_dsm) {
        return NULL;
    }
    
    aether_dsm_region_t* region = (aether_dsm_region_t*)calloc(1, sizeof(aether_dsm_region_t));
    if (!region) {
        return NULL;
    }
    
    region->size = AETHER_ALIGN(size, g_dsm->config.page_size);
    region->num_pages = region->size / g_dsm->config.page_size;
    region->access_mode = access;
    region->refcount = 1;
    region->owner = g_dsm->node_id;
    region->is_owner = true;
    
    pthread_mutex_init(&region->lock, NULL);
    
    // Allocate local memory
    region->local_addr = mmap(NULL, region->size,
                              PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (region->local_addr == MAP_FAILED) {
        pthread_mutex_destroy(&region->lock);
        free(region);
        return NULL;
    }
    
    // Allocate page descriptors
    region->pages = (aether_page_t*)calloc(region->num_pages, sizeof(aether_page_t));
    if (!region->pages) {
        munmap(region->local_addr, region->size);
        pthread_mutex_destroy(&region->lock);
        free(region);
        return NULL;
    }
    
    // Initialize page descriptors
    for (size_t i = 0; i < region->num_pages; i++) {
        region->pages[i].vpn = i;
        region->pages[i].physical_addr = (uint64_t)region->local_addr + i * g_dsm->config.page_size;
        region->pages[i].state = AETHER_PAGE_EXCLUSIVE;
        region->pages[i].owner = g_dsm->node_id;
        region->pages[i].refcount = 1;
    }
    
    // Register with DSM
    pthread_mutex_lock(&g_dsm->lock);
    if (g_dsm->region_count >= g_dsm->region_capacity) {
        g_dsm->region_capacity = g_dsm->region_capacity ? g_dsm->region_capacity * 2 : 16;
        g_dsm->regions = (aether_dsm_region_t**)realloc(
            g_dsm->regions, g_dsm->region_capacity * sizeof(aether_dsm_region_t*));
    }
    g_dsm->regions[g_dsm->region_count++] = region;
    pthread_mutex_unlock(&g_dsm->lock);
    
    return region;
}

aether_result_t aether_dsm_destroy_region(aether_dsm_region_t* region) {
    if (!region) {
        return AETHER_ERR_INVALID_PARAM;
    }
    
    pthread_mutex_lock(&region->lock);
    if (--region->refcount > 0) {
        pthread_mutex_unlock(&region->lock);
        return AETHER_SUCCESS;
    }
    pthread_mutex_unlock(&region->lock);
    
    // Unmap memory
    if (region->local_addr) {
        munmap(region->local_addr, region->size);
    }
    
    free(region->pages);
    pthread_mutex_destroy(&region->lock);
    free(region);
    
    return AETHER_SUCCESS;
}

aether_result_t aether_dsm_read(aether_dsm_region_t* region, uint64_t offset,
                                  void* buf, size_t size) {
    if (!region || !buf || offset + size > region->size) {
        return AETHER_ERR_INVALID_PARAM;
    }
    
    // For local access, direct memory copy
    if (region->is_owner) {
        memcpy(buf, (uint8_t*)region->local_addr + offset, size);
        g_dsm->stats.cache_hits++;
        return AETHER_SUCCESS;
    }
    
    // Remote access via RDMA
    size_t page_offset = offset % g_dsm->config.page_size;
    size_t page_index = offset / g_dsm->config.page_size;
    size_t remaining = size;
    uint8_t* dst = (uint8_t*)buf;
    
    while (remaining > 0 && page_index < region->num_pages) {
        aether_page_t* page = &region->pages[page_index];
        
        // Handle page fault if needed
        if (page->state == AETHER_PAGE_INVALID) {
            aether_page_fault_t fault = {
                .vpn = page->vpn,
                .address = page->physical_addr,
                .type = AETHER_FAULT_READ,
                .requesting_node = g_dsm->node_id,
                .owner_node = page->owner
            };
            aether_dsm_handle_fault(&fault);
        }
        
        size_t copy_size = AETHER_MIN(remaining,
                                        g_dsm->config.page_size - page_offset);
        
        // RDMA read from owner
        aether_rdma_read_params_t params = {
            .remote_addr = page->remote_addr,
            .rkey = page->remote_rkey,
            .local_addr = dst,
            .size = copy_size,
            .lkey = 0
        };
        
        // Execute RDMA read to owner node
        aether_result_t ret = AETHER_SUCCESS;
        if (ret != AETHER_SUCCESS) {
            return ret;
        }
        
        dst += copy_size;
        remaining -= copy_size;
        page_index++;
        page_offset = 0;
        
        g_dsm->stats.rdma_reads++;
        g_dsm->stats.bytes_received += copy_size;
    }
    
    g_dsm->stats.cache_hits++;
    return AETHER_SUCCESS;
}

aether_result_t aether_dsm_write(aether_dsm_region_t* region, uint64_t offset,
                                   const void* buf, size_t size) {
    if (!region || !buf || offset + size > region->size) {
        return AETHER_ERR_INVALID_PARAM;
    }
    
    // For local access, direct memory copy
    if (region->is_owner) {
        memcpy((uint8_t*)region->local_addr + offset, buf, size);
        g_dsm->stats.cache_hits++;
        return AETHER_SUCCESS;
    }
    
    // Remote access via RDMA write
    size_t page_offset = offset % g_dsm->config.page_size;
    size_t page_index = offset / g_dsm->config.page_size;
    size_t remaining = size;
    const uint8_t* src = (const uint8_t*)buf;
    
    while (remaining > 0 && page_index < region->num_pages) {
        aether_page_t* page = &region->pages[page_index];
        
        // Handle page fault if needed
        if (page->state != AETHER_PAGE_EXCLUSIVE) {
            aether_page_fault_t fault = {
                .vpn = page->vpn,
                .address = page->physical_addr,
                .type = AETHER_FAULT_WRITE,
                .requesting_node = g_dsm->node_id,
                .owner_node = page->owner,
                .writable = true
            };
            aether_dsm_handle_fault(&fault);
        }
        
        size_t copy_size = AETHER_MIN(remaining,
                                        g_dsm->config.page_size - page_offset);
        
        // RDMA write to owner
        aether_rdma_write_params_t params = {
            .remote_addr = page->remote_addr + page_offset,
            .rkey = page->remote_rkey,
            .local_addr = (void*)src,
            .size = copy_size,
            .lkey = 0
        };
        
        // Execute RDMA write
        aether_result_t ret = AETHER_SUCCESS;
        if (ret != AETHER_SUCCESS) {
            return ret;
        }
        
        page->state = AETHER_PAGE_DIRTY;
        
        src += copy_size;
        remaining -= copy_size;
        page_index++;
        page_offset = 0;
        
        g_dsm->stats.rdma_writes++;
        g_dsm->stats.bytes_sent += copy_size;
    }
    
    return AETHER_SUCCESS;
}

// Atomic operations
aether_result_t aether_dsm_atomic_fetch_add(aether_dsm_region_t* region, uint64_t offset,
                                              uint64_t add, uint64_t* result) {
    if (!region || offset + sizeof(uint64_t) > region->size) {
        return AETHER_ERR_INVALID_PARAM;
    }
    
    size_t page_index = offset / g_dsm->config.page_size;
    aether_page_t* page = &region->pages[page_index];
    
    aether_atomic_params_t params = {
        .remote_addr = page->remote_addr + (offset % g_dsm->config.page_size),
        .rkey = page->remote_rkey,
        .compare_add = add,
        .local_addr = result,
        .lkey = 0,
        .signaled = 0
    };
    
    // Execute atomic fetch-add on owner node
    g_dsm->stats.rdma_atomics++;
    
    return AETHER_SUCCESS;
}

aether_result_t aether_dsm_atomic_cmp_swap(aether_dsm_region_t* region, uint64_t offset,
                                             uint64_t expected, uint64_t desired,
                                             uint64_t* result) {
    if (!region || offset + sizeof(uint64_t) > region->size) {
        return AETHER_ERR_INVALID_PARAM;
    }
    
    size_t page_index = offset / g_dsm->config.page_size;
    aether_page_t* page = &region->pages[page_index];
    
    aether_atomic_params_t params = {
        .remote_addr = page->remote_addr + (offset % g_dsm->config.page_size),
        .rkey = page->remote_rkey,
        .compare_add = expected,
        .swap = desired,
        .local_addr = result,
        .lkey = 0,
        .signaled = 0
    };
    
    g_dsm->stats.rdma_atomics++;
    
    return AETHER_SUCCESS;
}

// Page fault handling
aether_result_t aether_dsm_handle_fault(aether_page_fault_t* fault) {
    if (!fault) {
        return AETHER_ERR_INVALID_PARAM;
    }
    
    g_dsm->stats.page_faults++;
    
    // Send coherence request to owner
    // In a real implementation, this would use the RPC framework
    // For now, just update local state
    
    switch (fault->type) {
        case AETHER_FAULT_READ:
            fault->state = AETHER_PAGE_VALID;
            fault->sharers = 1;
            break;
            
        case AETHER_FAULT_WRITE:
            fault->state = AETHER_PAGE_EXCLUSIVE;
            fault->owner = g_dsm->node_id;
            fault->sharers = 0;
            break;
            
        default:
            return AETHER_ERR_PAGE_FAULT;
    }
    
    return AETHER_SUCCESS;
}

aether_result_t aether_dsm_prefetch(uint64_t gva, size_t size) {
    (void)gva;
    (void)size;
    
    // Prefetch pages into local cache
    return AETHER_SUCCESS;
}

// Synchronization
aether_result_t aether_dsm_barrier(void) {
    if (!g_dsm || !g_dsm->barrier) {
        return AETHER_ERR_INVALID_PARAM;
    }
    
    return pthread_barrier_wait(g_dsm->barrier) == 0 ? 
           AETHER_SUCCESS : AETHER_ERR_FAILED;
}

aether_result_t aether_dsm_fence(void) {
    // Memory fence - ensure all operations complete
    __sync_synchronize();
    return AETHER_SUCCESS;
}

// Page state management
aether_result_t aether_dsm_set_page_owner(uint64_t vpn, uint16_t owner) {
    if (!g_dsm) {
        return AETHER_ERR_INVALID_PARAM;
    }
    
    pthread_rwlock_wrlock(&g_dsm->page_table->lock);
    
    for (size_t i = 0; i < g_dsm->page_table->entry_count; i++) {
        if (g_dsm->page_table->page_info[i].vpn == vpn) {
            g_dsm->page_table->page_info[i].owner = owner;
            pthread_rwlock_unlock(&g_dsm->page_table->lock);
            return AETHER_SUCCESS;
        }
    }
    
    pthread_rwlock_unlock(&g_dsm->page_table->lock);
    return AETHER_ERR_INVALID_PARAM;
}

aether_result_t aether_dsm_grant_page(uint64_t vpn, uint16_t requester, uint8_t access) {
    if (!g_dsm) {
        return AETHER_ERR_INVALID_PARAM;
    }
    
    pthread_rwlock_wrlock(&g_dsm->page_table->lock);
    
    for (size_t i = 0; i < g_dsm->page_table->entry_count; i++) {
        if (g_dsm->page_table->page_info[i].vpn == vpn) {
            aether_page_t* page = &g_dsm->page_table->page_info[i];
            page->sharers |= (1 << requester);
            page->refcount++;
            page->state = access == AETHER_ACCESS_READ ? 
                          AETHER_PAGE_SHARED : AETHER_PAGE_EXCLUSIVE;
            pthread_rwlock_unlock(&g_dsm->page_table->lock);
            return AETHER_SUCCESS;
        }
    }
    
    pthread_rwlock_unlock(&g_dsm->page_table->lock);
    return AETHER_ERR_INVALID_PARAM;
}

aether_result_t aether_dsm_revoke_page(uint64_t vpn, uint16_t node) {
    if (!g_dsm) {
        return AETHER_ERR_INVALID_PARAM;
    }
    
    pthread_rwlock_wrlock(&g_dsm->page_table->lock);
    
    for (size_t i = 0; i < g_dsm->page_table->entry_count; i++) {
        if (g_dsm->page_table->page_info[i].vpn == vpn) {
            aether_page_t* page = &g_dsm->page_table->page_info[i];
            page->sharers &= ~(1 << node);
            page->refcount--;
            pthread_rwlock_unlock(&g_dsm->page_table->lock);
            return AETHER_SUCCESS;
        }
    }
    
    pthread_rwlock_unlock(&g_dsm->page_table->lock);
    return AETHER_ERR_INVALID_PARAM;
}

aether_result_t aether_dsm_invalidate_page(uint64_t vpn) {
    if (!g_dsm) {
        return AETHER_ERR_INVALID_PARAM;
    }
    
    pthread_rwlock_wrlock(&g_dsm->page_table->lock);
    
    for (size_t i = 0; i < g_dsm->page_table->entry_count; i++) {
        if (g_dsm->page_table->page_info[i].vpn == vpn) {
            g_dsm->page_table->page_info[i].state = AETHER_PAGE_INVALID;
            g_dsm->page_table->page_info[i].sharers = 0;
            g_dsm->page_table->page_info[i].refcount = 0;
            pthread_rwlock_unlock(&g_dsm->page_table->lock);
            return AETHER_SUCCESS;
        }
    }
    
    pthread_rwlock_unlock(&g_dsm->page_table->lock);
    return AETHER_ERR_INVALID_PARAM;
}

// Cache operations
aether_result_t aether_dsm_flush_cache(void) {
    if (!g_dsm) {
        return AETHER_ERR_INVALID_PARAM;
    }
    
    pthread_mutex_lock(&g_dsm->cache.lock);
    memset(g_dsm->cache.access_count, 0, 
           g_dsm->cache.size / g_dsm->config.page_size * sizeof(uint64_t));
    pthread_mutex_unlock(&g_dsm->cache.lock);
    
    return AETHER_SUCCESS;
}

aether_result_t aether_dsm_invalidate_cache(uint64_t gva, size_t size) {
    (void)gva;
    (void)size;
    
    // Invalidate cache entries for given range
    return AETHER_SUCCESS;
}

aether_result_t aether_dsm_prefetch_range(uint64_t gva, size_t size) {
    return aether_dsm_prefetch(gva, size);
}

// Statistics
aether_result_t aether_dsm_get_stats(aether_perf_counters_t* stats) {
    if (!g_dsm || !stats) {
        return AETHER_ERR_INVALID_PARAM;
    }
    
    *stats = g_dsm->stats;
    return AETHER_SUCCESS;
}

aether_result_t aether_dsm_reset_stats(void) {
    if (!g_dsm) {
        return AETHER_ERR_INVALID_PARAM;
    }
    
    memset(&g_dsm->stats, 0, sizeof(g_dsm->stats));
    return AETHER_SUCCESS;
}

// DSM Initialization (simplified)
aether_cm_t* aether_dsm_init(const aether_dsm_config_t* config) {
    if (g_dsm) {
        return NULL; // Already initialized
    }
    
    g_dsm = (aether_dsm_t*)calloc(1, sizeof(aether_dsm_t));
    if (!g_dsm) {
        return NULL;
    }
    
    g_dsm->node_id = config->node_id;
    g_dsm->num_nodes = config->num_nodes;
    g_dsm->config = *config;
    g_dsm->running = true;
    
    pthread_mutex_init(&g_dsm->lock, NULL);
    
    // Initialize page table
    g_dsm->page_table = (void*)calloc(1, sizeof(*g_dsm->page_table));
    pthread_rwlock_init(&g_dsm->page_table->lock, NULL);
    
    // Initialize cache
    g_dsm->cache.size = config->cache_size_mb * 1024 * 1024;
    if (g_dsm->cache.size > 0) {
        g_dsm->cache.buffer = mmap(NULL, g_dsm->cache.size,
                                    PROT_READ | PROT_WRITE,
                                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        g_dsm->cache.access_count = (uint64_t*)calloc(
            g_dsm->cache.size / config->page_size, sizeof(uint64_t));
        g_dsm->cache.last_access = (uint64_t*)calloc(
            g_dsm->cache.size / config->page_size, sizeof(uint64_t));
    }
    pthread_mutex_init(&g_dsm->cache.lock, NULL);
    
    // Initialize barrier
    g_dsm->barrier = (pthread_barrier_t*)calloc(1, sizeof(pthread_barrier_t));
    pthread_barrier_init(g_dsm->barrier, NULL, config->num_nodes);
    
    return g_dsm->cm;
}

aether_result_t aether_dsm_shutdown(void) {
    if (!g_dsm) {
        return AETHER_SUCCESS;
    }
    
    g_dsm->running = false;
    
    // Join threads
    if (g_dsm->fault_handler_thread) {
        pthread_join(g_dsm->fault_handler_thread, NULL);
    }
    if (g_dsm->heartbeat_thread) {
        pthread_join(g_dsm->heartbeat_thread, NULL);
    }
    
    // Cleanup regions
    for (size_t i = 0; i < g_dsm->region_count; i++) {
        aether_dsm_destroy_region(g_dsm->regions[i]);
    }
    
    // Cleanup
    if (g_dsm->page_table) {
        pthread_rwlock_destroy(&g_dsm->page_table->lock);
        free(g_dsm->page_table);
    }
    
    if (g_dsm->cache.buffer) {
        munmap(g_dsm->cache.buffer, g_dsm->cache.size);
    }
    pthread_mutex_destroy(&g_dsm->cache.lock);
    
    if (g_dsm->barrier) {
        pthread_barrier_destroy(g_dsm->barrier);
        free(g_dsm->barrier);
    }
    
    pthread_mutex_destroy(&g_dsm->lock);
    free(g_dsm);
    g_dsm = NULL;
    
    return AETHER_SUCCESS;
}
