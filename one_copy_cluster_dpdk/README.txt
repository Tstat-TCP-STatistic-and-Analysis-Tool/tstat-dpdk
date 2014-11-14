LIBSTAT-DPDK

REQUIREMENTS * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
This software is taylored in Intel 82599. So it has been tested only on that NICs. Probably will work with every NIC supporting RSS.


INSTALLATION * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
1) Install DPDK:
	- We suggest to use the getting started guide ( http://dpdk.org/doc/intel/dpdk-start-linux-1.7.0.pdf ). Otherwise DPDK installation directory could not be found by Makefile.
2) Reserve a big number of hugepages to DPDK:
	- echo 6144 >/sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
	- mount -t hugetlbfs nodev /mnt/huge
3) Bind interface you want to use with DPDK drivers:
	- modprobe uio
	- insmod build/kmod/igb_uio.ko
	- tools/dpdk_nic_bind.py --bind=igb_uio eth0 ...
4) Set CPU on performance
	cpufreq-set -r -g performance
5) Install Tstat (libtstat is needed):
	- Download from http://tstat.polito.it/viewvc/software/tstat/branches/tstat-dpdk/tstat-3.0r648/. SVN is available.
	- tar -xzvf tstat-2.x.y.tar.gz
	- cd tstat-2.x.y
	- ./autogen.sh
	- ./configure --enable-libtstat
	- make
	- make install
6) Install cluster-dpdk
	- Decompress the archive
	- ensure RTE_SDK and RTE_TARGET env variables are set, see DPDK doc.
	- make


USAGE: * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
To use the cluster please read about Tstat configuration on http://tstat.polito.it/HOWTO.shtml .
By default Tstat configuration file is tstatX.conf where X in the number of the instance.
Infact each Tstat instance has got a different conf file in order to potentially keep separated logs and histograms directory.
Default log directory is tstatX.log.out where X in the number of the instance.

To start the cluster DPDK is recomended to use the provided scripts.
Every script has a sintax like this:
start_X_instances.sh
Where 'X' is the number of instances to be started.
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
