#!/bin/bash
#Starts a cluster on the number of cores passed by parameters

profile="/usr/bin/perf_3.16 stat -e cycles,instructions,branches,branch-misses,cache-references,cache-misses,major-faults,cpu/mem-loads/,cpu/mem-stores/,r1a2,r5a3"

$profile ./build/cluster_dpdk -c 0X11 -n 4 --proc-type=auto -- -n 4 -p 0 &
sleep 5
$profile ./build/cluster_dpdk -c 0X22 -n 4 --proc-type=auto -- -n 4 -p 1 &
sleep 2
$profile ./build/cluster_dpdk -c 0X44 -n 4 --proc-type=auto -- -n 4 -p 2 &
sleep 2
$profile ./build/cluster_dpdk -c 0X88 -n 4 --proc-type=auto -- -n 4 -p 3 &
sleep 2
