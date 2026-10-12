/* Minimal HTTP server exposing the metrics in Prometheus text format. */
#ifndef EXPORTER_H
#define EXPORTER_H

#include "latency.h"
#include "table.h"

/* Open a non-blocking listening TCP socket. Returns the fd, or -1 on error. */
int exporter_listen(int port);

/* Returns a connected client fd if a scrape is waiting, -1 otherwise. */
int exporter_accept(int listen_fd);

/* Read the HTTP request, write the metrics, close the client. */
void exporter_reply(int client_fd, const struct table *connections,
		    const struct table *retransmits, const struct latency *lat);

#endif
