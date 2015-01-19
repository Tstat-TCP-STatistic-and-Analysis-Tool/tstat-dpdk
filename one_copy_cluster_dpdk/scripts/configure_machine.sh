#THIS SCRIPT CONFIGURES THE SYSTEM TO RUN TSTAT-DPDK
#NOTE 1: Please check if the operation are the ones needed in you machine
#NOTE 2: Please define RTE_SDK and RTE_TARGET according to your DPDK installation

#Set CPU governor on performance
sudo cpufreq-set -r -g performance

#Creating and mounting hugepages
sudo echo 6144 >/sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
sudo mount -t hugetlbfs nodev /mnt/huge

#Loading modules
sudo modprobe uio
sudo insmod $RTE_SDK/$RTE_TARGET/kmod/igb_uio.ko

#Binding interfaces to DPDK drivers
#NOTE 3: if you don't want to bind all the DPDK-supported interfaces to DPDK, modify the line below.
sudo $RTE_SDK/tools/dpdk_nic_bind.py --bind=igb_uio $($RTE_SDK/tools/dpdk_nic_bind.py --status | sed -rn 's,.* if=([^ ]*).*igb_uio *$,\1,p')
