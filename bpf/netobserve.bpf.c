// SPDX-License-Identifier: GPL-2.0
/* Kernel side of ebpf-netobserve. Hooks are added step by step. */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#include <bpf/bpf_endian.h>
#include "netobserve.h"

#define AF_INET 2 /* IPv4; a macro, so it is not in vmlinux.h */

char LICENSE[] SEC("license") = "GPL";

/* Ring buffer: kernel writes events, userspace reads them. 256 KiB. */
struct {
	__uint(type, BPF_MAP_TYPE_RINGBUF);
	__uint(max_entries, 256 * 1024);
} events SEC(".maps");

/*
 * When did connect() start? Key: address of the kernel socket (struct sock).
 * LRU: if an entry is never removed (connect() failed before any state
 * change), the oldest entries are evicted instead of filling the map forever.
 */
struct {
	__uint(type, BPF_MAP_TYPE_LRU_HASH);
	__uint(max_entries, 16384);
	__type(key, __u64);
	__type(value, __u64);
} connect_start SEC(".maps");

/* Latency histogram, see LAT_SLOTS in netobserve.h. */
struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, LAT_SLOTS);
	__type(key, __u32);
	__type(value, __u64);
} latency_hist SEC(".maps");

/* Sum of all measured latencies in nanoseconds (single entry, key 0). */
struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, __u64);
} latency_sum SEC(".maps");

/*
 * Outbound connections.
 * tcp_v4_connect(sk, uaddr, addr_len) runs in the context of the process that
 * called connect(), so "current" pid/comm are the right ones. uaddr is the
 * destination, already copied into kernel memory by the syscall layer.
 */
SEC("kprobe/tcp_v4_connect")
int BPF_KPROBE(on_tcp_v4_connect, struct sock *sk, struct sockaddr *uaddr)
{
	struct sockaddr_in *dst = (struct sockaddr_in *)uaddr;
	__u64 key = (__u64)sk;
	__u64 now = bpf_ktime_get_ns();
	struct event *e;

	/* Remember the start time for the latency measurement below. */
	bpf_map_update_elem(&connect_start, &key, &now, BPF_ANY);

	e = bpf_ringbuf_reserve(&events, sizeof(*e), 0);
	if (!e)
		return 0; /* ring buffer full: drop the event */

	e->ts_ns = now;
	e->type = EVENT_CONNECT;
	e->pid = bpf_get_current_pid_tgid() >> 32;
	e->saddr = 0; /* not chosen yet at this point */
	e->sport = 0;
	e->daddr = BPF_CORE_READ(dst, sin_addr.s_addr);
	e->dport = bpf_ntohs(BPF_CORE_READ(dst, sin_port));
	bpf_get_current_comm(e->comm, sizeof(e->comm));

	bpf_ringbuf_submit(e, 0);
	return 0;
}

/*
 * Retransmissions.
 * The tracepoint already gives addresses and ports (ports in host byte order).
 * Careful: a retransmission is triggered by a timer or by an incoming ACK, so
 * "current" is often NOT the process that owns the socket (it can be
 * swapper/ksoftirqd or any process that was interrupted). pid/comm are
 * reported as a hint only.
 */
SEC("tracepoint/tcp/tcp_retransmit_skb")
int on_tcp_retransmit(struct trace_event_raw_tcp_retransmit_skb *ctx)
{
	struct event *e;

	if (ctx->family != AF_INET)
		return 0; /* IPv4 only */

	e = bpf_ringbuf_reserve(&events, sizeof(*e), 0);
	if (!e)
		return 0;

	e->ts_ns = bpf_ktime_get_ns();
	e->type = EVENT_RETRANSMIT;
	e->pid = bpf_get_current_pid_tgid() >> 32;
	/*
	 * saddr/daddr are 4-byte arrays at an odd offset in ctx. A plain copy
	 * makes clang read them through "ctx + offset" kept in a register, and
	 * the verifier refuses to dereference a modified ctx pointer. The helper
	 * takes the address as a plain value, so it is accepted.
	 */
	bpf_probe_read_kernel(&e->saddr, sizeof(e->saddr), ctx->saddr);
	bpf_probe_read_kernel(&e->daddr, sizeof(e->daddr), ctx->daddr);
	e->sport = ctx->sport;
	e->dport = ctx->dport;
	bpf_get_current_comm(e->comm, sizeof(e->comm));

	bpf_ringbuf_submit(e, 0);
	return 0;
}

/*
 * Connection latency.
 * This tracepoint fires on every TCP state change. For a client socket the
 * path is CLOSE -> SYN_SENT -> ESTABLISHED, so the time between the kprobe
 * above and ESTABLISHED is the duration of the three-way handshake.
 */
SEC("tracepoint/sock/inet_sock_set_state")
int on_set_state(struct trace_event_raw_inet_sock_set_state *ctx)
{
	__u64 key = (__u64)ctx->skaddr;
	__u64 *start, *counter, delta_ns, us;
	__u32 slot = 0, zero = 0;

	if (ctx->protocol != IPPROTO_TCP)
		return 0;

	if (ctx->newstate == TCP_CLOSE) {
		/* connect() failed or socket closed: forget the start time. */
		bpf_map_delete_elem(&connect_start, &key);
		return 0;
	}
	if (ctx->newstate != TCP_ESTABLISHED)
		return 0;

	start = bpf_map_lookup_elem(&connect_start, &key);
	if (!start)
		return 0; /* server side socket, or connect() started before us */
	delta_ns = bpf_ktime_get_ns() - *start;
	bpf_map_delete_elem(&connect_start, &key);

	/* slot = floor(log2(microseconds)), capped to the last slot. */
	for (us = delta_ns / 1000; us > 1 && slot < LAT_SLOTS - 1; us >>= 1)
		slot++;

	/* Atomic add: several CPUs can update the same counter. */
	counter = bpf_map_lookup_elem(&latency_hist, &slot);
	if (counter)
		__sync_fetch_and_add(counter, 1);
	counter = bpf_map_lookup_elem(&latency_sum, &zero);
	if (counter)
		__sync_fetch_and_add(counter, delta_ns);
	return 0;
}
