/**
 * Aether - Performance Profiling Tools
 * Latency tracking, bandwidth measurement, and performance analysis
 *
 * Copyright (c) 2024 Aether Authors
 */

#ifndef AETHER_PROFILER_H
#define AETHER_PROFILER_H

#include <aether/aether_common.h>
#include <pthread.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

// Metric Types
typedef enum {
    AETHER_METRIC_LATENCY = 0x01,
    AETHER_METRIC_BANDWIDTH = 0x02,
    AETHER_METRIC_THROUGHPUT = 0x03,
    AETHER_METRIC_COUNTER = 0x04,
    AETHER_METRIC_GAUGE = 0x05,
    AETHER_METRIC_HISTOGRAM = 0x06,
    AETHER_METRIC_PERCENTILE = 0x07
} aether_metric_type_t;

// Metric Aggregation
typedef enum {
    AETHER_AGG_NONE = 0x00,
    AETHER_AGG_SUM = 0x01,
    AETHER_AGG_AVG = 0x02,
    AETHER_AGG_MIN = 0x03,
    AETHER_AGG_MAX = 0x04,
    AETHER_AGG_COUNT = 0x05
} aether_aggregation_t;

// Time Sources
typedef enum {
    AETHER_CLOCK_MONOTONIC = 0x01,
    AETHER_CLOCK_MONOTONIC_RAW = 0x02,
    AETHER_CLOCK_THREAD_CPUTIME = 0x03,
    AETHER_CLOCK_PROCESS_CPUTIME = 0x04
} aether_clock_source_t;

// Statistical Summary
typedef struct {
    uint64_t count;
    double sum;
    double sum_sq;
    double min;
    double max;
    double mean;
    double variance;
    double stddev;
    double p50;
    double p90;
    double p95;
    double p99;
    double p999;
} aether_stats_t;

// Time Interval
typedef struct {
    uint64_t start_ns;
    uint64_t end_ns;
    uint64_t duration_ns;
} aether_interval_t;

// Metric Value
typedef union {
    uint64_t counter;
    double gauge;
    aether_interval_t interval;
    aether_stats_t stats;
} aether_metric_value_t;

// Metric Definition
typedef struct aether_metric {
    char name[128];
    char description[256];
    aether_metric_type_t type;
    aether_aggregation_t aggregation;
    aether_metric_value_t value;
    uint64_t timestamp;
    
    // Histogram buckets (for histogram type)
    uint64_t* histogram_buckets;
    size_t num_buckets;
    double bucket_width;
    
    // Atomic access for concurrent updates
    _Atomic(uint64_t) raw_count;
    _Atomic(uint64_t) raw_sum;
    _Atomic(uint64_t) raw_min;
    _Atomic(uint64_t) raw_max;
    
    struct aether_metric* next;
} aether_metric_t;

// Performance Counter Group
typedef struct {
    char name[64];
    char description[128];
    
    // Counters
    uint64_t rdma_reads;
    uint64_t rdma_writes;
    uint64_t rdma_atomics;
    uint64_t rpc_calls;
    uint64_t rpc_replies;
    uint64_t page_faults;
    uint64_t cache_hits;
    uint64_t cache_misses;
    uint64_t lock_contentions;
    
    // Bandwidth
    uint64_t bytes_sent;
    uint64_t bytes_received;
    uint64_t total_bytes_transferred;
    
    // Latency (in cycles)
    uint64_t total_latency_cycles;
    uint64_t min_latency_cycles;
    uint64_t max_latency_cycles;
    
    // Timing
    uint64_t start_time;
    uint64_t end_time;
    uint64_t elapsed_ns;
    
    // Thread-safe updates
    pthread_mutex_t lock;
} aether_counter_group_t;

// Trace Event
typedef struct {
    uint64_t timestamp_ns;
    uint32_t event_type;
    uint16_t node_id;
    uint16_t thread_id;
    uint64_t duration_ns;
    uint64_t data[4];
    char name[64];
} aether_trace_event_t;

// Trace Buffer
typedef struct {
    aether_trace_event_t* events;
    size_t capacity;
    size_t count;
    _Atomic(size_t) head;
    pthread_spinlock_t lock;
} aether_trace_buffer_t;

// Profiler Handle
typedef struct aether_profiler aether_profiler_t;

