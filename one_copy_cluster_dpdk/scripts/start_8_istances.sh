#!/bin/bash
#Starts a cluster on the number of cores passed by parameters

./build/cluster_dpdk -c 0X11 -n 4 --proc-type=auto -- -n 8 -p 0 &
sleep 5
./build/cluster_dpdk -c 0X22 -n 4 --proc-type=auto -- -n 8 -p 1 &
sleep 2
./build/cluster_dpdk -c 0X44 -n 4 --proc-type=auto -- -n 8 -p 2 &
sleep 2
./build/cluster_dpdk -c 0X88 -n 4 --proc-type=auto -- -n 8 -p 3 &
sleep 2
./build/cluster_dpdk -c 0X11 -n 4 --proc-type=auto -- -n 8 -p 4 &
sleep 2
./build/cluster_dpdk -c 0X22 -n 4 --proc-type=auto -- -n 8 -p 5 &
sleep 2
./build/cluster_dpdk -c 0X44 -n 4 --proc-type=auto -- -n 8 -p 6 &
sleep 2
./build/cluster_dpdk -c 0X88 -n 4 --proc-type=auto -- -n 8 -p 7 &
sleep 2

