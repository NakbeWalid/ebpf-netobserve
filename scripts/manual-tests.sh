#!/bin/bash
# Manual tests that need root (the BPF programs cannot be loaded without it).
# Run from the project root:   bash scripts/manual-tests.sh <step>
# Each step prints what you should see.
set -u
cd "$(dirname "$0")/.."

step="${1:-help}"

# Ask for the password now: a sudo started in the background cannot prompt.
case "$step" in [0-9]*) sudo -v || exit 1 ;; esac

case "$step" in
0)
	echo "== Step 0: the BPF object loads and attaches =="
	echo "Expected: 'netobserve attached', then it stops after 3 s."
	sudo timeout -s INT 3 ./netobserve
	;;
1)
	echo "== Step 1: outbound connections =="
	echo "Expected: CONNECT lines with comm=curl (dst = example.com:80) and"
	echo "comm=nc (dst=127.0.0.1:9), then a per-process table (curl, nc)."
	sudo ./netobserve &
	sleep 2
	curl -s -o /dev/null http://example.com
	nc -z -w1 127.0.0.1 9
	sleep 1
	sudo pkill -INT -x netobserve
	wait
	;;
2)
	echo "== Step 2: retransmissions under 20% packet loss =="
	echo "Expected: many RETRANSMIT lines 10.200.0.1:* -> 10.200.0.2:9000 and"
	echo "a per-destination table with 10.200.0.2."
	sudo ./netobserve &
	sleep 2
	sudo bash scripts/gen-retrans.sh
	sleep 1
	sudo pkill -INT -x netobserve
	wait
	;;
3)
	echo "== Step 3: connect latency =="
	echo "Expected: a latency histogram at exit with 20 loopback connections"
	echo "in the microsecond slots (roughly 16 us .. 1 ms) and 1 internet"
	echo "connection (curl) in a millisecond slot."
	python3 -m http.server 8099 --bind 127.0.0.1 >/dev/null 2>&1 &
	srv=$!
	sudo ./netobserve > build/step3.log &
	tracer=$!
	sleep 2
	for i in $(seq 20); do curl -s -o /dev/null http://127.0.0.1:8099/; done
	curl -s -o /dev/null http://example.com
	sleep 1
	sudo pkill -INT -x netobserve
	wait $tracer
	kill $srv
	sed -n '/Connect latency/,$p' build/step3.log
	;;
4)
	echo "== Step 4: Prometheus exporter with real data =="
	echo "Expected: connections_total{comm=\"curl\"} >= 5 and"
	echo "connect_latency_seconds_count >= 5."
	python3 -m http.server 8099 --bind 127.0.0.1 >/dev/null 2>&1 &
	srv=$!
	sudo ./netobserve -q &
	sleep 2
	for i in $(seq 5); do curl -s -o /dev/null http://127.0.0.1:8099/; done
	sleep 1
	curl -s http://localhost:9400/metrics
	sudo pkill -INT -x netobserve
	kill $srv
	wait
	;;
5)
	echo "== Step 5: Prometheus + Grafana (needs docker) =="
	echo "Start 'sudo ./netobserve -q' in another terminal first."
	echo "Expected: target 'netobserve' UP on http://localhost:9090/targets,"
	echo "dashboard 'ebpf-netobserve' on http://localhost:3000."
	docker compose -f dashboards/docker-compose.yml up -d
	;;
6)
	echo "== Step 6: overhead benchmark (asks for sudo) =="
	bash bench/run.sh
	;;
*)
	echo "usage: bash scripts/manual-tests.sh <step>"
	grep -E '^[0-9a-z]+\)$' "$0" | tr -d ')' | sed 's/^/  step /'
	;;
esac
