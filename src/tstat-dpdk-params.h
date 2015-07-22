/* IN THIS FILE THERE ARE THE CONSTANTS OF TSTAT-DPDK*/

/* If you define DEBUG, stats will be printed */
#define DEBUG
/* If you define DEBUG_DEADLINE, scheduler will be monitored and missed deadlines printed */
//#define DEBUG_DEADLINE
/* If you define SUM_IP, progressive numbers will be added to ip addresses to packets with different port */
//#define SUM_IP

/* Constants of the system */
#define TSTAT_CONF_FILE "tstat-conf/tstat00.conf"		//Tstat directories
#define TSTAT_LOG_DIR "tstat-logs/tstat00.log"

#define SCHED_RUNTIME_NS     (   1000*1000)			// Scheduling parameters in ns: 	activity time:	1000 us
#define SCHED_TOTALTIME_NS   ( 2*1000*1000)			//					period time:	2000 us
#define CPU_SET_NAME "tstat-dpdk00"

#define MEMPOOL_NAME "cluster_mem_pool"				// Name of the NICs' mem_pool, useless comment....
#define MEMPOOL_ELEM_NB (524288-1) 				// Must be greater than RX_QUEUE_SZ * RX_QUEUE_NB * nb_sys_ports; Should also be (2^n - 1). 
#define MEMPOOL_ELEM_SZ 2048  					// Power of two greater than 1500
#define MEMPOOL_CACHE_SZ 0  					// MUST BE 0 WITH SCHED_DEADLINE, otherwise the driver wil crash !!
#define MAX_QUEUES 16
#define MAX_PORTS 16

#define INTERMEDIATERING_NAME "intermedate_ring"
#define INTERMEDIATERING_SZ (MEMPOOL_ELEM_NB+1)

#define RX_QUEUE_SZ 4096					// The size of rx queue. Max is 4096 and is the one you'll have best performances with. Use lower if you want to use Burst Bulk Alloc.
#define TX_QUEUE_SZ 256						// Unused, you don't tx packets
#define PKT_BURST_SZ 4096					// The max size of batch of packets retreived when invoking the receive function. Use the RX_QUEUE_SZ for high speed

#define STDEV_THRESH 1		       				// define the threshold for calculating the stdev in millisecs. Samples higher than STDEV_THRESH will be ignored..
#define STAT_FILE "tstat-stats/stats00.txt"			// define the file statistics are put in. Every instance has got statsX.txt file

/* Use the next 2 variables to set up port direction, e.g. 

	static struct port_dir port_directions [] = {
		{ .pci_address = "01:00.0", .is_out=0 },
		{ .pci_address = "01:00.1", .is_out=1 },
	};
	static uint8_t nb_port_directions = 2;
*/

static struct port_dir port_directions [] = {};
static uint8_t nb_port_directions = 0;

/* RSS symmetrical 40 Byte seed, according to "Scalable TCP Session Monitoring with Symmetric Receive-side Scaling" (Shinae Woo, KyoungSoo Park from KAIST)  */
uint8_t rss_seed [] = {	0x6d, 0x5a, 0x6d, 0x5a, 0x6d, 0x5a, 0x6d, 0x5a,
			0x6d, 0x5a, 0x6d, 0x5a, 0x6d, 0x5a, 0x6d, 0x5a,
			0x6d, 0x5a, 0x6d, 0x5a, 0x6d, 0x5a, 0x6d, 0x5a,
			0x6d, 0x5a, 0x6d, 0x5a, 0x6d, 0x5a, 0x6d, 0x5a,
			0x6d, 0x5a, 0x6d, 0x5a, 0x6d, 0x5a, 0x6d, 0x5a
};
uint8_t rss_seed_alternative [] = {	0x6d, 0x5a, 0x6d, 0x5b, 0x6d, 0x5a, 0x6d, 0x5b,
					0x6d, 0x5a, 0x6d, 0x5a, 0x6d, 0x5a, 0x6d, 0x5a,
					0x6d, 0x5a, 0x6d, 0x5a, 0x6d, 0x5a, 0x6d, 0x5a,
					0x6d, 0x5a, 0x6d, 0x5a, 0x6d, 0x5a, 0x6d, 0x5a,
					0x6d, 0x5a, 0x6d, 0x5a, 0x6d, 0x5a, 0x6d, 0x5a
};

/* This seed is to load balance only respect destination IP, according to me (Martino Trevisan, from nowhere particular) */
uint8_t rss_seed_dst_ip [] = { 	
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
/* This seed is to load balance only respect source IP, according to me (Martino Trevisan, from nowhere particular) */
uint8_t rss_seed_src_ip [] = { 	
			0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

