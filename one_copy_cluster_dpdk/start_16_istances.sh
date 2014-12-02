#!/bin/bash
#Starts a cluster on the number of cores passed by parameters

./build/cluster_dpdk -c 0X11 -n 4 --proc-type=auto -- -n 16 -p 0 &
sleep 5
./build/cluster_dpdk -c 0X22 -n 4 --proc-type=auto -- -n 16 -p 1 &
#sleep 2
./build/cluster_dpdk -c 0X44 -n 4 --proc-type=auto -- -n 16 -p 2 &
#sleep 2
./build/cluster_dpdk -c 0X88 -n 4 --proc-type=auto -- -n 16 -p 3 &
#sleep 2
./build/cluster_dpdk -c 0X11 -n 4 --proc-type=auto -- -n 16 -p 4 &
#sleep 2
./build/cluster_dpdk -c 0X22 -n 4 --proc-type=auto -- -n 16 -p 5 &
#sleep 2
./build/cluster_dpdk -c 0X44 -n 4 --proc-type=auto -- -n 16 -p 6 &
#sleep 2
./build/cluster_dpdk -c 0X88 -n 4 --proc-type=auto -- -n 16 -p 7 &
#sleep 2
./build/cluster_dpdk -c 0X11 -n 4 --proc-type=auto -- -n 16 -p 8 &
#sleep 2
./build/cluster_dpdk -c 0X22 -n 4 --proc-type=auto -- -n 16 -p 9 &
#sleep 2
./build/cluster_dpdk -c 0X44 -n 4 --proc-type=auto -- -n 16 -p 10 &
#sleep 2
./build/cluster_dpdk -c 0X88 -n 4 --proc-type=auto -- -n 16 -p 11 &
#sleep 2
./build/cluster_dpdk -c 0X11 -n 4 --proc-type=auto -- -n 16 -p 12 &
#sleep 2
./build/cluster_dpdk -c 0X22 -n 4 --proc-type=auto -- -n 16 -p 13 &
#sleep 2
./build/cluster_dpdk -c 0X44 -n 4 --proc-type=auto -- -n 16 -p 14 &
#sleep 2
./build/cluster_dpdk -c 0X88 -n 4 --proc-type=auto -- -n 16 -p 15 &
#sleep 2

