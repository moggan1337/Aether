/**
 * Aether - Performance Profiling Implementation
 * Latency tracking, bandwidth measurement, and performance analysis
 *
 * Copyright (c) 2024 Aether Authors
 */

#include <profiling/profiler.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

struct aether_profiler {
    aether_profiler_config_t config;
    
    // Metrics
    aether_metric_t* metrics;
    size_t metric_count;
    pthread_mutex_t metrics_lock;
    
    // Trace buffer
    aether_trace_buffer_t trace;
    pthread_mutex_t trace_lock;
    
    // Statistics
    aether_stats_t global_stats;
    
    // System info
    uint64_t cpu_freq_hz;
};

static double percentile_value(double* sorted, size_t n, double p) {
    if (n == 0) return 0;
    if (n == 1) return sorted[0];
    
    double pos = p * (n - 1);
    size_t idx = (size_t)pos;
    double frac = pos - idx;
    
    if (idx + 1 < n) {
        return sorted[idx] * (1 - frac) + sorted[idx + 1] * frac;
    }
    return sorted[idx];
}

static int compare_double(const void* a, const void* b) {
    double da = *(const double*)a;
    double db = *(const double*)b;
    if (da < db) return -1;
    if (da > db) return 1;
    return 0;
}

aether_profiler_t* aether_profiler_create(const aether_profiler_config_t* config) {
    aether_profiler_t* profiler = (aether_profiler_t*)calloc(1, sizeof(aether_profiler_t));
    if (!profiler) {
        return NULL;
    }
    
    if (config) {
        profiler->config = *config;
    } else {
        profiler->config.enable_metrics = true;
        profiler->config.enable_tracing = true;
        profiler->config.enable_histograms = true;
        profiler->config.histogram_buckets = 64;
        profiler->config.sampling_rate = 1;
    }
    
    pthread_mutex_init(&profiler->metrics_lock, NULL);
    pthread_mutex_init(&profiler->trace_lock, NULL);
    
    // Initialize trace buffer
    profiler->trace.capacity = profiler->config.trace_capacity ? 
                                profiler->config.trace_capacity : 65536;
    profiler->trace.events = (aether_trace_event_t*)calloc(
        profiler->trace.capacity, sizeof(aether_trace_event_t));
    
    if (!profiler->trace.events) {
        pthread_mutex_destroy(&profiler->metrics_lock);
        pthread_mutex_destroy(&profiler->trace_lock);
        free(profiler);
        return NULL;
    }
    
    // Get CPU frequency
    FILE* fp = fopen("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq", "r");
    if (fp) {
        long freq;
        if (fscanf(fp, "%ld", &freq) == 1) {
            profiler->cpu_freq_hz = freq * 1000;
        }
        fclose(fp);
    }
    
    if (profiler->cpu_freq_hz == 0) {
        profiler->cpu_freq_hz = 2500000000ULL; // Default 2.5 GHz
    }
    
    return profiler;
}

aether_result_t aether_profiler_destroy(aether_profiler_t* profiler) {
    if (!profiler) {
        return AETHER_ERR_INVALID_PARAM;
    }
    
    // Free metrics
    pthread_mutex_lock(&profiler->metrics_lock);
    aether_metric_t* m = profiler->metrics;
    while (m) {
        aether_metric_t* next = m->next;
        if (m->histogram_buckets) {
            free(m->histogram_buckets);
        }
        free(m);
        m = next;
    }
    pthread_mutex_unlock(&profiler->metrics_lock);
    
    // Free trace buffer
    free(profiler->trace.events);
    
    pthread_mutex_destroy(&profiler->metrics_lock);
    pthread_mutex_destroy(&profiler->trace_lock);
    free(profiler);
    
    return AETHER_SUCCESS;
}

