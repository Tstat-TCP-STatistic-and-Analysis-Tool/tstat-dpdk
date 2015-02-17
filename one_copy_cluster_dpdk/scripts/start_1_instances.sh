#!/bin/bash

./build/cluster_dpdk -c 0X03 -n 4 --proc-type=auto -- -n 1 -p 0 &
sleep 5

