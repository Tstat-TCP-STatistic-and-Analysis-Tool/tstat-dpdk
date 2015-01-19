#include "main.h"

/* Constants of the system */
#define TSTAT_CONF_FILE "tstat-conf/tstat00.conf"
#define TSTAT_LOG_DIR "tstat00.log"

#define MEMPOOL_NAME "cluster_mem_pool"				// Name of the NICs' mem_pool, useless comment....
#define MEMPOOL_ELEM_NB (4096*16) 				// Must be greater than RX_QUEUE_SZ * RX_QUEUE_NB * nb_sys_ports; Should also be (2^n - 1). 
#define MEMPOOL_ELEM_SZ 2048  					// Power of two greater than 1500
#define MEMPOOL_CACHE_SZ 512  					// Must be greater or equal to CONFIG_RTE_MEMPOOL_CACHE_MAX_SIZE and "n modulo cache_size == 0". Empirically max is 512

#define INTERMEDIATEPOOL_NAME "intermediate_pool"	// Name of intermediate memory pool.
#define INTERMEDIATEPOOL_ELEM_NB (1048576-1)		//   The size (check amount of RAM of your system to set up this value). Use (4*1048576)/NB_INSTANCES
#define INTERMEDIATEPOOL_ELEM_SZ 2048			//    is 2GB.
#define INTERMEDIATEPOOL_CACHE_SZ 512			// Max cache size is 512, use this value

#define INTERMEDIATERING_NAME "intermedate_ring"
#define INTERMEDIATERING_SZ (INTERMEDIATEPOOL_ELEM_NB+1)

#define RX_QUEUE_SZ 4096		// The size of rx queue. Max is 4096 and is the one you'll have best performances with.
#define TX_QUEUE_SZ 256			// Unused, you don't tx packets
#define PKT_BURST_SZ 1024		// The max size of batch of packets retreived when invoking the receive function. Use the RX_QUEUE_SZ for hgh speed

#define STATS_PRINT_RATE (3500000000) 	// You print stats each STATS_PRINT_RATE clock cycles. On this machine is 3700000000, since clock is 3.7 Ghz. Use 140s for 5 min traces, 85s at 10Gbps
#define STDEV_THRESH (3500000)		// define the threshold for calculating the stdev. Samples higher than STDEV_THRESH will be ignored. Value expressed in CPU clocks.
#define STAT_FILE "tstat-stats/stats00.txt"		// define the file statistics are put in. Every instance has got statsX.txt file

/* Global vars */
/* System vars */
static int nb_sys_ports;
static int nb_sys_cores;
static int nb_istance;
/* Data structs of the system*/
static struct rte_mempool * intermediate_pool;
static struct rte_ring    * intermediate_ring;
static struct rte_mempool * pktmbuf_pool;
/* Stats */
static uint64_t nb_drop=0;
static uint64_t nb_packets=0;
static uint64_t nb_tstat_packets=0;
static double max = 0;
static uint64_t avg = 0;
static double avg_quad = 0;
static uint64_t old_time = 0;
static uint64_t old_nb_packets = 0;
static uint64_t old_nb_drop = 0;
static uint64_t old_nb_tstat_packets=0;