// Metrics
aether_metric_t* aether_metric_register(aether_profiler_t* profiler, const char* name,
                                         const char* description, aether_metric_type_t type) {
    if (!profiler || !name) {
        return NULL;
    }
    
    pthread_mutex_lock(&profiler->metrics_lock);
    
    // Check if metric already exists
    aether_metric_t* m = profiler->metrics;
    while (m) {
        if (strcmp(m->name, name) == 0) {
            pthread_mutex_unlock(&profiler->metrics_lock);
            return m;
        }
        m = m->next;
    }
    
    // Create new metric
    m = (aether_metric_t*)calloc(1, sizeof(aether_metric_t));
    if (!m) {
        pthread_mutex_unlock(&profiler->metrics_lock);
        return NULL;
    }
    
    strncpy(m->name, name, sizeof(m->name) - 1);
    if (description) {
        strncpy(m->description, description, sizeof(m->description) - 1);
    }
    m->type = type;
    m->timestamp = aether_get_time_ns(AETHER_CLOCK_MONOTONIC);
    
    // Initialize histogram if enabled
    if (type == AETHER_METRIC_HISTOGRAM && profiler->config.enable_histograms) {
        m->num_buckets = profiler->config.histogram_buckets;
        m->histogram_buckets = (uint64_t*)calloc(m->num_buckets, sizeof(uint64_t));
        m->bucket_width = 1000.0; // 1us default bucket width
    }
    
    // Add to list
    m->next = profiler->metrics;
    profiler->metrics = m;
    profiler->metric_count++;
    
    pthread_mutex_unlock(&profiler->metrics_lock);
    
    return m;
}

aether_result_t aether_metric_update_counter(aether_profiler_t* profiler, const char* name, int64_t delta) {
    if (!profiler || !name) {
        return AETHER_ERR_INVALID_PARAM;
    }
    
    pthread_mutex_lock(&profiler->metrics_lock);
    
    aether_metric_t* m = profiler->metrics;
    while (m) {
        if (strcmp(m->name, name) == 0) {
            if (m->type == AETHER_METRIC_COUNTER || m->type == AETHER_METRIC_THROUGHPUT) {
                atomic_fetch_add_explicit(&m->raw_count, delta, memory_order_relaxed);
                m->value.counter += delta;
            }
            pthread_mutex_unlock(&profiler->metrics_lock);
            return AETHER_SUCCESS;
        }
        m = m->next;
    }
    
    pthread_mutex_unlock(&profiler->metrics_lock);
    
    // Auto-register if not found
    m = aether_metric_register(profiler, name, NULL, AETHER_METRIC_COUNTER);
    if (m) {
        return aether_metric_update_counter(profiler, name, delta);
    }
    
    return AETHER_ERR_INVALID_PARAM;
}

aether_result_t aether_metric_update_gauge(aether_profiler_t* profiler, const char* name, double value) {
    if (!profiler || !name) {
        return AETHER_ERR_INVALID_PARAM;
    }
    
    pthread_mutex_lock(&profiler->metrics_lock);
    
    aether_metric_t* m = profiler->metrics;
    while (m) {
        if (strcmp(m->name, name) == 0) {
            if (m->type == AETHER_METRIC_GAUGE) {
                m->value.gauge = value;
            }
            pthread_mutex_unlock(&profiler->metrics_lock);
            return AETHER_SUCCESS;
        }
        m = m->next;
    }
    
    pthread_mutex_unlock(&profiler->metrics_lock);
    
    return AETHER_ERR_INVALID_PARAM;
}

aether_result_t aether_metric_record_latency(aether_profiler_t* profiler, const char* name, uint64_t ns) {
    if (!profiler || !name) {
        return AETHER_ERR_INVALID_PARAM;
    }
    
    pthread_mutex_lock(&profiler->metrics_lock);
    
    // Find or create metric
    aether_metric_t* m = profiler->metrics;
    while (m) {
        if (strcmp(m->name, name) == 0) {
            break;
        }
        m = m->next;
    }
    
    if (!m) {
        m = aether_metric_register(profiler, name, NULL, AETHER_METRIC_LATENCY);
        if (!m) {
            pthread_mutex_unlock(&profiler->metrics_lock);
            return AETHER_ERR_OOM;
        }
    }
    
    // Update statistics
    m->value.stats.count++;
    m->value.stats.sum += ns;
    m->value.stats.sum_sq += (double)ns * ns;
    
    if (ns < m->value.stats.min || m->value.stats.count == 1) {
        m->value.stats.min = ns;
    }
    if (ns > m->value.stats.max) {
        m->value.stats.max = ns;
    }
    
    // Update histogram
    if (m->histogram_buckets && m->num_buckets > 0) {
        size_t bucket = (size_t)(ns / m->bucket_width);
        if (bucket >= m->num_buckets) {
            bucket = m->num_buckets - 1;
        }
        m->histogram_buckets[bucket]++;
    }
    
    pthread_mutex_unlock(&profiler->metrics_lock);
    
    return AETHER_SUCCESS;
}

