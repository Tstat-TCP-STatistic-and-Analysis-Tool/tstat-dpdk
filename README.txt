Tstat-DPDK
==========

# 1. Description of the software
This program is a particular version of Tstat which uses Intel Data Plane Development Kit to retreive packets from network interfaces (NICs).
It uses a multicore approach to achieve high speed. So it's important to know the number of cores you CPU has.
It starts several **indipendent instances of Tstat**. Each instance will be fed by a portion of the incoming traffic.
So the each Tstat instance produces its own output in different directories.
* To understand Tstat please read: http://tstat.polito.it/HOWTO.shtml
* For information about DPDK please read: http://dpdk.org/doc
* For information about this Readme file and Tstat-DPDK please write to [martino.trevisan@studenti.polito.it](mailto:martino.trevisan@studenti.polito.it)

# 2. Requirements
* A machine with 82599 based network interfaces.
* A Debian based Linux distribution with kernel >= 2.6.3
* Linux kernel headers: to install type
```bash
	sudo apt-get install linux-headers-$(uname -r)
```
* Several package needed before installing this software:
  * DPDK needs: `make, cmp, sed, grep, arch, glibc.i686, libgcc.i686, libstdc++.i686, glibc-devel.i686, python`
  * Tstat needs: `libpcap-dev, rrdtool, librrd-dev, cpufrequtils, autoconf, libtool, subversion`



# 3. Installation

## 3.1 Install DPDK
Install DPDK 1.7.1. With other versions is not guaranted it works properly.
Make the enviroment variable `RTE_SDK` and `RTE_TARGET` to point respectively to DPDK installation directory and compilation target.
For documentation and details see http://dpdk.org/doc/intel/dpdk-start-linux-1.7.0.pdf
```bash
	wget http://dpdk.org/browse/dpdk/snapshot/dpdk-1.7.1.tar.gz
	tar xzf dpdk-1.7.1.tar.gz
	cd dpdk-1.7.1
	export RTE_SDK=$(pwd)
	export RTE_TARGET=x86_64-native-linuxapp-gcc
	make install T=$RTE_TARGET
	cd ..
```
**NOTE:** if you are running on a i686 machine, please use `i686-native-linuxapp-gcc` as `RTE_TARGET`

## 3.2 Install Tstat-DPDK
This operation requires super user privileges.
```bash
	svn co http://tstat.polito.it/svn/software/tstat/branches/tstat-dpdk/
	cd tstat-dpdk/tstat-3.0r648
	./autogen.sh
	./configure --enable-libtstat
	make
	sudo make install
	cd ../one_copy_cluster_dpdk
	make clean && make
	cd ../..
```

**NOTE 1:** be sure that when running `./configure --enable-libtstat` you get this output
```
-------------------------------------------------
  tstat Version 3.0
  -lpcap -lpthread -lm  -lrrd

  Prefix: '/usr/local'

  Package features:
    - pcap      yes
    - zlib      no
    - rrd       yes
    - libtstat  yes
--------------------------------------------------
```
This means that `libpcap` and `rrdtool` have been detected and `libtstat` compilation enabled.

**NOTE 2:** if, when running Tstat-DPDK, you encounter an error like this
```
	error while loading shared libraries: libtstat.so.0: cannot open shared object file: No such file or directory
```
run this command to make the enviroment to find `libstat` shared object:
```bash
	export LD_LIBRARY_PATH=/usr/local/lib
```