/* Main function */
int main(int argc, char **argv)
{
	int ret;
	int i;
	char name [50], nb_str [10];
	char * libtstat_conf_file = strdup(TSTAT_CONF_FILE);
	char * in = strdup(TSTAT_LOG_DIR);
	struct timeval tv;

	/* Create handler for SIGINT for CTRL + C closing */
	signal(SIGINT, sig_handler);

	/* Initialize DPDK enviroment with args, then shift argc and argv to get application parameters */
	ret = rte_eal_init(argc, argv);
	if (ret < 0) FATAL_ERROR("Cannot init EAL\n");
	argc -= ret;
	argv += ret;

	/* Check if this application can use two cores*/
	ret = rte_lcore_count ();
	if (ret != 2) FATAL_ERROR("This application needs exactly two (2) cores.");

	/* Parse arguments (must retrieve the total number of cores, which core I am, and time engine to use) */
	parse_args(argc, argv);
	if (ret < 0) FATAL_ERROR("Wrong arguments\n");

	/* Probe PCI bus for ethernet devices */
	ret = rte_eal_pci_probe();
	if (ret < 0) FATAL_ERROR("Cannot probe PCI\n");

	/* Get number of ethernet devices */
	nb_sys_ports = rte_eth_dev_count();
	if (nb_sys_ports <= 0) FATAL_ERROR("Cannot find ETH devices\n");

	/* Init libtstat, with per-instance log directory and conf files */
	libtstat_conf_file[16] += nb_istance/10;
	libtstat_conf_file[17] += nb_istance%10;
	tstat_init(libtstat_conf_file);
	gettimeofday(&tv, NULL);
	in[5] += nb_istance/10;
	in[6] += nb_istance%10;
	tstat_new_logdir(in, &tv);

	/* Init intermediate queue data structures: the mem_pool. Give each MEM_POOL of different instances a different name */
	strcpy(name, INTERMEDIATEPOOL_NAME);
	sprintf(nb_str, "%d", nb_istance);
	strcat(name, nb_str);
	intermediate_pool = rte_mempool_create( name, INTERMEDIATEPOOL_ELEM_NB, INTERMEDIATEPOOL_ELEM_SZ, INTERMEDIATEPOOL_CACHE_SZ, sizeof(struct rte_pktmbuf_pool_private), NULL, NULL, NULL, NULL,rte_socket_id(), MEMPOOL_F_SP_PUT | MEMPOOL_F_SC_GET);
	if (intermediate_pool == NULL) FATAL_ERROR("Cannot create intermediate_pool. Errno: %d [ENOMEM: %d, ENOSPC: %d, E_RTE_NO_TAILQ: %d, E_RTE_NO_CONFIG: %d, E_RTE_SECONDARY: %d, EINVAL: %d, EEXIST: %d]\n", rte_errno, ENOMEM, ENOSPC, E_RTE_NO_TAILQ, E_RTE_NO_CONFIG, E_RTE_SECONDARY, EINVAL, EEXIST  );
	
	/* Init intermediate queue data structures: the ring. Give each RING of different instances a different name */
	strcpy(name, INTERMEDIATERING_NAME);
	sprintf(nb_str, "%d", nb_istance);
	strcat(name, nb_str);
	intermediate_ring = rte_ring_create 	(name, INTERMEDIATERING_SZ, rte_socket_id(), RING_F_SP_ENQ | RING_F_SC_DEQ );
 	if (intermediate_ring == NULL ) FATAL_ERROR("Cannot create ring");

	/* The master process initializes the ports and the memory pool for the NICs */
	if (rte_eal_process_type() == RTE_PROC_PRIMARY){
	
		/* Create a mempool with per-core cache, initializing every element for be used as mbuf, and allocating on the current NUMA node */
		pktmbuf_pool = rte_mempool_create(MEMPOOL_NAME, MEMPOOL_ELEM_NB, MEMPOOL_ELEM_SZ, MEMPOOL_CACHE_SZ, sizeof(struct rte_pktmbuf_pool_private), rte_pktmbuf_pool_init, NULL, rte_pktmbuf_init, NULL,rte_socket_id(), 0);
		if (pktmbuf_pool == NULL) FATAL_ERROR("Cannot create mem_pool\n");
	
		/* Operations needed for each ethernet device */			
		for(i=0; i < nb_sys_ports; i++)
			init_port(i);
	}
	/* The slave process just opens the memory pool */
	else{
		pktmbuf_pool = rte_mempool_lookup(MEMPOOL_NAME);
		if (pktmbuf_pool == NULL) FATAL_ERROR("Cannot create mem_pool\n");
	}

	/* Start consumer and producer routine on 2 different cores: consumer launched first... */
	ret =  rte_eal_mp_remote_launch (main_loop_consumer, NULL, SKIP_MASTER);
	if (ret != 0) FATAL_ERROR("Cannot start consumer thread\n");	
	
	/* ... and then producer on this core */
	main_loop_producer ( NULL );	

	return 0;
}

