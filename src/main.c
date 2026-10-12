/* ebpf-netobserve: userspace loader, event printer and metrics exporter. */
#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <bpf/libbpf.h>
#include "exporter.h"
#include "latency.h"
#include "netobserve.h"
#include "netobserve.skel.h"
#include "table.h"

#define DEFAULT_PORT 9400

static volatile sig_atomic_t stop;
static int quiet;                /* -q: do not print one line per event */
static struct table connections; /* connections per process name */
static struct table retransmits; /* retransmissions per destination IP */

static void on_signal(int sig)
{
	(void)sig;
	stop = 1;
}

/* Called by libbpf for every record read from the ring buffer. */
static int handle_event(void *ctx, void *data, size_t size)
{
	const struct event *e = data;
	char src[INET_ADDRSTRLEN], dst[INET_ADDRSTRLEN];

	(void)ctx;
	if (size < sizeof(*e))
		return 0;

	inet_ntop(AF_INET, &e->saddr, src, sizeof(src));
	inet_ntop(AF_INET, &e->daddr, dst, sizeof(dst));

	if (e->type == EVENT_CONNECT) {
		table_add(&connections, e->comm);
		/* Timestamp printed like dmesg: seconds since boot. */
		if (!quiet)
			printf("[%12.6f] CONNECT pid=%u comm=%s dst=%s:%u\n",
			       e->ts_ns / 1e9, e->pid, e->comm, dst, e->dport);
	} else if (e->type == EVENT_RETRANSMIT) {
		table_add(&retransmits, dst);
		/* pid/comm = whatever was running on the CPU, see bpf file. */
		if (!quiet)
			printf("[%12.6f] RETRANSMIT pid=%u comm=%s %s:%u -> %s:%u\n",
			       e->ts_ns / 1e9, e->pid, e->comm,
			       src, e->sport, dst, e->dport);
	}
	return 0;
}

/*
 * Demo mode (-d): no BPF, no root. Serves made-up numbers so that the
 * /metrics output and the dashboard can be checked on any machine.
 */
static int run_demo(int listen_fd)
{
	struct latency lat = { .sum_ns = 5300000 };
	int i, client;

	for (i = 0; i < 5; i++)
		table_add(&connections, "curl");
	table_add(&connections, "we\"ird\\name");
	for (i = 0; i < 40; i++) { /* more labels than TABLE_MAX */
		char ip[LABEL_LEN];

		snprintf(ip, sizeof(ip), "10.0.0.%d", i);
		table_add(&retransmits, ip);
	}
	lat.slots[5] = 4;  /* 32..63 us */
	lat.slots[6] = 1;  /* 64..127 us */
	lat.slots[12] = 1; /* 4..8 ms */

	printf("DEMO MODE: fake data, BPF not loaded.\n");
	while (!stop) {
		client = exporter_accept(listen_fd);
		if (client >= 0)
			exporter_reply(client, &connections, &retransmits, &lat);
		else
			usleep(100 * 1000);
	}
	latency_print(&lat);
	return 0;
}

int main(int argc, char **argv)
{
	struct netobserve_bpf *skel;
	struct ring_buffer *rb;
	struct latency lat = { 0 };
	int port = DEFAULT_PORT, demo = 0;
	int hist_fd, sum_fd, listen_fd, client, opt;
	int err = 1;

	while ((opt = getopt(argc, argv, "p:qd")) != -1) {
		if (opt == 'p') {
			port = atoi(optarg);
		} else if (opt == 'q') {
			quiet = 1;
		} else if (opt == 'd') {
			demo = 1;
		} else {
			fprintf(stderr, "usage: %s [-p port] [-q] [-d]\n", argv[0]);
			return 1;
		}
	}

	/* Line-buffered output so events show up immediately, even in a pipe. */
	setvbuf(stdout, NULL, _IOLBF, 0);
	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);
	signal(SIGPIPE, SIG_IGN); /* a scraper that hangs up must not kill us */

	listen_fd = exporter_listen(port);
	if (listen_fd < 0) {
		perror("cannot listen for /metrics");
		return 1;
	}
	if (demo)
		return run_demo(listen_fd);

	/* Open the embedded BPF object, load it in the kernel, attach hooks. */
	skel = netobserve_bpf__open_and_load();
	if (!skel) {
		fprintf(stderr, "failed to open/load BPF object (are you root?)\n");
		return 1;
	}
	if (netobserve_bpf__attach(skel)) {
		fprintf(stderr, "failed to attach BPF programs\n");
		goto out;
	}
	rb = ring_buffer__new(bpf_map__fd(skel->maps.events), handle_event, NULL, NULL);
	if (!rb) {
		fprintf(stderr, "failed to create ring buffer reader\n");
		goto out;
	}
	hist_fd = bpf_map__fd(skel->maps.latency_hist);
	sum_fd = bpf_map__fd(skel->maps.latency_sum);

	printf("netobserve attached, metrics on http://localhost:%d/metrics. "
	       "Ctrl-C to stop.\n", port);

	while (!stop) {
		/* Wait up to 100 ms for events; handle_event() runs for each. */
		int n = ring_buffer__poll(rb, 100);

		if (n < 0 && n != -EINTR) {
			fprintf(stderr, "ring buffer error: %d\n", n);
			break;
		}
		/* Then answer a Prometheus scrape if one is waiting. */
		client = exporter_accept(listen_fd);
		if (client >= 0) {
			latency_read(hist_fd, sum_fd, &lat);
			exporter_reply(client, &connections, &retransmits, &lat);
		}
	}

	table_print(&connections, "\nConnections per process:");
	table_print(&retransmits, "\nRetransmissions per destination:");
	if (latency_read(hist_fd, sum_fd, &lat) == 0)
		latency_print(&lat);
	ring_buffer__free(rb);
	err = 0;
out:
	netobserve_bpf__destroy(skel);
	return err;
}
