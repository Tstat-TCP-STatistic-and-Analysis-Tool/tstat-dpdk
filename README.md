Tstat-DPDK Quick Start Guide
============================

* [1. Introduction](#intro)
* [2. Installation](#inst)
    * [2.1 Installing Intel DPDK](#inst-dpdk)
    * [2.2 Installing Tstat DPDK](#inst-tstat-dpdk)
        * [2.2.1 Installing last version of Tstat](#comp-libtstat)
        * [2.2.2 Installing the DPDK Load Balancer](#comp-dpdk-cluster)
* [3. System Comfiguration](#conf)
    * [3.1 Reserve a large number of hugepages to DPDK](#conf-hugepage)
    * [3.2 Force CPUs clock to remain high](#conf-cpu)
    * [3.3 Disable Address Layout Space Randomization](#alsr)
    * [3.4 Bind NICs to DPDK driver](#conf-nics)
    * [3.5 Set up Tstat configurations](#conf-libtstat)    
* [4. Run Tstat-DPDK](#run)
    * [4.1 Start Tstat Instances](#start)
    * [4.2 Stop Tstat Instances](#stop)
    * [4.3 Log Files Directory](#logs)
    * [4.4 System Performance Logs](#logs-perf)
    * [4.5 Advanced load balancing](#load-balance)



# 1. <a name="intro"></a> Introduction

This document is a quick start guide for Tstat-DPDK, a Tstat version supporting Intel Data Plane Development Kit ([DPDK](http://dpdk.org/)) network packet acquisition framework. Differently from the standard Tstat version, Tstat-DPDK supports multicore processing resulting in significant performance enhancements.

Tstat-DPDK consists of in a DPDK wrapper available [official SVN repository](http://tstat.polito.it/svn/software/tstat/branches/tstat-dpdk/)

```bash
    svn co http://tstat.polito.it/svn/software/tstat/branches/tstat-dpdk/
```

Essentially, the DPDK wrapper acts as **load balancer**, reading the aggregate traffic and dispatching packets to different **Tstat instances**, i.e., different Tstat processes. By associating different processes to different cores the overall system can sustain multiple 10Gbps of input aggregate traffic.

Here below we provide a step by step guide to setup the overall system.
This guide is not meant to be a fine grained tutorial about Intel DPDK not Tstat functioning. For more details, we refer the reader to

* official [Tstat Howto](http://tstat.polito.it/HOWTO.shtml)
* official Intel [DPDK website](http://dpdk.org/doc) and [quick start](http://dpdk.org/doc/quick-start)
* For information about this Readme file and Tstat-DPDK please write to [martino.trevisan@studenti.polito.it](mailto:martino.trevisan@studenti.polito.it) or the offical [Tstat mailing list](mailto:tstat@tlc.polito.it)

# 2. <a name="inst"></a>Installation

A few general dependencies are required before to start

* Intel DPDK requires  **Intel 82599 based network interfaces**
* A Linux distribution with kernel >= 3.14 (we tested it on Debian)
* Linux kernel headers: to install run
```bash
    sudo apt-get install linux-headers-$(uname -r)
```

* Several packages needed before installing this software:

  * DPDK needs: `make, cmp, sed, grep, arch, glibc.i686, libgcc.i686, libstdc++.i686, glibc-devel.i686, python`
  * Tstat needs: `libpcap-dev, rrdtool, librrd-dev, cpufrequtils, autoconf, libtool`


## 2.1 <a name="inst-dpdk"></a>Installing Intel DPDK

We highly recommend to use **Install DPDK v1.8.0**. With other versions it is **NOT** guaranted to work properly.

```bash
	wget http://dpdk.org/browse/dpdk/snapshot/dpdk-1.8.0.tar.gz
	tar xzf dpdk-1.8.0.tar.gz
	cd dpdk-1.8.0
	export RTE_SDK=$(pwd)
	export RTE_TARGET=x86_64-native-linuxapp-gcc
	make install T=$RTE_TARGET
	cd ..
```

**NOTES:** 

* `RTE_SDK` and `RTE_TARGET` are two environment variables used by DPDK to handle DPDK applications. These variables have to point respectively to DPDK installation directory and compilation target.
* if you are running on a i686 machine, please use `i686-native-linuxapp-gcc` as `RTE_TARGET`
* For more information we refer the reader to the [official documentation](http://dpdk.org/doc/guides-1.8/linux_gsg/index.html)



## 2.2 <a name="inst-tstat-dpdk"></a>Installing Tstat DPDK

This step requires to compile both Tstat and the DPDK load-balancer application. They are both available under Tstat SVN repository.   

As previously pointed out, the DPDK wrapper acts as a load balancer in the overall system. From one side it interfaces with the DPDK framework, while on the other it delivers data packets to Tstat processes. For this latter functionality it relies on LibTstat. In the following we refer to the root folder of the repository with `$TSTATDPDK`.

## 2.2.1 <a name="comp-libtstat"></a>Installing last version of Tstat

In this step you need to download and install the latest Tstat version from the official SVN repository, compile it, and install `libtstat`

```bash
	svn co http://tstat.polito.it/svn/software/tstat/trunk/
	cd $TSTATDPDK/trunk
	./autogen.sh
	./configure --enable-libtstat --enable-rrdthread
	make
	sudo make install
```

***Note:*** `configure` should print the following message is the set up is correct.
```
[...]
-------------------------------------------------
  tstat Version 3.0
  -lpcap -lpthread -lm  -lrrd

  Prefix: '/usr/local'

  Package features:
    - pcap      yes
    - zlib      no
    - rrd       yes
    - libtstat  yes
    - rrdthread yes
--------------------------------------------------
```
This indicates both `libpcap` and `rrdtool` libraries have been correctly found and Tstat will be compiled as a library with rrdthread feature active.

## 2.2.2 <a name="comp-dpdk-cluster"></a>Installing the DPDK Load Balancer
```bash
	svn co http://tstat.polito.it/svn/software/tstat/branches/tstat-dpdk 
	cd $TSTATDPDK/tstat-dpdk
	make
```

# 3. <a name="conf"></a>System Configuration

Before running Tstat-DPDK there are few things to do.

**Note:** To simplity the setup, all the steps can be automatically performed using the script `$TSTATDPDK/tstat-dpdk/scripts/configure_machine.sh`.
Please verify that the script is doing the right operation for your system.

## 3.1 <a name="conf-hugepage"></a>Reserve a large number of hugepages to DPDK

To improve memory handling, it is highly recommended to increase the number of `hugepages` used by the linux kernel. A typical set up would suggest to use `512 * N` where `N` is the number of cores (considered as sum of physical and virtual). The size of each huge page is 2MB.

For instance, for a 8 core system this would result in `4096 hugepages`, i.e., 8GB of memory. In scenarios not suffering of strong memory constraints, we recommend to use `6144 hugepages`, i.e., 12GB of memory

```bash
	sudo bash -c "echo 6144 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages"
	sudo mkdir -p /mnt/huge
	sudo mount -t hugetlbfs nodev /mnt/huge
```

## 3.2 <a name="conf-cpu"></a>Force CPUs clock to remain high

To achieve the best performance, your CPU must always run at the highest speed. You need to have installed `cpufrequtils` package.

```bash
	sudo cpufreq-set -r -g performance
```

## 3.3 <a name="alsr"></a>Disable Address Layout Space Randomization
Address Layout Space Randomization (ALSR) is a feature of Linux systems, that unfortunately sometimes makes DPDK application not to start properly (more info [here](http://dpdk.org/doc/guides-1.8/prog_guide/multi_proc_support.html#multi-process-limitations)).
So we suggest to disable it by means of this command:
```bash
	sudo bash -c "echo 0 > /proc/sys/kernel/randomize_va_space"
```

## 3.4 <a name="conf-nics"></a> Bind NICs to DPDK driver

In order to use the DPDK framework on a NIC we need to change the driver associated to the network card. The driver is located within the `kmod` folder of the DPDK version compiled (recall the configuration of the `RTE_SDK` and `RTE_TARGET` used at compile time).

The following command enable DPDK drivers for all DPDK-supported NICs

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
**NOTE:** If for any reason you don't want to bind all the DPDK-supported interfaces to DPDK enviroment, use the `dpdk_nic_bind.py` as described in the [DPDK getting started guide](http://dpdk.org/doc/intel/dpdk-start-linux-1.7.0.pdf).

## 3.5  <a name="conf-libtstat"></a>Set up Tstat configuration

Since multiple Tstat processes will be executed separately, the system will produce separate statistics for each instance. As such, we need to configure each instance separately.

For instance, in `$TSTATDPDK/tstat-dpdk/tstat-conf` we provide a set of example configuration files (`tstat00.conf`, `tstat01.conf`, etc.) which include all Tstat plugins and log files, RRDTool output stats and. Fell free to configure the software as you like.
For more information please refer to the [Tstat HOWTO](http://tstat.tlc.polito.it/HOWTO.shtml#libtstat_library)


# 4. <a name="run"></a>Run Tstat-DPDK

**Note:** if, when running Tstat-DPDK, you encounter an error like this
```
    error while loading shared libraries: libtstat.so.0: cannot open shared object file: No such file or directory
```
make sure the environment  finds `libtstat`  shared object by running:
```bash
    export LD_LIBRARY_PATH=/usr/local/lib
```

Sometimes it's necessary to make the changes permanent by running `ldconfig` command, but this is dependent on your setup (e.g. you are using a remote terminal via `ssh`)

## 4.1 <a name="start"></a>Start Tstat instances

First of all, you need to decide how many Tstat instances you want to run in parallel. You cannot set up more instances than available cores.

If you want to start 2 instances, type:
```bash
	sudo $TSTATDPDK/tstat-dpdk/build/tstat_dpdk -c 1 -n 4 -- -m 2
```
This executes Tstat-DPDK splitting the traffic among 2 separate Tstat processes.

It might be useful to run this command detached from the shell (ending it with `&`).

**Command line options**

`-m` option controls the **number of pararallel instances**.

`-c` and `-n` are DPDK-related. **Do not change them.** They are related to CPU cores and memory channels to use. For more info refer to [DPDK getting started guide](http://dpdk.org/doc/intel/dpdk-start-linux-1.7.0.pdf).

`-e` option set **extra header bytes** to skip in order to arrive to the IP header. Example: set to 4 in case of a vlan tag. 

`-v` option sets the NIC's flags for handling VLAN doubled-tagged traffic. Flags are: `RTE_ETH_RX_OFFLOAD_VLAN_STRIP`, `RTE_ETH_RX_OFFLOAD_QINQ_STRIP`,
    `RTE_ETH_VLAN_STRIP_OFFLOAD`, `RTE_ETH_QINQ_STRIP_OFFLOAD`. 


Approximatively every second, the DPDK load-balancer prints statistics. This includes avg, max, stdev of per packet processing time, number of TCP and UDP flows closed, incoming rate in Mpps, the eventual packet loss rate (in case of losses),  and utilization of the internal buffers used by Tstat-DPDK to manage packet acquisition.

Below you have an example of this output
```
Instance: 0 Avg: 1.023us Max: 35.127us StdDev: 14.452 TCP cl.: 42 UDP cl.: 213 Rate: 0.897Mpps, Loss: 0Mpps Buffer occupation:   0%
Instance: 1 Avg: 1.045us Max: 44.345us StdDev: 45.713 TCP cl.: 37 UDP cl.: 175 Rate: 0.813Mpps, Loss: 0Mpps Buffer occupation:   0%
```

## 4.2 <a name="stop"></a>Stop Tstat Instances


When quitting, the system prints overall statistics on its working divided by network interface and by instance.
The output has this form.
```
PORT: 0 Rx: 0 Drp: 0 Tot: 0 Perc: -nan%
	Queue 0 Rx: 0 Drp: 0 Tot: 0 Perc: -nan%
	Queue 1 Rx: 0 Drp: 0 Tot: 0 Perc: -nan%
PORT: 1 Rx: 0 Drp: 0 Tot: 0 Perc: -nan%
	Queue 0 Rx: 0 Drp: 0 Tot: 0 Perc: -nan%
	Queue 1 Rx: 0 Drp: 0 Tot: 0 Perc: -nan%

INSTANCE: 0 Rx: 0 Drp: 0 Tot: 0 Perc: -nan%
TSTAT STATISTICS:
Total packets: 0
TCP flows: 0
UDP flows: 0
Time elapsed: 2880.317s

INSTANCE: 1 Rx: 0 Drp: 0 Tot: 0 Perc: -nan%
TSTAT STATISTICS:
Total packets: 0
TCP flows: 0
UDP flows: 0
Time elapsed: 2879.215s
```
## 4.3 <a name="logs"></a>Log Files Directory
Tstat produces several log files for different events seen on the network.
Log files are kept separated for each instance and they are put in `$TSTATDPDK/tstat-dpdk/tstat-logs/tstatXX.log.out` where `XX` is the number of Tstat instance.
If you want to ovverride this setting you can use the `-s <logdir>` option in the Tstat configuration files.

RRD database directory is specified in the configuration files of Tstat, so you can configure its location by means of the `-r <rrd_dir>` option (recall that each instance must have its different database in different directories).

## 4.4 <a name="logs-perf"></a>System Performance Logs
In the directory `$TSTATDPDK/tstat-dpdk/tstat-stats` there is a set of file called `statsXX.txt`.
Each instance writes in its own file (the `XX` in the name of the file is its number) statistics while it is running.

This kind of output can be disabled by editing the source code -- 5th line of `src/tstat-dpdk-params.h` and commenting the `#define DEBUG` statement.

## 4.5 <a name="load-balance"></a>Advanced load balancing
It is possible to configure the load balancing rule to optimize particular network setups.

The default load balancing rule ensures that packets belonging to the same flow will be processed by the same Tstat instance.
In this way Tstat can produce per-flow consistent statistics.
Nevertheless the same host can appear in different Tstat instances for different TCP/UDP connections.

If you set up Tstat-DPDK in the links connecting a local network to the internet, you can setup an advanced load balancing rule.
Since it might be useful to have **the traffic of the same local network host to appear always in the same Tstat instance**, you can configure load balancing as follows:

* The interface sniffing the traffic from the **outgoing link** loadbalances the traffic according to **source IP** address.
* The interface sniffing the traffic from the **incoming link** loadbalances the traffic according to **destination IP** address.

To perform this action you have to modify few lines in the source code and then recompile as follows:

**1.** In `$TSTATDPDK/tstat-dpdk/src/tstat-dpdk-params.h` file, find these lines

```
	/* Use the next 2 variables to set up port direction */
	static struct port_dir port_directions [] = {};
	static uint8_t nb_port_directions = 0;
```
**2.** Modify them to respect your network configuration, like in this example:

```
	/* Use the next 2 variables to set up port direction */
	static struct port_dir port_directions [] = {
		{ .pci_address = "01:00.0", .is_out=0 },
		{ .pci_address = "01:00.1", .is_out=1 },
	};
	static uint8_t nb_port_directions = 2;
```

Where each entry in `port_directions` array describes a NIC indicating its PCI address (in the shown format) and either it is incoming or outgoing.

**3.** Recompile the loadbalancer just typing `make clean && make` in `$TSTATDPDK/tstat-dpdk/` folder.