/* Loop function, batch timing implemented */
static int main_loop_producer(__attribute__((unused)) void * arg){
	struct rte_mbuf * pkts_burst[PKT_BURST_SZ];
	struct timeval t_new, t_pack;
	struct rte_mbuf * m;
	struct packet_container * to_enqueue = NULL;
	int i, nb_rx, read_from_port = 0, ret = 0;
	uint64_t diff_usec;
	void *pip;

	/* Init time variables*/
	ret = gettimeofday(&t_pack , NULL);
	if (ret != 0) FATAL_ERROR("Error: gettimeofday failed. Quitting...\n");
	ret = gettimeofday(&t_new, NULL);
	if (ret != 0) FATAL_ERROR("Error: gettimeofday failed. Quitting...\n");

	/* Infinite loop */
	for (;;) {

		/* Read a burst for current port at queue 'nb_istance'*/
		nb_rx = rte_eth_rx_burst(read_from_port, nb_istance, pkts_burst, PKT_BURST_SZ);

		/* Create the batch times according to "Batch to the future" (various artists, most of them spanish)*/
		t_pack = t_new;
		ret = gettimeofday(&t_new, NULL);
		if ( unlikely( ret != 0) ) FATAL_ERROR("Error: gettimeofday failed. Quitting...\n");
		diff_usec = t_new.tv_sec * 1000000 + t_new.tv_usec - t_pack.tv_sec * 1000000 - t_pack.tv_usec;
		if (  unlikely( nb_rx != 0) ) diff_usec /= nb_rx;
		
		/* For each received packet. */
		for (i = 0; likely( i < nb_rx ) ; i++) {
			/* Retreive packet from burst, increase the counter */
			m = pkts_burst[i];
			nb_packets++;

			/* Creating pointer to data with mbuf macros */
			pip = rte_pktmbuf_mtod(m, char*)  + sizeof(struct ether_hdr);

			/* Adding to t_pack the diff_time */
			t_pack.tv_usec += diff_usec;
			if (unlikely ( t_pack.tv_usec > 1000000 )){
				t_pack.tv_sec  += t_pack.tv_usec / 1000000;
				t_pack.tv_usec %= 1000000;
			}

			/* Creating buffer to enqueue and handling full buffer condition*/
			ret = rte_mempool_get(intermediate_pool, (void**)(&to_enqueue) );

			if ( unlikely(ret != 0 )){
				/* I case of full buffer, print error message (every 1048576 packets lost) ad free NICs buffers anyway*/
				nb_drop++;
				rte_pktmbuf_free((struct rte_mbuf *)m);
				continue;
			}

			/* Filling intermediate buffer*/
			to_enqueue->time = t_pack;
			to_enqueue->length = rte_pktmbuf_data_len(m) - sizeof(struct ether_hdr);
			rte_memcpy(to_enqueue->data, pip, to_enqueue->length);

			/* Enqueieing buffer */
			ret = rte_ring_enqueue (intermediate_ring, to_enqueue);
			if( unlikely(ret != 0 )) FATAL_ERROR("Lost coordination beetween data structures\n");

			/* Releasing the NIC's mbuf */
			rte_pktmbuf_free((struct rte_mbuf *)m);
		}

		/* Increasing reading port number in Round-Robin logic */
		read_from_port = (read_from_port + 1) % nb_sys_ports;

	}

	return 0;
}

static int main_loop_consumer(__attribute__((unused)) void * arg){

	int ret;
	uint64_t time, interval, end_time,freq, local_nb_packets, local_nb_drop;
	double std_dev, average;
	struct packet_container * pkt;
	char * file_name;
	FILE * debug_file;

	/* Open debug file */
	file_name = strdup (STAT_FILE);
	file_name[17] += nb_istance/10;
	file_name[18] += nb_istance%10;	
	debug_file = fopen(file_name, "w");

	/* Infinite loop for consumer thread */
	for(;;){

		/* Dequeue packet */
		ret = rte_ring_dequeue(intermediate_ring, (void**)&pkt);
		
		/* Continue polling if no packet available */
		if( unlikely (ret != 0)) continue;

		/* Pass to tstat and take time before and after */
		time = rte_get_tsc_cycles();
		tstat_next_pckt(&(pkt->time), (void* )pkt->data, (void* ) (pkt->data+pkt->length), pkt->length, 0);
		end_time = rte_get_tsc_cycles();
		interval = end_time - time;

		/* Update stats */
		if(interval > max) max = interval;
		avg = avg + interval;
		/* Do not count values greater than 1 ms */
		if (interval < STDEV_THRESH) avg_quad = avg_quad + (double)interval*interval;
		nb_tstat_packets++;

		/* Print stats */
		if ( end_time - old_time > STATS_PRINT_RATE ){
			/* Read time, freq and packets (copy them to avoid concurrency problems)*/
			time = rte_get_tsc_cycles();
			freq = rte_get_tsc_hz();
			local_nb_packets = nb_packets;
			local_nb_drop = nb_drop;

			/* Calculate avg, max and stdev */
			average = (double)avg/(nb_tstat_packets-old_nb_tstat_packets)*1000000/freq; /* calculated in us */
			std_dev = sqrt (avg_quad*1000000/freq*1000000/freq/(nb_tstat_packets-old_nb_tstat_packets) - average*average); /* calculated in us */
			max = max*1000000/freq; /* convert in us */

			/* Print on file avg, max, and std_dev*/
			fprintf(debug_file, "%10.5f %10.5f %10.5f\n", average,max,std_dev);

			/* Print stats */
			printf("Instance: %d ", nb_istance);
			printf("Avg: %5.3fus Max: %11.3fus StdDev: %8.3f ", average, max, std_dev );
			printf("TCP cl.: %5ld UDP cl.: %5ld ", tcp_cleaned, udp_cleaned);
			printf("Rate: %5.3fMpps, Loss: %5.3fMpps ", (double)(local_nb_packets - old_nb_packets)/(time-old_time)*freq/1000000, (double)(local_nb_drop - old_nb_drop)/(time-old_time)*freq/1000000 );
			printf("Buffer occupation: %3.0f%%", (float)rte_mempool_free_count(intermediate_pool)/INTERMEDIATEPOOL_ELEM_NB*100);			
			if (local_nb_drop - old_nb_drop > 0) printf (" <---------------------------");		
			printf ("\n");
			
			/* Overwrite variables, and reset counters */
			old_time = time;
			old_nb_packets = local_nb_packets;
			old_nb_drop = local_nb_drop;
			old_nb_tstat_packets = nb_tstat_packets;
			avg = 0;
			avg_quad = 0;
			max = 0;
			tcp_cleaned = 0;
			udp_cleaned = 0;
		}

		/* Release buffer where the processed packet was stored */
		rte_mempool_put (intermediate_pool, (void *) pkt);

	}

	return 0;
}

