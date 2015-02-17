#!/bin/bash

./build/cluster_dpdk -c 0X03 -n 4 --proc-type=auto -- -n 2 -p 0 &
sleep 5
./build/cluster_dpdk -c 0X0C -n 4 --proc-type=auto -- -n 2 -p 1 &
sleep 2
