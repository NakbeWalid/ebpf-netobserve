/* Definitions shared by the kernel program (bpf/) and userspace (src/). */
#ifndef NETOBSERVE_H
#define NETOBSERVE_H

/* The BPF side gets __u32 & co. from vmlinux.h, userspace from linux/types.h. */
#ifndef __VMLINUX_H__
#include <linux/types.h>
#endif

#define COMM_LEN 16 /* same as TASK_COMM_LEN in the kernel */

enum event_type {
	EVENT_CONNECT = 1,    /* a process called connect() on a TCP/IPv4 socket */
	EVENT_RETRANSMIT = 2, /* the kernel re-sent a TCP segment */
};

/* One record sent from the kernel to userspace through the ring buffer. */
struct event {
	__u64 ts_ns;          /* kernel monotonic clock (time since boot) */
	__u32 type;           /* enum event_type */
	__u32 pid;            /* process id (tgid) */
	__u32 saddr;          /* source IPv4, network byte order (0 if unknown) */
	__u32 daddr;          /* destination IPv4, network byte order */
	__u16 sport;          /* source port, host byte order (0 if unknown) */
	__u16 dport;          /* destination port, host byte order */
	char comm[COMM_LEN];  /* process name */
};

/*
 * Connect latency histogram: LAT_SLOTS counters, powers of two in microseconds.
 * Slot i counts connections that took [2^i, 2^(i+1)) us (slot 0: below 2 us).
 * The last slot also takes everything slower (about 2 s and more).
 */
#define LAT_SLOTS 22

#endif