// Interval timing
aether_interval_t aether_begin_interval(aether_profiler_t* profiler) {
    aether_interval_t interval;
    interval.start_ns = aether_get_time_ns(profiler ? 
            profiler->config.clock_source : AETHER_CLOCK_MONOTONIC);
    interval.end_ns = 0;
    interval.duration_ns = 0;
    return interval;
}

void aether_end_interval(aether_profiler_t* profiler, const char* name, aether_interval_t* interval) {
    if (!interval) {
        return;
    }
    
    aether_clock_source_t clock = AETHER_CLOCK_MONOTONIC;
    if (profiler) {
        clock = profiler->config.clock_source;
    }
    
    interval->end_ns = aether_get_time_ns(clock);
    interval->duration_ns = interval->end_ns - interval->start_ns;
    
    if (name) {
        aether_metric_record_latency(profiler, name, interval->duration_ns);
    }
}

// Counter groups
aether_counter_group_t* aether_counter_group_create(const char* name, const char* description) {
    aether_counter_group_t* group = (aether_counter_group_t*)calloc(1, sizeof(aether_counter_group_t));
    if (!group) {
        return NULL;
    }
    
    if (name) {
        strncpy(group->name, name, sizeof(group->name) - 1);
    }
    if (description) {
        strncpy(group->description, description, sizeof(group->description) - 1);
    }
    
    group->start_time = aether_get_time_ns(AETHER_CLOCK_MONOTONIC);
    group->min_latency_cycles = UINT64_MAX;
    
    pthread_mutex_init(&group->lock, NULL);
    
    return group;
}

aether_result_t aether_counter_group_destroy(aether_counter_group_t* group) {
    if (!group) {
        return AETHER_ERR_INVALID_PARAM;
    }
    
    pthread_mutex_destroy(&group->lock);
    free(group);
    
    return AETHER_SUCCESS;
}

aether_result_t aether_counter_group_increment(aether_counter_group_t* group, const char* counter) {
    if (!group || !counter) {
        return AETHER_ERR_INVALID_PARAM;
    }
    
    pthread_mutex_lock(&group->lock);
    
    if (strcmp(counter, "rdma_reads") == 0) {
        group->rdma_reads++;
    } else if (strcmp(counter, "rdma_writes") == 0) {
        group->rdma_writes++;
    } else if (strcmp(counter, "rdma_atomics") == 0) {
        group->rdma_atomics++;
    } else if (strcmp(counter, "rpc_calls") == 0) {
        group->rpc_calls++;
    } else if (strcmp(counter, "rpc_replies") == 0) {
        group->rpc_replies++;
    } else if (strcmp(counter, "page_faults") == 0) {
        group->page_faults++;
    } else if (strcmp(counter, "cache_hits") == 0) {
        group->cache_hits++;
    } else if (strcmp(counter, "cache_misses") == 0) {
        group->cache_misses++;
    } else if (strcmp(counter, "lock_contentions") == 0) {
        group->lock_contentions++;
    }
    
    pthread_mutex_unlock(&group->lock);
    
    return AETHER_SUCCESS;
}

aether_result_t aether_counter_group_add(aether_counter_group_t* group, const char* counter, uint64_t value) {
    if (!group || !counter) {
        return AETHER_ERR_INVALID_PARAM;
    }
    
    pthread_mutex_lock(&group->lock);
    
    if (strcmp(counter, "bytes_sent") == 0) {
        group->bytes_sent += value;
    } else if (strcmp(counter, "bytes_received") == 0) {
        group->bytes_received += value;
    } else if (strcmp(counter, "total_bytes") == 0) {
        group->total_bytes_transferred += value;
    }
    
    pthread_mutex_unlock(&group->lock);
    
    return AETHER_SUCCESS;
}

