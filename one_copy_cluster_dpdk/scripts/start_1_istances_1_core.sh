#!/bin/bash
#Starts a cluster on the number of cores passed by parameters

./build/cluster_dpdk -c 0X01 -n 4 --proc-type=auto -- -n 1 -p 0 &
sleep 5

