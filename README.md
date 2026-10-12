# ebpf-netobserve

Kernel-level TCP observability with eBPF (libbpf, CO-RE). It traces:

- **outbound TCP connections** per process,
- **TCP retransmissions** per destination,
- **connection-establishment latency** (connect() to ESTABLISHED),

and exports them as Prometheus metrics, with a Grafana dashboard.

> **Verification status (read this first).** The machine this was developed on
> has no passwordless sudo, so the BPF programs were **compiled but never
> loaded** by the author of this code. What was actually run is listed in
> [PROGRESS.md](PROGRESS.md); every root-only test is scripted in
> `scripts/manual-tests.sh`. Until those tests pass, treat the kernel part as
> unverified.

## Architecture

```
                         KERNEL                                 USERSPACE
 +-----------------------------------------------+   +---------------------------+
 | kprobe tcp_v4_connect ---+--> events (ringbuf) ==> | handle_event()            |
 |   (pid, comm, dst)       |                    |   |   connections{comm}  ---+ |
 |                          +--> connect_start   |   |   retransmits{dst}   ---+ |
 |                               (LRU hash,      |   |   one line per event    | |
 |                                sock -> t0)    |   |                         v |
 | tracepoint tcp:tcp_retransmit_skb             |   |  exporter: GET /metrics   |
 |   (addresses, ports) -------> events (ringbuf)==> |   (plain sockets, :9400)  |
 |                                               |   |         ^         |       |
 | tracepoint sock:inet_sock_set_state           |   |         |         |       |
 |   ESTABLISHED: now - t0 --> latency_hist  <------ read at scrape time |       |
 |                             latency_sum       |   +-------------------|-------+
 +-----------------------------------------------+                       v
                                              Prometheus (scrape 5 s) --> Grafana
```

Design choices:

- Connections and retransmissions are **events** (ring buffer): they are rare
  enough, and userspace needs the details to print them.
- Latency is **aggregated in the kernel** (histogram in a BPF array map):
  nothing is sent to userspace per connection, the maps are only read when
  Prometheus scrapes.
- One thread: the main loop waits up to 100 ms for events, then answers one
  pending HTTP request. No locks needed. A scrape can therefore wait ~100 ms.

| File | Role |
|---|---|
| `bpf/netobserve.bpf.c` | the three kernel hooks and the BPF maps |
| `include/netobserve.h` | event struct and histogram layout, shared by both sides |
| `src/main.c` | load/attach, event loop, printing |
| `src/table.c` | small "label -> counter" table with a cardinality cap |
| `src/latency.c` | read and print the latency histogram |
| `src/exporter.c` | minimal HTTP server, Prometheus text format |

## Build and run

Needs: `clang`, `bpftool`, `make`, `gcc`, `libbpf-dev`, `libelf-dev`,
`zlib1g-dev`, and a kernel with BTF (`/sys/kernel/btf/vmlinux`).

```sh
make                      # builds ./netobserve (generated files in build/)
sudo ./netobserve         # prints events, serves http://localhost:9400/metrics
sudo ./netobserve -q      # quiet: metrics only
sudo ./netobserve -p 9500 # other port
./netobserve -d           # demo mode: FAKE data, no BPF, no root
```

On Ctrl-C it prints the per-process and per-destination tables and the
latency histogram.

Dashboard (needs Docker; not tested, see PROGRESS.md):

```sh
docker compose -f dashboards/docker-compose.yml up -d
# Grafana http://localhost:3000, Prometheus http://localhost:9090
```

## Metrics

| Metric | Type | Labels | Meaning |
|---|---|---|---|
| `connections_total` | counter | `comm` | connect() calls on TCP/IPv4 sockets, per process name |
| `tcp_retransmits_total` | counter | `dst` | retransmitted segments, per destination IPv4 address |
| `connect_latency_seconds` | histogram | `le` | time from connect() to ESTABLISHED; 21 power-of-two buckets from 2 us to ~2.1 s, plus `+Inf` |

Label cardinality is capped: only the first 32 distinct `comm` / `dst` values
get their own series, everything else is counted under `"other"`
(explanation in `src/table.h`).

## Benchmark

Method (`bench/run.sh`): `bench/connspam` opens and closes 20000 sequential
loopback TCP connections and reports connections/s, mean and p99 connect
latency, and CPU time. This is the worst case for the tracer: every connection
triggers the kprobe, the state tracepoint and one ring buffer event. 5 runs
without the tracer and 5 with it, interleaved; mean ± standard deviation.
Bulk throughput (iperf3-style) is not measured: no hook sits on the per-packet
path, except the retransmission tracepoint.

Results: see [bench/RESULTS.md](bench/RESULTS.md). **Currently baseline only**
(the "with tracer" column needs root: `bash bench/run.sh`). All numbers come
from virtualized WSL2 and are indicative only.

## Limitations

- BPF side not yet run (see the status note at the top).
- IPv4 only (`tcp_v6_connect` is not hooked, IPv6 retransmissions are skipped).
- `connections_total` counts connect() *attempts*, successful or not.
- For retransmissions, pid/comm are those of whatever was running on the CPU
  (often not the socket owner): a hint, not an attribution.
- `comm` is the 15-character process name, not a pid: two processes with the
  same name share a counter.
- Latency buckets are powers of two, so quantiles are accurate within 2x.
- If the ring buffer is full, events are dropped without being counted.
- Counters live in userspace memory and restart from zero with the tool.

## Future work

- OpenTelemetry export (OTLP) next to Prometheus. Deliberately not implemented.
- IPv6, a dropped-events counter, per-destination latency.