// Trace
aether_result_t aether_trace_event(aether_profiler_t* profiler, uint32_t event_type,
                                     const char* name, uint64_t duration_ns, uint64_t* data) {
    if (!profiler || !name) {
        return AETHER_ERR_INVALID_PARAM;
    }
    
    pthread_mutex_lock(&profiler->trace_lock);
    
    size_t idx = profiler->trace.count % profiler->trace.capacity;
    aether_trace_event_t* event = &profiler->trace.events[idx];
    
    event->timestamp_ns = aether_get_time_ns(AETHER_CLOCK_MONOTONIC);
    event->event_type = event_type;
    event->thread_id = (uint16_t)pthread_self();
    event->duration_ns = duration_ns;
    
    strncpy(event->name, name, sizeof(event->name) - 1);
    
    if (data) {
        memcpy(event->data, data, sizeof(event->data));
    }
    
    profiler->trace.count++;
    
    pthread_mutex_unlock(&profiler->trace_lock);
    
    return AETHER_SUCCESS;
}

aether_result_t aether_trace_mark(aether_profiler_t* profiler, uint32_t event_type, const char* name) {
    return aether_trace_event(profiler, event_type, name, 0, NULL);
}

// Statistics computation
aether_result_t aether_compute_stats(aether_stats_t* stats, const uint64_t* samples, size_t count) {
    if (!stats || !samples || count == 0) {
        return AETHER_ERR_INVALID_PARAM;
    }
    
    // Copy samples for sorting
    double* sorted = (double*)malloc(count * sizeof(double));
    if (!sorted) {
        return AETHER_ERR_OOM;
    }
    
    stats->count = count;
    stats->sum = 0;
    stats->sum_sq = 0;
    stats->min = samples[0];
    stats->max = samples[0];
    
    for (size_t i = 0; i < count; i++) {
        double val = (double)samples[i];
        sorted[i] = val;
        stats->sum += val;
        stats->sum_sq += val * val;
        if (val < stats->min) stats->min = val;
        if (val > stats->max) stats->max = val;
    }
    
    stats->mean = stats->sum / count;
    double variance = (stats->sum_sq / count) - (stats->mean * stats->mean);
    stats->variance = variance >= 0 ? variance : 0;
    stats->stddev = sqrt(stats->variance);
    
    // Sort for percentiles
    qsort(sorted, count, sizeof(double), compare_double);
    
    stats->p50 = percentile_value(sorted, count, 0.50);
    stats->p90 = percentile_value(sorted, count, 0.90);
    stats->p95 = percentile_value(sorted, count, 0.95);
    stats->p99 = percentile_value(sorted, count, 0.99);
    stats->p999 = percentile_value(sorted, count, 0.999);
    
    free(sorted);
    
    return AETHER_SUCCESS;
}

aether_result_t aether_update_stats(aether_stats_t* stats, uint64_t value) {
    if (!stats) {
        return AETHER_ERR_INVALID_PARAM;
    }
    
    stats->count++;
    stats->sum += value;
    stats->sum_sq += (double)value * value;
    
    if (value < stats->min || stats->count == 1) {
        stats->min = value;
    }
    if (value > stats->max) {
        stats->max = value;
    }
    
    stats->mean = stats->sum / stats->count;
    double variance = (stats->sum_sq / stats->count) - (stats->mean * stats->mean);
    stats->variance = variance >= 0 ? variance : 0;
    stats->stddev = sqrt(stats->variance);
    
    return AETHER_SUCCESS;
}

