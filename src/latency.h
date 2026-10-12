/* Userspace view of the connect latency histogram kept in BPF maps. */
#ifndef LATENCY_H
#define LATENCY_H

#include "netobserve.h"

struct latency {
	unsigned long long slots[LAT_SLOTS]; /* see LAT_SLOTS in netobserve.h */
	unsigned long long sum_ns;           /* sum of all latencies */
};

/* Copy the two BPF maps into `out`. Returns 0 on success. */
int latency_read(int hist_fd, int sum_fd, struct latency *out);
void latency_print(const struct latency *lat);

#endif
