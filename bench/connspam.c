/*
 * connspam: open and close N TCP connections to a local server, one after
 * the other, and report how fast it went.
 *
 *   usage: connspam <connections> [port]
 *   output (one line): conns=N rate=<conn/s> mean_us=.. p99_us=.. cpu_s=..
 *
 * The server is a child process that just accept()s and close()s.
 * cpu_s is the CPU time (user + system) used by client and server together.
 */
#include <arpa/inet.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static double now_seconds(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

static double cpu_seconds(int who)
{
	struct rusage ru;

	getrusage(who, &ru);
	return ru.ru_utime.tv_sec + ru.ru_utime.tv_usec / 1e6 +
	       ru.ru_stime.tv_sec + ru.ru_stime.tv_usec / 1e6;
}

static int compare_double(const void *a, const void *b)
{
	double x = *(const double *)a, y = *(const double *)b;

	return (x > y) - (x < y);
}

static void server_loop(int listen_fd)
{
	for (;;) {
		int c = accept(listen_fd, NULL, NULL);

		if (c >= 0)
			close(c);
	}
}

int main(int argc, char **argv)
{
	int n = argc > 1 ? atoi(argv[1]) : 10000;
	int port = argc > 2 ? atoi(argv[2]) : 9555;
	struct sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_port = htons(port),
		.sin_addr.s_addr = htonl(INADDR_LOOPBACK),
	};
	/* Close with a RST instead of FIN: the port is free again at once.
	 * Without this, closed sockets wait 60 s in TIME_WAIT and we would
	 * run out of local ports after about 28000 connections. */
	struct linger rst_on_close = { .l_onoff = 1, .l_linger = 0 };
	double *latency, start, total = 0, elapsed;
	int listen_fd, one = 1, i;
	pid_t server;

	if (n <= 0) {
		fprintf(stderr, "usage: %s <connections> [port]\n", argv[0]);
		return 1;
	}
	latency = malloc(n * sizeof(*latency));
	listen_fd = socket(AF_INET, SOCK_STREAM, 0);
	setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	if (!latency || listen_fd < 0 ||
	    bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) ||
	    listen(listen_fd, 128)) {
		perror("connspam setup");
		return 1;
	}

	server = fork();
	if (server == 0)
		server_loop(listen_fd); /* never returns */

	start = now_seconds();
	for (i = 0; i < n; i++) {
		int fd = socket(AF_INET, SOCK_STREAM, 0);
		double t0 = now_seconds();

		if (fd < 0 || connect(fd, (struct sockaddr *)&addr, sizeof(addr))) {
			perror("connect");
			kill(server, SIGKILL);
			return 1;
		}
		latency[i] = now_seconds() - t0;
		total += latency[i];
		setsockopt(fd, SOL_SOCKET, SO_LINGER, &rst_on_close, sizeof(rst_on_close));
		close(fd);
	}
	elapsed = now_seconds() - start;

	/* Stop the server and collect it, so its CPU time is counted below. */
	kill(server, SIGKILL);
	waitpid(server, NULL, 0);

	qsort(latency, n, sizeof(*latency), compare_double);
	printf("conns=%d rate=%.0f mean_us=%.2f p99_us=%.2f cpu_s=%.3f\n",
	       n, n / elapsed, total / n * 1e6, latency[n * 99 / 100] * 1e6,
	       cpu_seconds(RUSAGE_SELF) + cpu_seconds(RUSAGE_CHILDREN));
	return 0;
}
