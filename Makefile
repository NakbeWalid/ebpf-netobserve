# ebpf-netobserve build.
#
# Pipeline (same idea as test/hello):
#   1. bpftool dumps kernel types (BTF)        -> build/vmlinux.h
#   2. clang compiles bpf/netobserve.bpf.c     -> build/netobserve.bpf.o
#   3. bpftool generates the skeleton          -> build/netobserve.skel.h
#   4. gcc compiles src/*.c and links libbpf   -> ./netobserve

CLANG   ?= clang
BPFTOOL ?= bpftool
CC      ?= gcc

# Kernel arch name, needed by bpf_tracing.h to decode kprobe arguments.
ARCH := $(shell uname -m | sed 's/x86_64/x86/; s/aarch64/arm64/')

BUILD   := build
VMLINUX := $(BUILD)/vmlinux.h
BPF_OBJ := $(BUILD)/netobserve.bpf.o
SKEL    := $(BUILD)/netobserve.skel.h

SRCS := $(wildcard src/*.c)
OBJS := $(patsubst src/%.c,$(BUILD)/%.o,$(SRCS))

CFLAGS     := -O2 -g -Wall -Wextra -Iinclude -I$(BUILD)
BPF_CFLAGS := -O2 -g -Wall -target bpf -D__TARGET_ARCH_$(ARCH) -Iinclude -I$(BUILD)
LDLIBS     := -lbpf -lelf -lz

.PHONY: all clean bench
all: netobserve

$(BUILD):
	mkdir -p $(BUILD)

# 1. Kernel type definitions, generated from the running kernel's BTF.
$(VMLINUX): | $(BUILD)
	$(BPFTOOL) btf dump file /sys/kernel/btf/vmlinux format c > $@

# 2. Kernel-side program, compiled to BPF bytecode.
$(BPF_OBJ): bpf/netobserve.bpf.c include/netobserve.h $(VMLINUX)
	$(CLANG) $(BPF_CFLAGS) -c $< -o $@

# 3. Skeleton: a C header embedding the bytecode plus load/attach helpers.
$(SKEL): $(BPF_OBJ)
	$(BPFTOOL) gen skeleton $< > $@

# 4. Userspace. Every object depends on the skeleton and on our headers.
$(BUILD)/%.o: src/%.c $(SKEL) $(wildcard include/*.h) $(wildcard src/*.h)
	$(CC) $(CFLAGS) -c $< -o $@

netobserve: $(OBJS)
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

# Load generator for the benchmark (plain C, no BPF).
bench: bench/connspam
bench/connspam: bench/connspam.c
	$(CC) -O2 -g -Wall -Wextra $< -o $@

clean:
	rm -rf $(BUILD) netobserve bench/connspam
