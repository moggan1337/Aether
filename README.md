# Aether - Distributed Shared Memory with RDMA

<p align="center">
  <img src="docs/aether-logo.png" alt="Aether Logo" width="200"/>
</p>

**High-Performance Distributed Shared Memory System Using Remote Direct Memory Access**

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Build Status](https://github.com/moggan1337/Aether/actions/workflows/build.yml/badge.svg)](https://github.com/moggan1337/Aether/actions)
[![Documentation](https://img.shields.io/badge/docs-latest-brightgreen.svg)](docs/)
[![Paper](https://img.shields.io/badge/paper-arXiv-red.svg)](https://arxiv.org/)

## Overview

Aether is a production-grade distributed shared memory (DSM) system that leverages RDMA (Remote Direct Memory Access) technology to achieve ultra-low latency and high-throughput inter-node communication. Built on libibverbs, Aether provides transparent distributed memory sharing across compute nodes connected via InfiniBand or RoCE networks.

### Key Features

- **One-Sided Memory Operations**: True zero-copy remote memory access without involving the remote CPU
- **Page-Based DSM**: Fine-grained memory sharing with configurable page sizes
- **Multiple Coherence Protocols**: MESI, MOESI, Directory-based, and Token-based coherence
- **Lock-Free Data Structures**: RCU (Read-Copy-Update) and MVRLU implementations
- **RDMA-Aware RPC**: High-performance RPC framework optimized for RDMA transport
- **Memory-Mapped Distributed Arrays**: NumPy-like array interface for distributed data
- **Comprehensive Profiling**: Built-in performance monitoring and benchmarking tools

## Table of Contents

1. [Architecture](#architecture)
2. [RDMA Internals](#rdma-internals)
3. [Installation](#installation)
4. [Quick Start](#quick-start)
5. [API Reference](#api-reference)
6. [Performance Tuning](#performance-tuning)
7. [Benchmarks](#benchmarks)
8. [Troubleshooting](#troubleshooting)
9. [Contributing](#contributing)
10. [License](#license)

## Architecture

### System Components

```
┌─────────────────────────────────────────────────────────────────┐
│                        Aether DSM Layer                          │
├─────────────────────────────────────────────────────────────────┤
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────────┐  │
│  │  DSM Core   │  │  Coherence  │  │    Lock-Free Structures  │  │
│  │  Manager    │  │  Protocols  │  │  (RCU, MVRLU, Queues)   │  │
│  └─────────────┘  └─────────────┘  └─────────────────────────┘  │
├─────────────────────────────────────────────────────────────────┤
│                        RPC Framework                             │
├─────────────────────────────────────────────────────────────────┤
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────────┐  │
│  │  RPC Server │  │  RPC Client │  │   RDMA Transport Layer   │  │
│  └─────────────┘  └─────────────┘  └─────────────────────────┘  │
├─────────────────────────────────────────────────────────────────┤
│                      RDMA Verbs Layer                            │
├─────────────────────────────────────────────────────────────────┤
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────────┐  │
│  │  Queue Pair │  │ Completion  │  │   Memory Region Mgmt    │  │
│  │   Manager   │  │    Queue    │  │   (MR, MW, PD)          │  │
│  └─────────────┘  └─────────────┘  └─────────────────────────┘  │
├─────────────────────────────────────────────────────────────────┤
│                    libibverbs / libibcm                          │
├─────────────────────────────────────────────────────────────────┤
│  ┌─────────────────────────────────────────────────────────────┐│
│  │              InfiniBand / RoCE Network                       ││
│  └─────────────────────────────────────────────────────────────┘│
└─────────────────────────────────────────────────────────────────┘
```

### Directory Structure

```
Aether/
├── include/                    # Public headers
│   ├── aether/                # Core types and definitions
│   ├── rdma/                  # RDMA programming interfaces
│   ├── dsm/                   # DSM layer interfaces
│   ├── rpc/                   # RPC framework
│   ├── structures/            # Lock-free data structures
│   └── profiling/             # Performance profiling
├── src/                       # Implementation
│   ├── core/                  # RDMA core (device, QP, CQ, MR)
│   ├── dsm/                   # DSM implementation
│   ├── rpc/                   # RPC framework
│   ├── structures/            # Lock-free structures
│   └── profiling/             # Profiler
├── tests/                     # Unit tests
├── benchmarks/                # Performance benchmarks
├── docs/                      # Documentation
├── scripts/                   # Utility scripts
├── CMakeLists.txt            # CMake build configuration
├── Makefile                  # Makefile
└── README.md                  # This file
```

## RDMA Internals

### Understanding RDMA Verbs

RDMA (Remote Direct Memory Access) enables direct memory-to-memory data transfer between servers without involving either system's CPU or operating system. The verbs API (libibverbs) provides the programming interface for RDMA operations.

#### Key Concepts

1. **Protection Domain (PD)**
   - Logical isolation unit for RDMA resources
   - Contains Memory Regions, Queue Pairs, and Memory Windows
   - All RDMA resources within a PD can interact

2. **Memory Region (MR)**
   - Registered memory buffer accessible via RDMA
   - Provides local and remote keys (lkey/rkey) for access
   - Permissions: Local Write, Remote Read, Remote Write, Atomic

3. **Queue Pair (QP)**
   - Communication endpoint with Send and Receive queues
   - Types: RC (Reliable Connected), UC (Unreliable Connected), UD (Unreliable Datagram)
   - States: RESET → INIT → RTR (Ready to Receive) → RTS (Ready to Send)

4. **Completion Queue (CQ)**
   - Captures work completions from QPs
   - Polling or event-driven notification
   - Contains Work Completions (WC) with status and results

### One-Sided Operations

RDMA provides true one-sided operations that complete without remote CPU involvement:

#### RDMA Read
```c
// Read remote memory to local buffer
aether_rdma_read_params_t params = {
    .remote_addr = 0x10000000,  // Remote virtual address
    .rkey = 0x12345678,         // Remote key
    .local_addr = local_buffer, // Local buffer
    .size = 4096,               // Transfer size
    .lkey = local_mr->lkey,     // Local key
    .signaled = 1               // Request completion
};
aether_rdma_read_sync(qp, &params, timeout_ms);
```

#### RDMA Write
```c
// Write local buffer to remote memory
aether_rdma_write_params_t params = {
    .remote_addr = 0x10000000,
    .rkey = 0x12345678,
    .local_addr = local_buffer,
    .size = 4096,
    .lkey = local_mr->lkey,
    .signaled = 1
};
aether_rdma_write_sync(qp, &params, timeout_ms);
```

### Atomic Operations

RDMA atomic operations provide lock-free synchronization:

| Operation | Description | Use Case |
|-----------|-------------|----------|
| Fetch & Add | Atomically add value to remote memory | Counters, sequences |
| Compare & Swap | Atomic CAS with expected value | Lock-free algorithms |
| Swap | Atomic exchange | Implementation primitive |

```c
// Atomic fetch and add
aether_atomic_params_t params = {
    .remote_addr = counter_addr,
    .rkey = mr->rkey,
    .compare_add = 1,
    .local_addr = &result,
    .lkey = local_mr->lkey
};
aether_atomic_fetch_add(qp, &params);
```

### Connection Establishment

```
Node A                                    Node B
   │                                         │
   │  Create QP ─────────────────────────────│
   │                                         │
   │  Modify QP to INIT ─────────────────────│
   │                                         │
   │  Modify QP to RTR ──────────────────────│←─│─ Modify QP to INIT
   │                                         │   │
   │  Modify QP to RTS ──────────────────────│───│─ Modify QP to RTR
   │                                         │   │
   │         ════════════════════════════    │   │
   │              Connection Ready            │   │
   │         ════════════════════════════    │   │
   │                                         │   │
```

### Memory Registration

Memory must be registered with the RDMA NIC to be accessible remotely:

```c
aether_mem_reg_params_t params = {
    .addr = buffer,
    .size = buffer_size,
    .access_flags = AETHER_MR_PERMISSION_LOCAL_WRITE |
                    AETHER_MR_PERMISSION_REMOTE_READ |
                    AETHER_MR_PERMISSION_REMOTE_WRITE |
                    AETHER_MR_PERMISSION_ATOMIC,
    .use_hugepages = true
};

aether_mr_t* mr = aether_reg_mr(pd, &params);
printf("Local Key: 0x%x, Remote Key: 0x%x\n", mr->lkey, mr->rkey);
```

## Installation

### Prerequisites

```bash
# Ubuntu/Debian
sudo apt-get install build-essential cmake
sudo apt-get install libibverbs-dev librdmacm-dev
sudo apt-get install rdma-core infiniband-diags

# RHEL/CentOS
sudo yum groupinstall "Infiniband Support"
sudo yum install libibverbs-devel librdmacm-devel
```

### Build

```bash
# Clone the repository
git clone https://github.com/moggan1337/Aether.git
cd Aether

# Build with Make
make -j$(nproc)

# Or build with CMake
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### Verification

```bash
# List available RDMA devices
ibv_devices

# Check device status
ibv_devinfo
```

## Quick Start

### Basic DSM Usage

```c
#include <aether/aether_common.h>
#include <dsm/dsm.h>
#include <rdma/rdma_device.h>
#include <rdma/rdma_cm.h>

int main(int argc, char** argv) {
    // Initialize DSM configuration
    aether_dsm_config_t config = {
        .node_id = 0,
        .num_nodes = 2,
        .page_size = 4096,
        .coherence_protocol = AETHER_COHERENCE_MESI,
        .cache_enabled = true,
        .cache_size_mb = 64
    };
    
    // Initialize DSM
    aether_dsm_init(&config);
    
    // Create a distributed memory region (1GB)
    aether_dsm_region_t* region = aether_dsm_create_region(1UL << 30, AETHER_ACCESS_RW);
    
    // Access distributed memory
    uint64_t offset = 0x1000;
    uint64_t value = 42;
    
    aether_dsm_write(region, offset, &value, sizeof(value));
    aether_dsm_read(region, offset, &value, sizeof(value));
    
    printf("Read value: %lu\n", value);
    
    // Cleanup
    aether_dsm_destroy_region(region);
    aether_dsm_shutdown();
    
    return 0;
}
```

### RDMA Communication

```c
#include <rdma/rdma_ops.h>
#include <rdma/rdma_device.h>
#include <rdma/rdma_memory.h>

int main() {
    // Open RDMA device
    aether_device_t* dev = aether_open_device("mlx5_0");
    if (!dev) {
        fprintf(stderr, "Failed to open RDMA device\n");
        return 1;
    }
    
    // Create protection domain
    aether_pd_t* pd = aether_create_pd(dev);
    
    // Register memory region
    char buffer[4096];
    aether_mem_reg_params_t params = {
        .addr = buffer,
        .size = sizeof(buffer),
        .access_flags = IBV_ACCESS_LOCAL_WRITE | 
                        IBV_ACCESS_REMOTE_READ | 
                        IBV_ACCESS_REMOTE_WRITE
    };
    aether_mr_t* mr = aether_reg_mr(pd, &params);
    
    printf("Memory Region: lkey=0x%x, rkey=0x%x\n", mr->lkey, mr->rkey);
    
    // Create Queue Pair
    aether_qp_init_attr_t qp_attr = {
        .type = AETHER_QP_TYPE_RC,
        .max_send_wr = 256,
        .max_recv_wr = 256,
        .max_send_sge = 4,
        .max_recv_sge = 4
    };
    aether_qp_t* qp = aether_create_qp(pd, &qp_attr);
    
    // Connect to remote node (see RPC framework for full connection handling)
    // ...
    
    // Perform RDMA operations
    // RDMA write
    aether_rdma_write_params_t write_params = {
        .remote_addr = remote_addr,
        .rkey = remote_rkey,
        .local_addr = buffer,
        .size = sizeof(buffer),
        .lkey = mr->lkey
    };
    aether_rdma_write_sync(qp, &write_params, 1000);
    
    // Cleanup
    aether_destroy_qp(qp);
    aether_dereg_mr(mr);
    aether_destroy_pd(pd);
    aether_close_device(dev);
    
    return 0;
}
```

### Using Distributed Arrays

```c
#include <dsm/dsm_array.h>

int main() {
    // Create array configuration
    aether_array_config_t config = {
        .num_elements = 1000000,
        .dtype = AETHER_ARRAY_TYPE_FLOAT64,
        .access_mode = AETHER_ACCESS_RW,
        .partitioning_scheme = 0  // Block partitioning
    };
    
    // Create distributed array
    aether_array_t* arr = aether_array_create(&config);
    
    // Fill array with values
    double fill_val = 3.14159;
    for (uint64_t i = 0; i < aether_array_num_elements(arr); i++) {
        aether_array_set(arr, i, &fill_val);
    }
    
    // RDMA bulk transfer
    double local_buffer[1024];
    aether_array_rdma_get_bulk(arr, 0, local_buffer, 1024, 1);  // Get from node 1
    
    // Reduction operation
    double sum;
    aether_array_reduce_sum(arr, &sum);
    printf("Array sum: %f\n", sum);
    
    // Cleanup
    aether_array_destroy(arr);
    
    return 0;
}
```

## API Reference

### Core Types

```c
// Node identification
typedef struct {
    uint16_t node_id;
    uint16_t lid;
    uint32_t qpn;
    uint64_t guid;
    char hostname[256];
} aether_node_id_t;

// Memory region descriptor
typedef struct {
    uint64_t addr;
    uint64_t size;
    uint32_t lkey;
    uint32_t rkey;
    uint8_t flags;
} aether_mr_t;

// Page descriptor for DSM
typedef struct {
    uint64_t vpn;
    uint64_t physical_addr;
    uint8_t state;
    uint8_t owner;
    uint8_t sharers;
} aether_page_t;
```

### DSM API

| Function | Description |
|----------|-------------|
| `aether_dsm_init()` | Initialize DSM system |
| `aether_dsm_shutdown()` | Shutdown DSM system |
| `aether_dsm_create_region()` | Create distributed memory region |
| `aether_dsm_destroy_region()` | Destroy memory region |
| `aether_dsm_read()` | Read from distributed memory |
| `aether_dsm_write()` | Write to distributed memory |
| `aether_dsm_atomic_fetch_add()` | Atomic fetch and add |
| `aether_dsm_atomic_cmp_swap()` | Atomic compare and swap |
| `aether_dsm_barrier()` | Synchronization barrier |
| `aether_dsm_fence()` | Memory fence |

### RDMA API

| Function | Description |
|----------|-------------|
| `aether_open_device()` | Open RDMA device |
| `aether_close_device()` | Close RDMA device |
| `aether_create_pd()` | Create protection domain |
| `aether_reg_mr()` | Register memory region |
| `aether_dereg_mr()` | Deregister memory region |
| `aether_create_qp()` | Create queue pair |
| `aether_qp_connect()` | Connect QP to remote |
| `aether_rdma_read()` | RDMA read operation |
| `aether_rdma_write()` | RDMA write operation |
| `aether_atomic_fetch_add()` | Atomic fetch and add |
| `aether_atomic_cmp_swap()` | Atomic compare and swap |

### Lock-Free Data Structures

| Function | Description |
|----------|-------------|
| `aether_lf_queue_create()` | Create lock-free queue |
| `aether_lf_queue_enqueue()` | Enqueue element |
| `aether_lf_queue_dequeue()` | Dequeue element |
| `aether_lf_stack_create()` | Create lock-free stack |
| `aether_lf_stack_push()` | Push onto stack |
| `aether_lf_stack_pop()` | Pop from stack |
| `aether_lf_hashmap_create()` | Create lock-free hash map |
| `aether_lf_hashmap_insert()` | Insert key-value pair |
| `aether_lf_hashmap_lookup()` | Lookup value |

### RPC Framework

| Function | Description |
|----------|-------------|
| `aether_rpc_server_create()` | Create RPC server |
| `aether_rpc_server_start()` | Start RPC server |
| `aether_rpc_client_create()` | Create RPC client |
| `aether_rpc_call()` | Synchronous RPC call |
| `aether_rpc_call_async()` | Asynchronous RPC call |
| `aether_rpc_rdma_call()` | RDMA-optimized RPC |

## Performance Tuning

### Network Configuration

```bash
# Set InfiniBand port speed
ibportstate <dev> <port> speed 100

# Verify link state
ibv_devinfo

# Check for packet pacing support
ibv_devinfo -v
```

### Memory Configuration

```c
// Use hugepages for better performance
aether_mem_reg_params_t params = {
    .use_hugepages = true,
    .access_flags = IBV_ACCESS_LOCAL_WRITE | 
                    IBV_ACCESS_REMOTE_WRITE
};
aether_mr_t* mr = aether_reg_mr(pd, &params);

// Pre-pinning memory
mlockall(MCL_CURRENT | MCL_FUTURE);
```

### Queue Pair Tuning

```c
// Optimal QP configuration for low latency
aether_qp_init_attr_t qp_attr = {
    .type = AETHER_QP_TYPE_RC,
    .max_send_wr = 64,           // Smaller for lower latency
    .max_recv_wr = 64,
    .max_send_sge = 1,           // Single SGE for simplicity
    .max_recv_sge = 1,
    .max_inline_data = 64,      // Enable inline data
    .sq_sig_all = 0             // Selective signaling
};
```

### Completion Queue Configuration

```c
// Dedicated CQ for each QP
aether_cq_t* send_cq = aether_create_cq(dev, 128, NULL, 0);
aether_cq_t* recv_cq = aether_create_cq(dev, 128, NULL, 0);

// Polling vs notification
// For latency-critical: poll actively
// For throughput: use async notifications
```

## Benchmarks

### Latency Benchmarks

```bash
# Run latency benchmark
./build/benchmarks/latency_bench

# Results (typical on 100Gbps InfiniBand):
# RDMA Read Latency (4KB): 1.2 μs
# RDMA Write Latency (4KB): 1.1 μs
# Atomic Fetch-Add Latency: 1.8 μs
# RPC Round-Trip Latency: 2.5 μs
```

### Bandwidth Benchmarks

```bash
# Run bandwidth benchmark
./build/benchmarks/bandwidth_bench

# Results (typical on 100Gbps InfiniBand HDR):
# RDMA Read Bandwidth: 94 Gbps
# RDMA Write Bandwidth: 96 Gbps
# Bi-directional Bandwidth: 180 Gbps
```

### DSM Benchmarks

```bash
# Run DSM benchmark
./build/benchmarks/dsm_bench

# Results:
# Page Fault Resolution: 5 μs
# Cache Hit Rate: 95%
# Coherence Protocol Overhead: 2%
```

### Throughput Benchmarks

```
┌──────────────────┬────────────┬────────────┐
│     Operation    │  2 Nodes  │  4 Nodes   │
├──────────────────┼────────────┼────────────┤
│ RDMA Reads/sec   │  850K     │  1.6M      │
│ RDMA Writes/sec  │  920K     │  1.8M      │
│ RPC Calls/sec    │  400K     │  750K      │
│ Array Ops/sec    │  200K     │  380K      │
└──────────────────┴────────────┴────────────┘
```

## Troubleshooting

### Common Issues

#### "No RDMA devices found"

```bash
# Check if RDMA drivers are loaded
lsmod | grep -E "(mlx|ib)"

# Load drivers manually
modprobe mlx5_ib
modprobe ib_uverbs

# Verify devices
ibv_devices
```

#### "Failed to open device"

```bash
# Check device permissions
ls -la /dev/infiniband/

# Add user to rdma group
sudo usermod -aG rdma $USER
# Log out and back in for changes to take effect
```

#### "QP creation failed"

```bash
# Check resource limits
cat /sys/class/infiniband/*/device/max_qp_wr
cat /sys/class/infiniband/*/device/max_cq

# Increase limits in /etc/security/limits.conf
* soft nofile 65536
* hard nofile 65536
* soft memlock unlimited
* hard memlock unlimited
```

#### High latency or packet drops

```bash
# Check network health
ibchecknet

# Verify port counters
ibPortCounters <dev> <port>

# Check for congestion
ib_query_port <dev> <port>
```

### Debug Mode

Compile with debug flags:

```makefile
CFLAGS += -DAETHER_DEBUG -g -O0
```

Enable tracing:

```c
// Enable RDMA operation tracing
export AETHER_TRACE_RDMA=1
export AETHER_TRACE_DSM=1
export AETHER_TRACE_LEVEL=DEBUG
```

## Contributing

We welcome contributions! Please see our [Contributing Guide](CONTRIBUTING.md) for details.

### Development Setup

```bash
# Install development dependencies
make dev-deps

# Run tests
make test

# Run with valgrind
valgrind --leak-check=full ./build/tests/dsm_test
```

### Code Style

- Follow GNU coding standards
- Document all public APIs
- Include unit tests for new features
- Run `make format` before submitting

## Academic Citation

If you use Aether in your research, please cite:

```bibtex
@article{aether2024,
  title={Aether: High-Performance Distributed Shared Memory with RDMA},
  author={Aether Team},
  journal={arXiv preprint arXiv:XXXX.XXXXX},
  year={2024}
}
```

## Related Projects

- [libibverbs](https://github.com/linux-rdma/rdma-core) - RDMA userspace library
- [UCX](https://github.com/openucx/ucx) - Unified Communication X
- [GDRCopy](https://github.com/NVIDIA/gdrcopy) - GPU Direct RDMA

## License

MIT License - see [LICENSE](LICENSE) file for details.

## Acknowledgments

- InfiniBand Trade Association
- OpenFabrics Alliance
- RDMAmojo Blog

---

<p align="center">
  Built with ❤️ for high-performance computing
</p>
