/* Includes */
#define _GNU_SOURCE
#include <libtstat.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <signal.h>
#include <errno.h>
#include <libgen.h>
#include <sys/queue.h>
#include <sys/syscall.h>
#include <math.h>
#include <sched.h>
#include <pthread.h>
#include <unistd.h>
#include <arpa/inet.h>

#include <rte_common.h>
#include <rte_log.h>
#include <rte_memory.h>
#include <rte_memcpy.h>
#include <rte_memzone.h>
#include <rte_tailq.h>
#include <rte_errno.h>
#include <rte_eal.h>
#include <rte_per_lcore.h>
#include <rte_launch.h>
#include <rte_lcore.h>
#include <rte_branch_prediction.h>
#include <rte_interrupts.h>
#include <rte_pci.h>
#include <rte_debug.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_ethdev.h>
#include <rte_ring.h>
#include <rte_log.h>
#include <rte_mempool.h>
#include <rte_mbuf.h>
#include <rte_string_fns.h>
#include <rte_cycles.h>
#include <rte_atomic.h>
#include <rte_version.h>

/* Useful macro for error handling */
#define FATAL_ERROR(fmt, args...)       rte_exit(EXIT_FAILURE, fmt "\n", ##args)

/* Function prototypes */
static int main_loop_producer(__attribute__((unused)) void * arg);
static int main_loop_consumer(__attribute__((unused)) void * arg);
static void create_port_to_direction_array(void);
static void sig_handler(int signo);
static void init_port(int i);
static int parse_args(int argc, char **argv);

/* Struct for exchanging packets between the two threads */
struct packet_container{
	struct timeval time;
	uint16_t length;
	uint8_t data [2048-sizeof(struct timeval) - sizeof(uint16_t)];
};

/* Struct to init a port ith the correct seed according to its direction */
struct port_dir{
	char * pci_address;	//MAC address of the port in the form: XX:XX:X
	uint8_t is_out;		// 1 if the port is towards the internet, 0 otherwise
};


/* Struct for devices configuration for const defines see rte_ethdev.h */
static struct rte_eth_conf port_conf = {
	.rxmode = {
		.mq_mode = RTE_ETH_MQ_RX_RSS,  	/* Enable RSS */
	},
	.txmode = {
		.mq_mode = RTE_ETH_MQ_TX_NONE,
	},
	.rx_adv_conf = {
		.rss_conf = {
			.rss_key_len = 40,				/* and the seed length.					*/
			.rss_hf = (RTE_ETH_RSS_NONFRAG_IPV4_TCP | RTE_ETH_RSS_NONFRAG_IPV4_UDP | RTE_ETH_RSS_NONFRAG_IPV6_TCP | RTE_ETH_RSS_NONFRAG_IPV6_UDP ) ,
		}	
	}
};

/* Struct for configuring each rx queue. These are default values */
static const struct rte_eth_rxconf rx_conf = {
	.rx_thresh = {
		.pthresh = 8,   /* Ring prefetch threshold */
		.hthresh = 8,   /* Ring host threshold */
		.wthresh = 4,   /* Ring writeback threshold */
	},
	.rx_free_thresh = 32,    /* Immediately free RX descriptors */
};

/* Struct for configuring each tx queue. These are default values */
static const struct rte_eth_txconf tx_conf = {
	.tx_thresh = {
		.pthresh = 36,  /* Ring prefetch threshold */
		.hthresh = 0,   /* Ring host threshold */
		.wthresh = 0,   /* Ring writeback threshold */
	},
	.tx_free_thresh = 0,    /* Use PMD default values */
	.offloads = 0,  /* IMPORTANT for vmxnet3, otherwise it won't work */
	.tx_rs_thresh = 0,      /* Use PMD default values */
};

