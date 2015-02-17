#!/bin/bash

./build/cluster_dpdk -c 0X11 -n 4 --proc-type=auto -- -n 4 -p 0 &
sleep 5
./build/cluster_dpdk -c 0X22 -n 4 --proc-type=auto -- -n 4 -p 1 &
sleep 2
./build/cluster_dpdk -c 0X44 -n 4 --proc-type=auto -- -n 4 -p 2 &
sleep 2
./build/cluster_dpdk -c 0X88 -n 4 --proc-type=auto -- -n 4 -p 3 &
sleep 2
