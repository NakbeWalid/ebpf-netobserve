#include <stdio.h>
#include <bpf/bpf.h>
#include "latency.h"

int latency_read(int hist_fd, int sum_fd, struct latency *out)
{
	__u32 key;
	__u64 value;

	/* One bpf() syscall per slot: 22 small lookups, only when needed. */
	for (key = 0; key < LAT_SLOTS; key++) {
		if (bpf_map_lookup_elem(hist_fd, &key, &value))
			return -1;
		out->slots[key] = value;
	}
	key = 0;
	if (bpf_map_lookup_elem(sum_fd, &key, &value))
		return -1;
	out->sum_ns = value;
	return 0;
}

void latency_print(const struct latency *lat)
{
	unsigned long long total = 0;
	int i;

	printf("\nConnect latency (microseconds):\n");
	for (i = 0; i < LAT_SLOTS; i++) {
		total += lat->slots[i];
		if (lat->slots[i] == 0)
			continue;
		if (i == LAT_SLOTS - 1)
			printf("  %8llu and more    : %llu\n", 1ULL << i, lat->slots[i]);
		else
			printf("  %8llu .. %-8llu: %llu\n",
			       i ? 1ULL << i : 0, (2ULL << i) - 1, lat->slots[i]);
	}
	if (total)
		printf("  count=%llu mean=%.1f us\n", total, lat->sum_ns / 1e3 / total);
	else
		printf("  (no connection measured)\n");
}
