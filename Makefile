# Aether - Distributed Shared Memory with RDMA
# Makefile

# Compiler and flags
CC := gcc
CXX := g++
CFLAGS := -Wall -Wextra -Werror -O3 -g -fPIC
CXXFLAGS := -Wall -Wextra -Werror -O3 -g -std=c++17 -fPIC
LDFLAGS := -lpthread -lrt -lm

# Directories
SRC_DIR := src
INC_DIR := include
BUILD_DIR := build
LIB_DIR := lib

# Source files
CORE_SRCS := $(wildcard $(SRC_DIR)/core/*.c)
DSM_SRCS := $(wildcard $(SRC_DIR)/dsm/*.c)
RPC_SRCS := $(wildcard $(SRC_DIR)/rpc/*.c)
STRUCT_SRCS := $(wildcard $(SRC_DIR)/structures/*.c)
PROF_SRCS := $(wildcard $(SRC_DIR)/profiling/*.c)

ALL_SRCS := $(CORE_SRCS) $(DSM_SRCS) $(RPC_SRCS) $(STRUCT_SRCS) $(PROF_SRCS)
ALL_OBJS := $(ALL_SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

# Libraries
STATIC_LIB := $(LIB_DIR)/libaether.a
SHARED_LIB := $(LIB_DIR)/libaether.so

# RDMA libraries
RDMA_LIBS := -libverbs -lrdmacm

# Include paths
INCLUDES := -I$(INC_DIR)

# Tests
TEST_DIR := tests
BENCHMARK_DIR := benchmarks
TESTS := $(wildcard $(TEST_DIR)/*.c)
TEST_BINS := $(TESTS:$(TEST_DIR)/%.c=$(BUILD_DIR)/%)

# Benchmarks
BENCHMARKS := $(wildcard $(BENCHMARK_DIR)/*.c)
BENCH_BINS := $(BENCHMARKS:$(BENCHMARK_DIR)/%.c=$(BUILD_DIR)/%)

.PHONY: all clean install test benchmarks docs

all: $(BUILD_DIR) $(LIB_DIR) $(STATIC_LIB) $(SHARED_LIB)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)/core $(BUILD_DIR)/dsm $(BUILD_DIR)/rpc
	mkdir -p $(BUILD_DIR)/structures $(BUILD_DIR)/profiling
	mkdir -p $(BUILD_DIR)/tests $(BUILD_DIR)/benchmarks

$(LIB_DIR):
	mkdir -p $(LIB_DIR)

# Static library
$(STATIC_LIB): $(ALL_OBJS)
	@mkdir -p $(LIB_DIR)
	ar rcs $@ $^
	ranlib $@

# Shared library
$(SHARED_LIB): $(ALL_OBJS)
	@mkdir -p $(LIB_DIR)
	$(CC) -shared -o $@ $^ $(LDFLAGS) $(RDMA_LIBS)

# Object files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Tests
test: $(TEST_BINS)

$(BUILD_DIR)/%: $(TEST_DIR)/%.c $(STATIC_LIB)
	@mkdir -p $(BUILD_DIR)/tests
	$(CC) $(CFLAGS) $(INCLUDES) $< -o $@ -L$(LIB_DIR) -laether $(LDFLAGS) $(RDMA_LIBS)

# Benchmarks
benchmarks: $(BENCH_BINS)

$(BUILD_DIR)/%: $(BENCHMARK_DIR)/%.c $(STATIC_LIB)
	@mkdir -p $(BUILD_DIR)/benchmarks
	$(CC) $(CFLAGS) $(INCLUDES) $< -o $@ -L$(LIB_DIR) -laether $(LDFLAGS) $(RDMA_LIBS)

# Documentation
docs:
	@mkdir -p docs/html
	@echo "Documentation generation not yet configured"

# Install
install: all
	install -d $(DESTDIR)/usr/local/include/aether
	install -d $(DESTDIR)/usr/local/include/rdma
	install -d $(DESTDIR)/usr/local/include/dsm
	install -d $(DESTDIR)/usr/local/include/rpc
	install -d $(DESTDIR)/usr/local/include/structures
	install -d $(DESTDIR)/usr/local/include/profiling
	install -d $(DESTDIR)/usr/local/lib
	install -m 644 $(INC_DIR)/aether/*.h $(DESTDIR)/usr/local/include/aether/
	install -m 644 $(INC_DIR)/rdma/*.h $(DESTDIR)/usr/local/include/rdma/
	install -m 644 $(INC_DIR)/dsm/*.h $(DESTDIR)/usr/local/include/dsm/
	install -m 644 $(INC_DIR)/rpc/*.h $(DESTDIR)/usr/local/include/rpc/
	install -m 644 $(INC_DIR)/structures/*.h $(DESTDIR)/usr/local/include/structures/
	install -m 644 $(INC_DIR)/profiling/*.h $(DESTDIR)/usr/local/include/profiling/
	install -m 644 $(STATIC_LIB) $(DESTDIR)/usr/local/lib/
	install -m 644 $(SHARED_LIB) $(DESTDIR)/usr/local/lib/
	ldconfig

# Clean
clean:
	rm -rf $(BUILD_DIR) $(LIB_DIR)

# Dependencies
$(BUILD_DIR)/core/%.d: $(SRC_DIR)/core/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -M $< -o $@

-include $(wildcard $(BUILD_DIR)/core/*.d $(BUILD_DIR)/dsm/*.d $(BUILD_DIR)/rpc/*.d)