// Report generation
aether_result_t aether_profiler_report(aether_profiler_t* profiler, FILE* output) {
    if (!profiler || !output) {
        return AETHER_ERR_INVALID_PARAM;
    }
    
    fprintf(output, "=== Aether Performance Report ===\n\n");
    
    pthread_mutex_lock(&profiler->metrics_lock);
    
    fprintf(output, "Metrics (%zu total):\n", profiler->metric_count);
    fprintf(output, "-------------------------------\n");
    
    aether_metric_t* m = profiler->metrics;
    while (m) {
        fprintf(output, "\n%s (%s):\n", m->name, m->description);
        
        switch (m->type) {
            case AETHER_METRIC_COUNTER:
                fprintf(output, "  Type: Counter\n");
                fprintf(output, "  Value: %lu\n", (unsigned long)m->value.counter);
                break;
                
            case AETHER_METRIC_GAUGE:
                fprintf(output, "  Type: Gauge\n");
                fprintf(output, "  Value: %.2f\n", m->value.gauge);
                break;
                
            case AETHER_METRIC_LATENCY:
            case AETHER_METRIC_HISTOGRAM:
            case AETHER_METRIC_PERCENTILE:
                if (m->value.stats.count > 0) {
                    fprintf(output, "  Count: %lu\n", (unsigned long)m->value.stats.count);
                    fprintf(output, "  Mean: %.2f ns\n", m->value.stats.mean);
                    fprintf(output, "  StdDev: %.2f ns\n", m->value.stats.stddev);
                    fprintf(output, "  Min: %lu ns\n", (unsigned long)m->value.stats.min);
                    fprintf(output, "  Max: %lu ns\n", (unsigned long)m->value.stats.max);
                    fprintf(output, "  P50: %.2f ns\n", m->value.stats.p50);
                    fprintf(output, "  P90: %.2f ns\n", m->value.stats.p90);
                    fprintf(output, "  P95: %.2f ns\n", m->value.stats.p95);
                    fprintf(output, "  P99: %.2f ns\n", m->value.stats.p99);
                }
                break;
                
            default:
                fprintf(output, "  Type: Unknown\n");
        }
        
        m = m->next;
    }
    
    pthread_mutex_unlock(&profiler->metrics_lock);
    
    fprintf(output, "\nTrace Events: %zu\n", (size_t)profiler->trace.count);
    
    return AETHER_SUCCESS;
}

aether_result_t aether_profiler_export_json(aether_profiler_t* profiler, const char* filename) {
    if (!profiler || !filename) {
        return AETHER_ERR_INVALID_PARAM;
    }
    
    FILE* fp = fopen(filename, "w");
    if (!fp) {
        return AETHER_ERR_INVALID_PARAM;
    }
    
    fprintf(fp, "{\n");
    fprintf(fp, "  \"profiler\": \"Aether\",\n");
    fprintf(fp, "  \"timestamp\": %lu,\n", (unsigned long)time(NULL));
    
    pthread_mutex_lock(&profiler->metrics_lock);
    
    fprintf(fp, "  \"metrics\": [\n");
    
    aether_metric_t* m = profiler->metrics;
    bool first = true;
    while (m) {
        if (!first) fprintf(fp, ",\n");
        first = false;
        
        fprintf(fp, "    {\"name\": \"%s\", ", m->name);
        fprintf(fp, "\"type\": %d", m->type);
        
        if (m->type == AETHER_METRIC_COUNTER) {
            fprintf(fp, ", \"value\": %lu}", (unsigned long)m->value.counter);
        } else if (m->type == AETHER_METRIC_GAUGE) {
            fprintf(fp, ", \"value\": %.2f}", m->value.gauge);
        } else if (m->value.stats.count > 0) {
            fprintf(fp, ", \"count\": %lu, \"mean\": %.2f, \"p99\": %.2f}",
                    (unsigned long)m->value.stats.count,
                    m->value.stats.mean, m->value.stats.p99);
        } else {
            fprintf(fp, "}");
        }
        
        m = m->next;
    }
    
    fprintf(fp, "\n  ],\n");
    fprintf(fp, "  \"trace_count\": %zu\n", (size_t)profiler->trace.count);
    fprintf(fp, "}\n");
    
    pthread_mutex_unlock(&profiler->metrics_lock);
    
    fclose(fp);
    
    return AETHER_SUCCESS;
}

