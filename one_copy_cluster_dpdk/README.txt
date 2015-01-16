LIBSTAT-DPDK

REQUIREMENTS *********************************************************************************************************************************************
This software is taylored on Intel 82599 NIC. So it has been tested only on that NICs. Probably will work with every NIC supporting RSS and DPDK.

NOTES ***************************************************************************************************************************************************
1. Please use DPDK 1.7.1 (http://dpdk.org/browse/dpdk/snapshot/dpdk-1.7.1.tar.gz)
2. Install RRDTool to have better experience with Tstat (http://oss.oetiker.ch/rrdtool/) before compiling Tstat.

INSTALLATION *********************************************************************************************************************************************
1) Install DPDK:
	- We suggest to use the getting started guide ( http://dpdk.org/doc/intel/dpdk-start-linux-1.7.0.pdf ). Otherwise DPDK installation directory could not be found by Makefile.
2) Reserve a big number of hugepages to DPDK:
	- echo 6144 >/sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
	- mount -t hugetlbfs nodev /mnt/huge
3) Bind interfaces you want to use with DPDK drivers:
	- modprobe uio
	- insmod build/kmod/igb_uio.ko
	- tools/dpdk_nic_bind.py --bind=igb_uio eth0 ...
4) Set CPU on performance
	cpufreq-set -r -g performance
5) Install Tstat (libtstat in this specifical version is needed):
	- Download from "http://tstat.polito.it/viewvc/software/tstat/branches/tstat-dpdk/tstat-3.0r648/". SVN is available.
	- Go in the directory where you downloaded Tstat
	- ./autogen.sh
	- ./configure --enable-libtstat
	- make
	- make install
6) Install cluster-dpdk
	- Decompress the archive
	- ensure RTE_SDK and RTE_TARGET env variables are set and point to DPDK installation directory, see DPDK Getting Started Guide.
	- make


USAGE: ************************************************************************************************************************************************
To use the cluster please read about Tstat configuration on http://tstat.polito.it/HOWTO.shtml .
By default Tstat configuration file is tstatX.conf/ in the subdirectory tstat-conf where X in the number of the instance.
Infact each Tstat instance has got a different conf file in order to potentially keep separated logs and histograms directory.
Default log directory is tstatX.log.out where X in the number of the instance.
The system produces stats on its performances, and they are stored in tstat-stats/ subdirectory. One file for each instance.

To start the cluster DPDK is recomended to use the provided scripts in the scripts/ directory.
Every script has a sintax like this:
start_X_instances.sh
Where 'X' is the number of instances to be started.
Just ignore the scripts which doesn't respect this name format.
Note that every instance uses two logical cores, so pay attention to have enough cores.

By default cluster_dpdk uses all interfaces bound to DPDK. To modify this behavior pass --use-device or -b option to DPDK EAL.
For starting manually cluster_dpdk instances use this sintax.
	cluster_dpdk -c COREMASK -n NUM --proc-type=auto -- -n TOT_INST -p N_INST
Where:
	1) -c COREMASK : is DPDK core mask. Every instance needs two cores. Be coherent in assigning cores (read: do not reuse cores).
	2) -n NUM : number of memory channels, DPDK parameter.
	3) -n TOT_INST : number of instance of cluster_dpdk you are planning to set up
	4) -p N_INST : number of this instance. Must be between 0 and TOT_INST-1
NOTE: the first two arguments are DPDK EAL ones. Infact each argument is given before the '--' is passed to EAL.
See DPDK getting started guide for further details.

DPDK CONF EXAMPLE SCRIPT ****************************************************************************************************************************************
#THIS SCRIPT STARTS DPDK ENVIROMENT. MUST EXECUTE AS SUPER USER

#Set CPU governor on performance
sudo cpufreq-set -r -g performance

#create enviromente variable (so must execute the script in this way: '. ./start_env.sh' )
export RTE_SDK=/home/testuser/intel-dpdk-enviroment/dpdk-1.7.1
export RTE_TARGET=x86_64-native-linuxapp-gcc

#creatig and mounting hugepages
sudo echo 6144 >/sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
sudo mount -t hugetlbfs nodev /mnt/huge

#loading modules
sudo modprobe uio
sudo insmod $RTE_SDK/$RTE_TARGET/kmod/igb_uio.ko

#Binding interfaces to DPDK drivers
$RTE_SDK/tools/dpdk_nic_bind.py --bind=igb_uio 01:00.0
$RTE_SDK/tools/dpdk_nic_bind.py --bind=igb_uio 01:00.1