// Profiler Configuration
typedef struct {
    aether_clock_source_t clock_source;
    size_t metric_capacity;
    size_t trace_capacity;
    size_t histogram_buckets;
    uint32_t sampling_rate;
    bool enable_tracing;
    bool enable_metrics;
    bool enable_histograms;
    char* output_dir;
} aether_profiler_config_t;

// ============================================================================
// Profiler Operations
// ============================================================================

aether_profiler_t* aether_profiler_create(const aether_profiler_config_t* config);
aether_result_t aether_profiler_destroy(aether_profiler_t* profiler);

// Metrics
aether_metric_t* aether_metric_register(aether_profiler_t* profiler, const char* name,
                                         const char* description, aether_metric_type_t type);
aether_result_t aether_metric_update_counter(aether_profiler_t* profiler, const char* name, int64_t delta);
aether_result_t aether_metric_update_gauge(aether_profiler_t* profiler, const char* name, double value);
aether_result_t aether_metric_record_latency(aether_profiler_t* profiler, const char* name, uint64_t ns);

// Interval timing
aether_interval_t aether_begin_interval(aether_profiler_t* profiler);
void aether_end_interval(aether_profiler_t* profiler, const char* name, aether_interval_t* interval);

// Counter groups
aether_counter_group_t* aether_counter_group_create(const char* name, const char* description);
aether_result_t aether_counter_group_destroy(aether_counter_group_t* group);
aether_result_t aether_counter_group_increment(aether_counter_group_t* group, const char* counter);
aether_result_t aether_counter_group_add(aether_counter_group_t* group, const char* counter, uint64_t value);

// Trace
aether_result_t aether_trace_event(aether_profiler_t* profiler, uint32_t event_type,
                                     const char* name, uint64_t duration_ns, uint64_t* data);
aether_result_t aether_trace_mark(aether_profiler_t* profiler, uint32_t event_type, const char* name);

// Statistics computation
aether_result_t aether_compute_stats(aether_stats_t* stats, const uint64_t* samples, size_t count);
aether_result_t aether_update_stats(aether_stats_t* stats, uint64_t value);

// Report generation
aether_result_t aether_profiler_report(aether_profiler_t* profiler, FILE* output);
aether_result_t aether_profiler_export_json(aether_profiler_t* profiler, const char* filename);
aether_result_t aether_profiler_export_csv(aether_profiler_t* profiler, const char* filename);

// Reset
aether_result_t aether_profiler_reset(aether_profiler_t* profiler);

// ============================================================================
// Utility Functions
// ============================================================================

static inline uint64_t aether_get_time_ns(aether_clock_source_t clock) {
    struct timespec ts;
    clockid_t clk_id;
    
    switch (clock) {
        case AETHER_CLOCK_MONOTONIC:
            clk_id = CLOCK_MONOTONIC;
            break;
        case AETHER_CLOCK_MONOTONIC_RAW:
            clk_id = CLOCK_MONOTONIC_RAW;
            break;
        case AETHER_CLOCK_THREAD_CPUTIME:
            clk_id = CLOCK_THREAD_CPUTIME_ID;
            break;
        case AETHER_CLOCK_PROCESS_CPUTIME:
            clk_id = CLOCK_PROCESS_CPUTIME_ID;
            break;
        default:
            clk_id = CLOCK_MONOTONIC;
    }
    
    clock_gettime(clk_id, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

static inline uint64_t aether_calc_latency_ns(uint64_t start, uint64_t end) {
    return end > start ? end - start : 0;
}

// Convert cycles to nanoseconds
static inline uint64_t aether_cycles_to_ns(uint64_t cycles, uint64_t freq_hz) {
    return (cycles * 1000000000ULL) / freq_hz;
}

// ============================================================================
// Benchmarking Utilities
// ============================================================================

typedef struct {
    const char* name;
    uint64_t iterations;
    uint64_t total_ns;
    uint64_t min_ns;
    uint64_t max_ns;
    double mean_ns;
    double stddev_ns;
    double ops_per_sec;
    double mb_per_sec;
} aether_benchmark_result_t;

aether_benchmark_result_t aether_run_benchmark(const char* name,
                                                 uint64_t iterations,
                                                 uint64_t (*func)(void*),
                                                 void* arg);

#ifdef __cplusplus
}
#endif

#endif // AETHER_PROFILER_H
