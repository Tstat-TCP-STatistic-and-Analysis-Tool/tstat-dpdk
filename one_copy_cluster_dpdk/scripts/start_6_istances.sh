#!/bin/bash
#Starts a cluster on the number of cores passed by parameters

./build/cluster_dpdk -c 0X09 -n 4 --proc-type=auto -- -n 6 -p 0 &
sleep 5
./build/cluster_dpdk -c 0X05 -n 4 --proc-type=auto -- -n 6 -p 1 &
sleep 2
./build/cluster_dpdk -c 0X03 -n 4 --proc-type=auto -- -n 6 -p 2 &
sleep 2
./build/cluster_dpdk -c 0X90 -n 4 --proc-type=auto -- -n 6 -p 3 &
sleep 2
./build/cluster_dpdk -c 0X50 -n 4 --proc-type=auto -- -n 6 -p 4 &
sleep 2
./build/cluster_dpdk -c 0X30 -n 4 --proc-type=auto -- -n 6 -p 5 &
sleep 2
