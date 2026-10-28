/* Includes */
#define _GNU_SOURCE
#include <libtstat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdint.h>
#include <signal.h>
#include <errno.h>
#include <sched.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <arpa/inet.h>

#include <rte_common.h>
#include <rte_errno.h>
#include <rte_eal.h>
#include <rte_lcore.h>
#include <rte_branch_prediction.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_ethdev.h>
#include <rte_ring.h>
#include <rte_log.h>
#include <rte_mempool.h>
#include <rte_mbuf.h>
#include <rte_cycles.h>

/* Useful macro for error handling */
#define FATAL_ERROR(fmt, args...)       rte_exit(EXIT_FAILURE, fmt "\n", ##args)

/* Struct for devices configuration, for const defines see rte_ethdev.h */
static struct rte_eth_conf port_conf = {
	.rxmode = {
		.mq_mode = RTE_ETH_MQ_RX_RSS,	/* Enable RSS */
	},
	.txmode = {
		.mq_mode = RTE_ETH_MQ_TX_NONE,
	},
	.rx_adv_conf = {
		.rss_conf = {
			.rss_key_len = 40,
			.rss_hf = (RTE_ETH_RSS_NONFRAG_IPV4_TCP | RTE_ETH_RSS_NONFRAG_IPV4_UDP | RTE_ETH_RSS_NONFRAG_IPV6_TCP | RTE_ETH_RSS_NONFRAG_IPV6_UDP ) ,
		}
	}
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
