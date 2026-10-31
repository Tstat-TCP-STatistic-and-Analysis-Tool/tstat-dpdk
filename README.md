Tstat-DPDK Quick Start Guide
============================

* [1. Introduction](#intro)
* [2. Installation](#inst)
    * [2.1 Installing Intel DPDK](#inst-dpdk)
    * [2.2 Installing Tstat DPDK](#inst-tstat-dpdk)
        * [2.2.1 Installing Libtstat](#comp-libtstat)
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
    * [4.4 Merging the logs of the instances](#merge)



# 1. <a name="intro"></a> Introduction

This document is a quick start guide for Tstat-DPDK, a Tstat version supporting Intel Data Plane Development Kit ([DPDK](http://dpdk.org/)) network packet acquisition framework. Differently from the standard Tstat version, Tstat-DPDK supports multicore processing resulting in significant performance enhancements.

Tstat-DPDK consists of in a DPDK wrapper for Tstat

```bash
    git clone git@github.com:Tstat-TCP-STatistic-and-Analysis-Tool/tstat-dpdk.git
```

Essentially, the DPDK wrapper acts as **load balancer**, reading the aggregate traffic and dispatching packets to different **Tstat instances**, i.e., different Tstat processes. By associating different processes to different cores the overall system can sustain multiple 10Gbps of input aggregate traffic.

Here below we provide a step by step guide to setup the overall system.
This guide is not meant to be a fine grained tutorial about Intel DPDK not Tstat functioning. For more details, we refer the reader to

* the [Tstat wiki](https://github.com/Tstat-TCP-STatistic-and-Analysis-Tool/tstat/wiki)
* official Intel [DPDK website](http://dpdk.org/doc) and [quick start](http://dpdk.org/doc/quick-start)
* For information about this Readme file and Tstat-DPDK please write to [martino.trevisan@dia.units.it](mailto:martino.trevisan@dia.units.it)

# 2. <a name="inst"></a>Installation

A few general dependencies are required before starting

* Intel DPDK requires  **Intel 82599 based network interfaces**
* A Linux distribution with kernel >= 3.14 (we tested it on Ubuntu)
* Linux kernel headers: to install run
```bash
    sudo apt-get install linux-headers-$(uname -r)
```

* Several packages needed before installing this software:

  * DPDK needs: `make, cmp, sed, grep, arch, glibc.i686, libgcc.i686, libstdc++.i686, glibc-devel.i686, python`
  * Tstat needs: `build-essential, libpcap-dev, libldns-dev, libssl-dev, librrd-dev`
  * The system configuration and the tools need: `cpufrequtils, libxml2-dev` (for `rrd_merger`), `bc` and `wireshark-common` (`mergecap`, for the log mergers)

  On Ubuntu, the Tstat dependencies can be installed with:
```bash
    sudo apt-get install build-essential libpcap-dev libldns-dev libssl-dev librrd-dev
```


## 2.1 <a name="inst-dpdk"></a>Installing Intel DPDK

We highly recommend to use **Install DPDK v23.11.1**. With other versions it is **NOT** guaranted to work properly.

```bash
DPDK_VERS="23.11.1"

mkdir -p $HOME/dpdk
wget -P $HOME/dpdk https://fast.dpdk.org/rel/dpdk-${DPDK_VERS}.tar.xz

pushd $HOME/dpdk
  tar xJf dpdk-${DPDK_VERS}.tar.xz
  pushd dpdk-stable-${DPDK_VERS}
    meson setup build
    pushd build
      ninja
      sudo meson install
      sudo ldconfig
popd
popd
popd
```

**NOTES:** 

* `RTE_SDK` and `RTE_TARGET` are two environment variables used by DPDK to handle DPDK applications. These variables have to point respectively to DPDK installation directory and compilation target.
* if you are running on a i686 machine, please use `i686-native-linuxapp-gcc` as `RTE_TARGET`
* For more information we refer the reader to the [official documentation](http://dpdk.org/doc/)



## 2.2 <a name="inst-tstat-dpdk"></a>Installing Tstat DPDK

This step requires to compile both Tstat and the DPDK load-balancer application. They are both available under Tstat Git repositories.   

As previously pointed out, the DPDK wrapper acts as a load balancer in the overall system. From one side it interfaces with the DPDK framework, while on the other it delivers data packets to Tstat processes. For this latter functionality it relies on LibTstat. In the following we refer to the root folder of the repository with `$TSTATDPDK`.

## 2.2.1 <a name="comp-libtstat"></a>Installing Libtstat

In this step you need to download the latest Tstat version from the official Git repository, compile it as a library (`libtstat`) and install it.
Tstat is built with a plain Makefile. Two build options, both enabled by default, are needed by Tstat-DPDK:

* `RRD_THREADED` writes the RRD files from a separate thread, so that RRD updates do not stall the packet processing
* `PACKET_STATS` adds the statistics on the first segments of each flow (advanced set of `log_tcp_complete` and extra columns of `log_udp_complete`)

Do not set them to `0` when building Libtstat for Tstat-DPDK.

```bash
	mkdir -p $HOME/tstat
	pushd $HOME/tstat
	  git clone https://github.com/Tstat-TCP-STatistic-and-Analysis-Tool/tstat trunk
	popd
	
	pushd $HOME/tstat/trunk
	  make all libtstat
	  sudo make install
	  sudo ldconfig
	popd
```

`make install` installs the `tstat` executable in `/usr/local/bin`, and `libtstat.so`, `libtstat.h` and `libtstat.pc` under `/usr/local/lib`, `/usr/local/include` and `/usr/local/lib/pkgconfig`.
The installation prefix can be changed with `sudo make install PREFIX=/opt/tstat`.
Run `tstat -v` to check the installed version.
For more details, see the [Installation](https://github.com/Tstat-TCP-STatistic-and-Analysis-Tool/tstat/wiki/Installation) page of the Tstat wiki.

## 2.2.2 <a name="comp-dpdk-cluster"></a>Installing the DPDK Load Balancer
```bash
	mkdir -p $HOME/tstat
	pushd $HOME/tstat
	  git clone https://github.com/Tstat-TCP-STatistic-and-Analysis-Tool/tstat-dpdk
	popd
	
	pushd $HOME/tstat/tstat-dpdk
	  make
	  pushd tools/rrd_merger
	    make
	  popd
	popd
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
	IFACES=("0000:85:00.0" ... )
	
	#Loading modules
	sudo modprobe uio
	sudo modprobe vfio-pci
	
	#Binding interfaces to DPDK drivers
	for IFACE in "${IFACES[@]}" ; do
	    sudo $RTE_SDK/usertools/dpdk-devbind.py --bind=vfio-pci $IFACE
	done
```

To check if your network interfaces have been properly set by the Intel DPDK enviroment run:
```bash
	sudo $RTE_SDK/usertools/dpdk-devbind.py --status
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

For instance, in `$TSTATDPDK/tstat-dpdk/tstat-conf` we provide a set of example configuration files (`tstat00.conf`, `tstat01.conf`, etc.), one for each instance, which set the internal networks (`subnets.txt`), the log files to produce (`runtime.conf`), the RRD output (`rrd.conf`) and the output directories. By default, instance `i` reads `tstat-conf/tstatXX.conf` (with `XX` = `i` on two digits) relative to the current directory: you can use other files with the `-c` option (see [Start Tstat instances](#start)). Feel free to configure the software as you like: the available options are described in the [Tstat wiki](https://github.com/Tstat-TCP-STatistic-and-Analysis-Tool/tstat/wiki).


# 4. <a name="run"></a>Run Tstat-DPDK

**Note:** if, when running Tstat-DPDK, you encounter an error like this
```
    error while loading shared libraries: libtstat.so.1: cannot open shared object file: No such file or directory
```
make sure the environment  finds `libtstat`  shared object by running:
```bash
    export LD_LIBRARY_PATH=/usr/local/lib
```

or update the cache of the dynamic linker with `sudo ldconfig` (needed once after installing Libtstat).

## 4.1 <a name="start"></a>Start Tstat instances

First of all, you need to decide how many Tstat instances you want to run in parallel.
Each instance is a process with two threads, each one running on a dedicated core: the *producer* reads the packets of the instance from the NICs, and the *consumer* passes them to Tstat.
Instance `i` uses cores `2*i` (producer) and `2*i+1` (consumer), so `N` instances use cores `0` ... `2N-1` and you cannot run more instances than half of the available cores.
Avoid running other CPU intensive processes on these cores.

If you want to start 2 instances, type:
```bash
	sudo $TSTATDPDK/tstat-dpdk/build/tstat_dpdk -c 1 -n 4 -- -m 2
```
This executes Tstat-DPDK splitting the traffic among 2 separate Tstat processes.

It might be useful to run this command detached from the shell (ending it with `&`).

**Command line options**

`-m` option controls the **number of pararallel instances**.

`-c <format>` sets the **Tstat configuration file** of each instance. The argument is a `printf`-like format, where `%d` (or, e.g., `%02d`) is replaced by the number of the instance; it must contain exactly one such conversion (use `%%` for a literal `%`). The default is `tstat-conf/tstat%02d.conf`, i.e., `tstat-conf/tstat00.conf`, `tstat-conf/tstat01.conf`, etc., relative to the current directory. For example, `-c /etc/tstat/instance%d.conf` uses `/etc/tstat/instance0.conf`, `/etc/tstat/instance1.conf`, etc.

The `-c` and `-n` options before `--` are DPDK-related. **Do not change them.** They are related to CPU cores and memory channels to use. For more info refer to [DPDK getting started guide](http://dpdk.org/doc/intel/dpdk-start-linux-1.7.0.pdf).

`-e` option set **extra header bytes** to skip in order to arrive to the IP header. Example: set to 4 in case of a vlan tag. 

`-v` option sets the NIC's flags for handling VLAN doubled-tagged traffic. Flags are: `RTE_ETH_RX_OFFLOAD_VLAN_STRIP`, `RTE_ETH_RX_OFFLOAD_QINQ_STRIP`,
    `RTE_ETH_VLAN_STRIP_OFFLOAD`, `RTE_ETH_QINQ_STRIP_OFFLOAD`. 


Every second, Tstat-DPDK prints statistics about the last second. If the output is a terminal, the screen is redrawn from the top at each update; otherwise (e.g., when redirected to a file), the statistics are appended.
```
═══ Tstat-DPDK ═══ 2026-10-30 20:18:29 ═══ uptime 0d 02:14:07 ═══════════
Lost since start: 3 105 packets (0.001%), last loss: 2026-10-30 20:18:29

PORT        Mbps      kpps      missed      errors    drop%
   0       959.0     120.2           0           0    0.000
   1       614.8      62.8           0           0    0.000
 all      1573.8     183.0           0           0    0.000

INST        Mbps      kpps   share%   ring%       drops    drop%
   0       536.9      62.1    34.1   78.40       1 482    2.387  LOSS
   1       517.6      58.6    32.9   61.25           0    0.000  RING>50%
   2       327.3      37.1    20.8    0.00           0    0.000
   3       190.9      21.6    12.1    0.00           0    0.000
```
For each port (and for all of them):
* `Mbps` and `kpps`: rate of the packets received by the NIC.
* `missed`: packets dropped by the NIC, because its rx queues are full or because no buffer is available in the mempool.
* `errors`: erroneous packets.
* `drop%`: percentage of missed and erroneous packets.

For each instance:
* `Mbps` and `kpps`: rate of the packets read by the producer from the NICs.
* `share%`: percentage of the packets of all the instances analyzed by this instance, which shows how RSS balances the traffic.
* `ring%`: maximum occupancy, in the last second, of the ring between the producer and the consumer. A high value means that the consumer cannot keep up with the traffic.
* `drops` and `drop%`: packets dropped by the producer, because the ring is full.

The line under the title reports the packets lost since the start, by the NICs (missed and erroneous) and by the instances (dropped), their percentage over the packets that reached the NICs, and the time of the last loss (`never` if no packet has been lost).
The rows with losses in the last second end with `LOSS`, and the instances whose ring was more than 50% full (`RING_WARN_PERC` in `src/tstat-dpdk-params.h`) end with `RING>50%`, as they are about to lose packets. On a terminal, these rows are also colored in red and yellow, respectively.
As the probes always lose some packets when starting, the losses until the first update (after one second) are not counted, neither in the rows nor in the total.

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

## 4.4 <a name="merge"></a>Merging the logs of the instances
Since each Tstat instance writes its own logs, the directory `$TSTATDPDK/tstat-dpdk/tools` contains two scripts to merge them, which can be run periodically (e.g., with `cron`):

```bash
	tools/log_merger.sh <out_dir> tstat_log00 tstat_log01 ...
	tools/log_merger_hash.sh <salt> <out_dir> tstat_log00 tstat_log01 ...
```

For each hourly directory present in all the instances and older than one hour, the scripts merge each log file, sorting the rows by time (start time, or end time for `log_tcp_complete` and `log_tcp_nocomplete`; the rows of `log_udp_complete` are only concatenated), merge the packet level traces in `traces00` with `mergecap`, and compress the results with `gzip` in `<out_dir>/<year>/<month>/`. By default the merged directories are deleted: see the parameters at the beginning of the scripts.

`log_merger_hash.sh` also replaces the IP addresses anonymized by Tstat (Crypto-PAn, option `-Y`) with salted SHA-256 hashes, using `tools/hash-log.py`. It does not process `log_http_complete`, which is discarded.

The RRD databases of the instances can be merged with `tools/rrd_merger/rrd_merger <out_dir> <in_dir1> <in_dir2> ...`.
