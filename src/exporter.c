#include <arpa/inet.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include "exporter.h"

int exporter_listen(int port)
{
	struct sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_port = htons(port),
		.sin_addr.s_addr = htonl(INADDR_ANY),
	};
	int one = 1;
	int fd = socket(AF_INET, SOCK_STREAM, 0);

	if (fd < 0)
		return -1;
	/* Allow a quick restart on the same port. */
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) || listen(fd, 8)) {
		close(fd);
		return -1;
	}
	/* Non-blocking: accept() returns at once when nobody is waiting. */
	fcntl(fd, F_SETFL, O_NONBLOCK);
	return fd;
}

int exporter_accept(int listen_fd)
{
	return accept(listen_fd, NULL, NULL);
}

/* Label values must escape backslash, double quote and newline. */
static void print_label(FILE *out, const char *s)
{
	for (; *s; s++) {
		if (*s == '\\' || *s == '"')
			fprintf(out, "\\%c", *s);
		else if (*s == '\n')
			fputs("\\n", out);
		else
			fputc(*s, out);
	}
}

static void print_counter(FILE *out, const char *name, const char *help,
			  const char *label, const struct table *t)
{
	int i;

	fprintf(out, "# HELP %s %s\n# TYPE %s counter\n", name, help, name);
	for (i = 0; i < t->n; i++) {
		fprintf(out, "%s{%s=\"", name, label);
		print_label(out, t->rows[i].label);
		fprintf(out, "\"} %llu\n", t->rows[i].count);
	}
	/* Everything above the cardinality cap, see table.h. */
	fprintf(out, "%s{%s=\"other\"} %llu\n", name, label, t->other);
}

static void print_histogram(FILE *out, const struct latency *lat)
{
	const char *name = "connect_latency_seconds";
	unsigned long long cumulative = 0;
	int i;

	fprintf(out, "# HELP %s Time from connect() to TCP ESTABLISHED.\n", name);
	fprintf(out, "# TYPE %s histogram\n", name);
	/*
	 * Prometheus buckets are cumulative: bucket{le="x"} counts every
	 * observation <= x. Slot i ends at 2^(i+1) microseconds. The last slot
	 * has no upper bound, so it only appears in the +Inf bucket.
	 */
	for (i = 0; i < LAT_SLOTS - 1; i++) {
		cumulative += lat->slots[i];
		fprintf(out, "%s_bucket{le=\"%.6f\"} %llu\n",
			name, (double)(2ULL << i) / 1e6, cumulative);
	}
	cumulative += lat->slots[LAT_SLOTS - 1];
	fprintf(out, "%s_bucket{le=\"+Inf\"} %llu\n", name, cumulative);
	fprintf(out, "%s_sum %.9f\n", name, (double)lat->sum_ns / 1e9);
	fprintf(out, "%s_count %llu\n", name, cumulative);
}

void exporter_reply(int client_fd, const struct table *connections,
		    const struct table *retransmits, const struct latency *lat)
{
	struct timeval timeout = { .tv_sec = 1 };
	char request[1024];
	ssize_t n;
	FILE *out;

	/* A silent client must not block the event loop for more than 1 s. */
	setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
	n = read(client_fd, request, sizeof(request) - 1);
	request[n > 0 ? n : 0] = '\0';

	/* fdopen() lets us use fprintf() on the socket. */
	out = fdopen(client_fd, "w");
	if (!out) {
		close(client_fd);
		return;
	}
	if (strncmp(request, "GET /metrics", 12) != 0) {
		fprintf(out, "HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n"
			     "try /metrics\n");
		fclose(out);
		return;
	}
	/* No Content-Length: the body ends when we close the connection. */
	fprintf(out, "HTTP/1.1 200 OK\r\n"
		     "Content-Type: text/plain; version=0.0.4\r\n"
		     "Connection: close\r\n\r\n");
	print_counter(out, "connections_total",
		      "Outbound TCP/IPv4 connections per process name.",
		      "comm", connections);
	print_counter(out, "tcp_retransmits_total",
		      "TCP/IPv4 retransmitted segments per destination address.",
		      "dst", retransmits);
	print_histogram(out, lat);
	fclose(out); /* flushes and closes client_fd */
}
