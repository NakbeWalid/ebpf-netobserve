# ebpf-netobserve

## Goal
Kernel-level network observability tool: trace outbound TCP connections per
process, TCP retransmissions and connection-establishment latency with eBPF,
export them to Prometheus (/metrics), show them in Grafana, benchmark overhead.

## Environment
- WSL2 Ubuntu, kernel 6.18, BTF at /sys/kernel/btf/vmlinux
- C, libbpf (CO-RE), clang, bpftool (skeletons), make
- No passwordless sudo: BPF programs cannot be loaded by the agent.
  Anything that needs root goes to `scripts/manual-tests.sh`.

## Build
- `make`        build everything (output: `./netobserve`, generated files in `build/`)
- `make bench`  build the load generator `bench/connspam`
- `make clean`  remove all generated files
- Run: `sudo ./netobserve [-p port] [-q]`; `./netobserve -d` = demo mode
  (fake data, no BPF, no root) to check the /metrics output.

## Layout
- `bpf/`        kernel programs (`*.bpf.c`)
- `src/`        userspace C (loader, event printing, Prometheus exporter)
- `include/`    headers shared by kernel and userspace
- `bench/`      overhead benchmark (load generator + script + results)
- `dashboards/` Prometheus + Grafana provisioning
- `scripts/`    manual test scripts (need sudo)
- `test/`       original minimal libbpf example (do not modify)

## Conventions
- Code and comments in English; PROGRESS.md in French, short.
- Small readable files; every line must be explainable by a student.
- Minimal dependencies: libbpf, libelf, zlib only.
- IPv4 only for now.
- Never claim something works unless it was run; otherwise "NON VERIFIE".
- The user makes the git commits themselves: do not run `git commit`.

## Current status
See PROGRESS.md.