aether_result_t aether_profiler_export_csv(aether_profiler_t* profiler, const char* filename) {
    if (!profiler || !filename) {
        return AETHER_ERR_INVALID_PARAM;
    }
    
    FILE* fp = fopen(filename, "w");
    if (!fp) {
        return AETHER_ERR_INVALID_PARAM;
    }
    
    fprintf(fp, "name,type,count,mean,stddev,min,max,p50,p90,p95,p99\n");
    
    pthread_mutex_lock(&profiler->metrics_lock);
    
    aether_metric_t* m = profiler->metrics;
    while (m) {
        fprintf(fp, "%s,%d", m->name, m->type);
        
        if (m->type == AETHER_METRIC_COUNTER) {
            fprintf(fp, ",%lu,,,,,,,,\n", (unsigned long)m->value.counter);
        } else if (m->type == AETHER_METRIC_GAUGE) {
            fprintf(fp, ",,%.2f,,,,,,,,\n", m->value.gauge);
        } else if (m->value.stats.count > 0) {
            fprintf(fp, ",%lu,%.2f,%.2f,%lu,%lu,%.2f,%.2f,%.2f,%.2f\n",
                    (unsigned long)m->value.stats.count,
                    m->value.stats.mean, m->value.stats.stddev,
                    (unsigned long)m->value.stats.min,
                    (unsigned long)m->value.stats.max,
                    m->value.stats.p50, m->value.stats.p90,
                    m->value.stats.p95, m->value.stats.p99);
        } else {
            fprintf(fp, "\n");
        }
        
        m = m->next;
    }
    
    pthread_mutex_unlock(&profiler->metrics_lock);
    
    fclose(fp);
    
    return AETHER_SUCCESS;
}

aether_result_t aether_profiler_reset(aether_profiler_t* profiler) {
    if (!profiler) {
        return AETHER_ERR_INVALID_PARAM;
    }
    
    pthread_mutex_lock(&profiler->metrics_lock);
    
    aether_metric_t* m = profiler->metrics;
    while (m) {
        memset(&m->value, 0, sizeof(m->value));
        m->timestamp = aether_get_time_ns(AETHER_CLOCK_MONOTONIC);
        
        if (m->histogram_buckets) {
            memset(m->histogram_buckets, 0, m->num_buckets * sizeof(uint64_t));
        }
        
        m = m->next;
    }
    
    profiler->trace.count = 0;
    
    pthread_mutex_unlock(&profiler->metrics_lock);
    
    return AETHER_SUCCESS;
}

// Benchmarking
aether_benchmark_result_t aether_run_benchmark(const char* name,
                                                 uint64_t iterations,
                                                 uint64_t (*func)(void*),
                                                 void* arg) {
    aether_benchmark_result_t result = {0};
    
    if (!name || !func) {
        return result;
    }
    
    result.name = name;
    result.iterations = iterations;
    result.min_ns = UINT64_MAX;
    
    // Warm up
    for (uint64_t i = 0; i < 100 && i < iterations; i++) {
        func(arg);
    }
    
    // Run benchmark
    uint64_t total_ns = 0;
    
    for (uint64_t i = 0; i < iterations; i++) {
        uint64_t start = aether_get_time_ns(AETHER_CLOCK_MONOTONIC);
        uint64_t duration = func(arg);
        uint64_t end = aether_get_time_ns(AETHER_CLOCK_MONOTONIC);
        
        uint64_t ns = duration ? duration : (end - start);
        total_ns += ns;
        
        if (ns < result.min_ns) result.min_ns = ns;
        if (ns > result.max_ns) result.max_ns = ns;
    }
    
    result.total_ns = total_ns;
    result.mean_ns = (double)total_ns / iterations;
    
    // Calculate standard deviation
    double sum_sq_diff = 0;
    for (uint64_t i = 0; i < iterations; i++) {
        uint64_t start = aether_get_time_ns(AETHER_CLOCK_MONOTONIC);
        func(arg);
        uint64_t end = aether_get_time_ns(AETHER_CLOCK_MONOTONIC);
        uint64_t ns = end - start;
        
        double diff = (double)ns - result.mean_ns;
        sum_sq_diff += diff * diff;
    }
    result.stddev_ns = sqrt(sum_sq_diff / iterations);
    
    result.ops_per_sec = 1e9 / result.mean_ns;
    
    return result;
}