/* Signal handling function */
static void sig_handler(int signo)
{
	/* Catch just SIGINT */
	if (signo == SIGINT){
		
		/* Waiting time proportional to istance for correct output writing*/
		sleep(nb_istance+1);

		/* The master core prints the per port stats  */
		int i, j;
		struct rte_eth_stats stat;

		/* The master core print per port and per queue stats */
		if (rte_eal_process_type() == RTE_PROC_PRIMARY){
			for (i = 0; i < nb_sys_ports; i++){	
				rte_eth_stats_get(i, &stat);
				printf("\nPORT: %d Rx: %ld Drp: %ld Tot: %ld Perc: %.3f%%", i, stat.ipackets, stat.imissed, stat.ipackets+stat.imissed, (float)stat.imissed/(stat.ipackets+stat.imissed)*100 );
				for (j = 0; j < nb_sys_cores; j++){
					/* Per port queue stats */
					printf("\n\tQueue %d Rx: %ld Drp: %ld Tot: %ld Perc: %.3f%%", j, stat.q_ipackets[j] ,stat.q_errors[j], stat.q_ipackets[j]+stat.q_errors[j], (float)stat.q_errors[j]/(stat.q_ipackets[j]+stat.q_errors[j])*100);
				}			
				
			}
			printf("\n");
		}
		
		/* Every process prints per instance stats and tstat stats */
		tstat_report report;
    		tstat_close(&report);
		printf("\nINSTANCE: %d Rx: %ld Drp: %ld Tot: %ld Perc: %.3f%%\n", nb_istance, nb_tstat_packets, nb_drop,  nb_tstat_packets + nb_drop, (float)nb_drop/(nb_drop+nb_tstat_packets)*100);
		printf("TSTAT STATISTICS:\n");
		/* Can use instead  tstat_print_report(&report, stdout); */
		printf("Total packets: %lu\nTCP flows: %lu\nUDP flows: %lu\nTime elapsed: %.3lfs\n", report.pnum, report.f_TCP_count, report.f_UDP_count, report.wallclock / 1000000 );

		exit(0);	
	}
}

