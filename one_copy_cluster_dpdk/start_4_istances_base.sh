#!/bin/bash
#Starts a cluster on the number of cores passed by parameters

./build/cluster_dpdk -c 0X03 -n 4 --proc-type=auto -- -n 4 -p 0 &
sleep 5
./build/cluster_dpdk -c 0X0C -n 4 --proc-type=auto -- -n 4 -p 1 &
sleep 2
./build/cluster_dpdk -c 0X30 -n 4 --proc-type=auto -- -n 4 -p 2 &
sleep 2
./build/cluster_dpdk -c 0XC0 -n 4 --proc-type=auto -- -n 4 -p 3 &
sleep 2
