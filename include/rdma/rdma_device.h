/**
 * Aether - RDMA Device Management
 * InfiniBand/RoCE device discovery and initialization
 *
 * Copyright (c) 2024 Aether Authors
 */

#ifndef AETHER_RDMA_DEVICE_H
#define AETHER_RDMA_DEVICE_H

#include <aether/aether_common.h>
#include <infiniband/verbs.h>

#ifdef __cplusplus
extern "C" {
#endif

// Device capability flags
#define AETHER_DEV_CAP_MASK_RC       0x01
#define AETHER_DEV_CAP_MASK_UD       0x02
#define AETHER_DEV_CAP_MASK_UC       0x04
#define AETHER_DEV_CAP_MASK_RD       0x08
#define AETHER_DEV_CAP_MASK_ATOMIC   0x10
#define AETHER_DEV_CAP_MASK_SRQ      0x20
#define AETHER_DEV_CAP_MASK_XRC      0x40
#define AETHER_DEV_CAP_MASK_MEM_WINDOW 0x80

// Device Information
typedef struct {
    char name[64];
    char vendor_name[64];
    char hw_ver[64];
    char firmware[128];
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t revision;
    uint64_t node_guid;
    uint64_t sys_image_guid;
    uint32_t max_cq;
    uint32_t max_cqe;
    uint32_t max_qp;
    uint32_t max_qp_wr;
    uint32_t max_sge;
    uint32_t max_sge_rd;
    uint32_t max_cq_entries;
    uint32_t max_mr;
    uint64_t max_mr_size;
    uint64_t page_size_cap;
    uint32_t max_pd;
    uint32_t max_ah;
    uint32_t max_ee_rd_atom;
    uint32_t max_res_rd_atom;
    uint32_t max_qp_rd_atom;
    uint32_t min_rnr_timer;
    uint32_t max_ud_av;
    uint32_t max_ud_qp;
    uint32_t max_total_mcast_qp_attach;
    uint16_t max_pkeys;
    uint16_t local_ca_ack_delay;
    uint8_t max_port;
    uint64_t caps_mask;
    struct ibv_device* ibv_dev;
} aether_device_info_t;

// Port Information
typedef struct {
    uint8_t port_num;
    uint8_t state;
    uint8_t max_mtu;
    uint8_t active_mtu;
    uint16_t gid_tbl_len;
    uint16_t port_cap_flags;
    uint32_t max_msg_size;
    uint32_t bad_pkey_cntr;
    uint32_t qkey_viol_cntr;
    uint16_t pkey_tbl_len;
    uint8_t sm_sl;
    uint8_t sm_lid;
    uint64_t cap_mask;
    uint32_t max_vl_num;
    uint8_t init_type_reply;
    uint8_t active_width;
    uint8_t active_speed;
    uint8_t phys_state;
    uint32_t link_layer;
    uint64_t subnet_timeout;
} aether_port_info_t;

// Device Context
typedef struct aether_device aether_device_t;

struct aether_device {
    struct ibv_context* ctx;
    struct ibv_device_attr attr;
    struct ibv_port_attr port_attrs[AETHER_MAX_NODES];
    aether_device_info_t info;
    uint8_t active_port;
    bool is_open;
    
    // Device functions
    aether_result_t (*close)(aether_device_t* dev);
    aether_result_t (*query_port)(aether_device_t* dev, uint8_t port, 
                                   aether_port_info_t* info);
    aether_result_t (*get_gid)(aether_device_t* dev, uint8_t port,
                               int index, union ibv_gid* gid);
};

// Device enumeration
int aether_discover_devices(aether_device_info_t** devices);
void aether_free_device_list(aether_device_info_t* devices, int count);

// Device management
aether_device_t* aether_open_device(const char* dev_name);
aether_result_t aether_close_device(aether_device_t* dev);

// Device queries
aether_result_t aether_query_device(aether_device_t* dev);
aether_result_t aether_query_port(aether_device_t* dev, uint8_t port,
                                   aether_port_info_t* info);
aether_result_t aether_get_gid(aether_device_t* dev, uint8_t port,
                                int index, union ibv_gid* gid);

// Device capabilities check
bool aether_device_supports_rc(aether_device_t* dev);
bool aether_device_supports_ud(aether_device_t* dev);
bool aether_device_supports_atomics(aether_device_t* dev);
bool aether_device_supports_srq(aether_device_t* dev);

#ifdef __cplusplus
}
#endif

#endif // AETHER_RDMA_DEVICE_H
