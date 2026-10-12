#!/bin/bash
# Generate TCP retransmissions in an isolated setup (run as root).
#
#   host: veth nob0 10.200.0.1  <----->  netns "nob": veth nob1 10.200.0.2
#         netem: 20% loss on nob0               nc listening on port 9000
#
# The host sends 5 MB to the namespace; lost segments are retransmitted.
# Everything created here (namespace, veth pair, qdisc) is removed on exit.
# Needs the sch_netem kernel module (loaded automatically by tc).
set -eu

NS=nob
LOSS="${LOSS:-20%}"

cleanup() {
	ip netns del "$NS" 2>/dev/null || true   # also removes nob1 and its peer
	ip link del nob0 2>/dev/null || true
}
trap cleanup EXIT
cleanup

ip netns add "$NS"
ip link add nob0 type veth peer name nob1
ip link set nob1 netns "$NS"
ip addr add 10.200.0.1/24 dev nob0
ip link set nob0 up
ip netns exec "$NS" ip addr add 10.200.0.2/24 dev nob1
ip netns exec "$NS" ip link set nob1 up

tc qdisc add dev nob0 root netem loss "$LOSS"

ip netns exec "$NS" nc -l 9000 > /dev/null &
sleep 0.5
head -c 5M /dev/zero | nc -N 10.200.0.2 9000
wait
echo "gen-retrans: sent 5 MB through $LOSS loss"
