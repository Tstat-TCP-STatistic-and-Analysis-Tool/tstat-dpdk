#!/bin/bash
# Configure the system to run Tstat-DPDK (see Section 3 of the README). Run it after each reboot.
# NOTE: check that the operations are the ones needed on your machine, and set the parameters below.

# Hugepages: about 1100 hugepages of 2 MB (2.2 GB) for each Tstat instance.
# On NUMA machines, this amount is reserved on each node
HUGEPAGES_MEM=12G

# PCI addresses of the NICs to bind to DPDK (see 'dpdk-devbind.py --status').
# The interfaces must be down
IFACES=("0000:04:00.0" "0000:04:00.1")

set -e

# Set the CPU governor to performance
sudo cpupower frequency-set -g performance

# Reserve and mount the hugepages
sudo dpdk-hugepages.py --pagesize 2M --setup $HUGEPAGES_MEM

# Bind the NICs to the vfio-pci driver (it needs the IOMMU enabled in the kernel command line)
sudo modprobe vfio-pci
for IFACE in "${IFACES[@]}" ; do
	sudo dpdk-devbind.py --bind=vfio-pci $IFACE
done

dpdk-hugepages.py --show
dpdk-devbind.py --status-dev net