# 4. Configuration of the machine
Before running Tstat-DPDK there are few things to do:
## 4.1 Reserve a big number of hugepages to DPDK
The commands below reserve 6144 hugepages. Reserve about 512 * N, where N is the number of cores of your machine. The size of each huge page is 2MB. Check to have enough RAM on your machine.
```bash
	sudo su
	echo 6144 >/sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
	mkdir -p /mnt/huge
	mount -t hugetlbfs nodev /mnt/huge
```
## 4.2 Set CPU on performance governor
To achieve the best performances, your CPU must run always at the highest speed. You need to have installed `cpufrequtils` package.
```bash
	sudo cpufreq-set -r -g performance
```
## 4.3  Bind the interfaces you want to use with DPDK drivers
It means that you have to load DPDK driver and associate it to you network interface. Remember to set `RTE_SDK` and `RTE_TARGET`.
```bash
	sudo modprobe uio
	sudo insmod $RTE_SDK/$RTE_TARGET/kmod/igb_uio.ko
	sudo $RTE_SDK/tools/dpdk_nic_bind.py --bind=igb_uio $($RTE_SDK/tools/dpdk_nic_bind.py --status | sed -rn 's,.* if=([^ ]*).*igb_uio *$,\1,p')
```
To check if your network interfaces have been properly set by the Intel DPDK enviroment run:
```bash
	sudo $RTE_SDK/tools/dpdk_nic_bind.py --status
```
You should get an output like this, which means that the first two interfaces are running under DPDK driver, while the third is not:
```
Network devices using DPDK-compatible driver
============================================
0000:04:00.0 'Ethernet Controller 10-Gigabit X540-AT2' drv=igb_uio unused=
0000:04:00.1 'Ethernet Controller 10-Gigabit X540-AT2' drv=igb_uio unused=
Network devices using kernel driver
===================================
0000:03:00.0 'NetXtreme BCM5719 Gigabit Ethernet PCIe' if=eth0 drv=tg3 unused=igb_uio *Active*
```
## 4.4  Set up Tstat configuration
In the directory `installation-dir/one_copy_cluster_dpdk/tstat-conf` there is a set of files: `tstat00.conf`, `tstat01.conf` ecc...
This are the configuration files of each Tstat process. Configure it as you prefer; the default configuration includes all plugins and RRDTool engine.
Having one configuration file for each Tstat istance let you to specify manually RRDTool directory. This is necessary to make it work (but RRDTool usage it's not mandatory).


# 5. Usage
## 5.1 How to start it
To start Tstat-DPDK **go in `installation-dir/one_copy_cluster_dpdk`.** Then use the provided script to start it.
Decide how many instances of Tstat-DPDK you want to run in parallel: a Tstat instance needs two cores.
If you want to start 2 instances, type:
```bash
	./scripts/start_2_istances.sh
```
The system approximately each one seconds prints statstics about its performances, a line each instance.
The information are about average, maximum, and standard deviation of per-packet processing time. There is the number of TCP and UDP flows closed.
The output includes also the incoming rate in Mpps and the eventual packets' losing rate (in case of losses). You can see also the occupation of the internal buffer.
The ouput has got this form.
```
Instance: 0 Avg: 1.023us Max: 35.127us StdDev: 14.452 TCP cl.: 42 UDP cl.: 213 Rate: 0.897Mpps, Loss: 0Mpps Buffer occupation:   0%
Instance: 1 Avg: 1.045us Max: 44.345us StdDev: 45.713 TCP cl.: 37 UDP cl.: 175 Rate: 0.813Mpps, Loss: 0Mpps Buffer occupation:   0%
```
## 5.2 How to stop it
To stop Tstat-DPDK, type:
```bash
	./scripts/quit_instances.sh
```
The system will quit and it will print overall statistics on its working divided by network interface and by instance.
The output has this form.
```
PORT: 0 Rx: 0 Drp: 0 Tot: 0 Perc: -nan%
	Queue 0 Rx: 0 Drp: 0 Tot: 0 Perc: -nan%
	Queue 1 Rx: 0 Drp: 0 Tot: 0 Perc: -nan%
PORT: 1 Rx: 0 Drp: 0 Tot: 0 Perc: -nan%
	Queue 0 Rx: 0 Drp: 0 Tot: 0 Perc: -nan%
	Queue 1 Rx: 0 Drp: 0 Tot: 0 Perc: -nan%

ISTANCE: 0 Rx: 0 Drp: 0 Tot: 0 Perc: -nan%
TSTAT STATISTICS:
Total packets: 0
TCP flows: 0
UDP flows: 0
Time elapsed: 2880.317s

ISTANCE: 1 Rx: 0 Drp: 0 Tot: 0 Perc: -nan%
TSTAT STATISTICS:
Total packets: 0
TCP flows: 0
UDP flows: 0
Time elapsed: 2879.215s
```
## 5.3 Log files directory
Tstat produces several logs file for different events seen on the network.
Log files are keep separate for each instance and they are put in `installation-dir/one_copy_cluster_dpdk/tstatXX.log.out` where `XX` is the number of Tstat instance.
It cannot be modified.
RRD files directory is specified in the configuration files of Tstat.
## 5.4 Statistics of the system
In the directory `installation-dir/one_copy_cluster_dpdk/tstat-stats` there is a set of file called `statsXX.txt`.
Each instance writes in its own file (the `XX` in the name of the file is its number) statistics while it is running.
There are 3 columns which are respectively the mean, the max and the standard deviation of the per packet processing time.