/* Init each port with the configuration contained in the structs. Every interface has nb_sys_cores queues */
static void init_port(int i) {

		int j;
		int ret;
		uint8_t rss_key [40];
		struct rte_eth_link link;
		struct rte_eth_dev_info dev_info;
		struct rte_eth_rss_conf rss_conf;
		struct rte_eth_fdir fdir_conf;
		struct rte_eth_rss_reta reta_3_cores;

		/* Retreiving and printing device infos */
		rte_eth_dev_info_get(i, &dev_info);
		printf("Name:%s\n\tDriver name: %s\n\tMax rx queues: %d\n\tMax tx queues: %d\n", dev_info.pci_dev->driver->name,dev_info.driver_name, dev_info.max_rx_queues, dev_info.max_tx_queues);
		printf("\tPCI Adress: %04d:%02d:%02x:%01d\n", dev_info.pci_dev->addr.domain, dev_info.pci_dev->addr.bus, dev_info.pci_dev->addr.devid, dev_info.pci_dev->addr.function);
		if (dev_info.max_rx_queues < nb_sys_cores) FATAL_ERROR("Every interface must have a queue on each core, but this is not supported. Quitting...\n");

		/* Configure device with 'nb_sys_cores' rx queues and 1 tx queue */
		ret = rte_eth_dev_configure(i, nb_sys_cores, 1, &port_conf);
		if (ret < 0) rte_panic("Error configuring the port\n");

		/* For each RX queue in each NIC */
		for (j = 0; j < nb_sys_cores; j++){
			/* Configure rx queue j of current device on current NUMA socket. It takes elements from the mempool */
			ret = rte_eth_rx_queue_setup(i, j, RX_QUEUE_SZ, rte_socket_id(), &rx_conf, pktmbuf_pool);
			if (ret < 0) FATAL_ERROR("Error configuring receiving queue\n");
			/* Configure mapping [queue] -> [element in stats array] */
			ret = rte_eth_dev_set_rx_queue_stats_mapping 	(i, j, j );
			if (ret < 0) FATAL_ERROR("Error configuring receiving queue stats\n");
 	
		}




		/* Configure tx queue of current device on current NUMA socket. Mandatory configuration even if you want only rx packet */
		ret = rte_eth_tx_queue_setup(i, 0, TX_QUEUE_SZ, rte_socket_id(), &tx_conf);
		if (ret < 0) FATAL_ERROR("Error configuring transmitting queue. Errno: %d (%d bad arg, %d no mem)\n", -ret, EINVAL ,ENOMEM);

		/* Start device */		
		ret = rte_eth_dev_start(i);
		if (ret < 0) FATAL_ERROR("Cannot start port\n");

		/* Enable receipt in promiscuous mode for an Ethernet device */
		rte_eth_promiscuous_enable(i);

		/* Print link status */
		rte_eth_link_get_nowait(i, &link);
		if (link.link_status) 	printf("\tPort %d Link Up - speed %u Mbps - %s\n", (uint8_t)i, (unsigned)link.link_speed,(link.link_duplex == ETH_LINK_FULL_DUPLEX) ?("full-duplex") : ("half-duplex\n"));
		else			printf("\tPort %d Link Down\n",(uint8_t)i);

		/* Print RSS support, not reliable because a NIC could support rss configuration just in rte_eth_dev_configure whithout supporting rte_eth_dev_rss_hash_conf_get*/
		rss_conf.rss_key = rss_key;
		ret = rte_eth_dev_rss_hash_conf_get (i,&rss_conf);
		if (ret == 0) printf("\tDevice supports RSS\n"); else printf("\tDevice DOES NOT support RSS\n");
		
		/* Print Flow director support */
		ret = rte_eth_dev_fdir_get_infos (i, &fdir_conf);
		if (ret == 0) printf("\tDevice supports Flow Director\n"); else printf("\tDevice DOES NOT support Flow Director\n"); 

		/* Sperimental, try unbalanced queue distribution for RSS in order to mitigate asymmetrical speed of instance due to hypethreading */
		if (nb_sys_cores == 3){
			/* Create Redirection Table of RSS; weight are  35 58 35 */
			reta_3_cores.mask_lo = 0xFFFFFFFFFFFFFFFF;
			reta_3_cores.mask_hi = 0xFFFFFFFFFFFFFFFF;
			for (j=0; j < 35 ; j++)reta_3_cores.reta [j] = 0;	//34
			for (j=35; j < 35+58; j++)reta_3_cores.reta [j] = 1;	// 34 34 60
			for (j=35+58; j < 128; j++)reta_3_cores.reta [j] = 2;	//34 60 128
			/* Updating table on port 'i' */
			rte_eth_dev_rss_reta_update(i, &reta_3_cores);
		}


}

static int parse_args(int argc, char **argv)
{
	int option;
	
	/* Initialize variables to detect missing arguments */
	nb_sys_cores = -1;
	nb_istance = -1;

	/* Retrive arguments */
	while ((option = getopt(argc, argv,"n:p:")) != -1) {
        	switch (option) {
             		case 'n' : nb_sys_cores = atoi(optarg); 
                 		break;
             		case 'p' : nb_istance = atoi(optarg);
                 		break;
             		default: return -1; 
		}
   	}

	/* Returning bad value in case of wrong arguments */
	if(nb_sys_cores < 1 || nb_istance < 0)
		return -1;

	return 0;

}



