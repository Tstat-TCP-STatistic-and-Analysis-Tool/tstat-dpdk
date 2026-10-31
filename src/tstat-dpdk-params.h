/* Compile-time parameters of Tstat-DPDK */

/* If you define OFFLOAD_CPU, the consumer sleeps SLEEP_TIME_US when no packet is available,
   instead of polling the ring continuously */
#define OFFLOAD_CPU
#define SLEEP_TIME_US 1000

/* Sleep for some time if all interfaces have the queue empty.
   Assumes to sleep an amount of time to avoid queues to fet full */
#define OFFLOAD_POLLING
#define AVG_PKT_SIZE 1000 /* Average packet size estimated to compute sleep time */
#define OFFLOAD_POLLING_GUARD 4 /* Division factor for sleep safeguard */
/* sleep_time = nb_queues*queue_size*avg_pkt_size*8/line_rate_bitrate */

/* Per-instance files: %02d is replaced by the number of the instance */
#define TSTAT_CONF_FILE "tstat-conf/tstat%02d.conf"	/* Tstat configuration, default of the -c option */
#define TSTAT_LOG_DIR   "tstat-logs/tstat%02d.log"	/* Default log directory (overridden by -s in TSTAT_CONF_FILE) */

#define MAX_INSTANCES 16
#define MAX_PORTS 16

/* Packet buffers: one mempool for each instance */
#define MEMPOOL_NAME "cluster_mem_pool"
#define MEMPOOL_ELEM_NB (524288-1)	/* Must be greater than RX_QUEUE_SZ * nb_ports; should be (2^n - 1) */
#define MEMPOOL_ELEM_SZ 4096		/* Power of two greater than 1500, so that the NIC does not segment packets */
#define MEMPOOL_CACHE_SZ 0		/* The threads are not EAL lcores, so they cannot use a mempool cache */

/* Ring between the producer and the consumer of each instance */
#define INTERMEDIATERING_NAME "intermediate_ring"
#define INTERMEDIATERING_SZ (MEMPOOL_ELEM_NB+1)
#define RING_WARN_PERC 50		/* Instances whose ring is fuller than this percentage are highlighted */

#define RX_QUEUE_SZ 4096		/* Size of the rx queues: 4096 is the maximum and gives the best performance */
#define TX_QUEUE_SZ 256			/* Unused, packets are never transmitted */
#define PKT_BURST_SZ 32			/* Packets read with each rte_eth_rx_burst() */


/* RSS symmetrical 40 Byte seed, according to "Scalable TCP Session Monitoring with Symmetric Receive-side Scaling" (Shinae Woo, KyoungSoo Park from KAIST) */
static uint8_t rss_seed [] = {	0x6d, 0x5a, 0x6d, 0x5a, 0x6d, 0x5a, 0x6d, 0x5a,
				0x6d, 0x5a, 0x6d, 0x5a, 0x6d, 0x5a, 0x6d, 0x5a,
				0x6d, 0x5a, 0x6d, 0x5a, 0x6d, 0x5a, 0x6d, 0x5a,
				0x6d, 0x5a, 0x6d, 0x5a, 0x6d, 0x5a, 0x6d, 0x5a,
				0x6d, 0x5a, 0x6d, 0x5a, 0x6d, 0x5a, 0x6d, 0x5a
};

/* The same symmetrical seed, 52 Byte long as required by X710 cards
   (https://dev.dpdk.narkive.com/AIj6ALhm/dpdk-dev-symmetry-for-tcp-packets-on-x710-intel).
   Unused: replace rss_seed with it when using X710 cards */
static uint8_t rss_seed_x710 [] __rte_unused = {
				0x6d, 0x5a, 0x6d, 0x5a, 0x6d, 0x5a, 0x6d, 0x5a,
				0x6d, 0x5a, 0x6d, 0x5a, 0x6d, 0x5a, 0x6d, 0x5a,
				0x6d, 0x5a, 0x6d, 0x5a, 0x6d, 0x5a, 0x6d, 0x5a,
				0x6d, 0x5a, 0x6d, 0x5a, 0x6d, 0x5a, 0x6d, 0x5a,
				0x6d, 0x5a, 0x6d, 0x5a, 0x6d, 0x5a, 0x6d, 0x5a,
				0x6d, 0x5a, 0x6d, 0x5a, 0x6d, 0x5a, 0x6d, 0x5a,
				0x6d, 0x5a, 0x6d, 0x5a
};
