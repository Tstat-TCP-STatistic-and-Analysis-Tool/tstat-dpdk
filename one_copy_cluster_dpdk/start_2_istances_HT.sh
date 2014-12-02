#!/bin/bash
#Starts a cluster on the number of cores passed by parameters

./build/cluster_dpdk -c 0X11 -n 4 --proc-type=auto -- -n 2 -p 0 &
sleep 5
./build/cluster_dpdk -c 0X22 -n 4 --proc-type=auto -- -n 2 -p 1 &
sleep 2
