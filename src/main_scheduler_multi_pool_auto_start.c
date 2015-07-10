#include "main.h"
#include "main_scheduler.h"

/* If you define DEBUG, stats will be printed */
#define DEBUG
/* If you define DEBUG_DEADLINE, scheduler will be monitored and missed deadlines printed */
//#define DEBUG_DEADLINE
/* If you define SUM_IP, progressive numbers will be added to ip addresses to packets with different port */
//#define SUM_IP

/* Constants of the system */
#define TSTAT_CONF_FILE "tstat-conf/tstat00.conf"		//Tstat directories
#define TSTAT_LOG_DIR "tstat-logs/tstat00.log"

#define SCHED_RUNTIME_NS     (   1000*1000)				// Scheduling parameters in ns: 	activity time:	1000 us
#define SCHED_TOTALTIME_NS   ( 2*1000*1000)				//					period time:	2000 us
#define CPU_SET_NAME "tstat-dpdk00"

#define MEMPOOL_NAME "cluster_mem_pool"				// Name of the NICs' mem_pool, useless comment....
#define MEMPOOL_ELEM_NB (524288-1) 				// Must be greater than RX_QUEUE_SZ * RX_QUEUE_NB * nb_sys_ports; Should also be (2^n - 1). 
#define MEMPOOL_ELEM_SZ 2048  					// Power of two greater than 1500
#define MEMPOOL_CACHE_SZ 0  					// MUST BE 0 WITH SCHED_DEADLINE, otherwise the driver wil crash !!
#define MAX_QUEUES 16

#define INTERMEDIATERING_NAME "intermedate_ring"
#define INTERMEDIATERING_SZ (MEMPOOL_ELEM_NB+1)

#define RX_QUEUE_SZ 4096			// The size of rx queue. Max is 4096 and is the one you'll have best performances with. Use lower if you want to use Burst Bulk Alloc.
#define TX_QUEUE_SZ 256			// Unused, you don't tx packets
#define PKT_BURST_SZ 4096		// The max size of batch of packets retreived when invoking the receive function. Use the RX_QUEUE_SZ for high speed

#define STDEV_THRESH 1		        // define the threshold for calculating the stdev in millisecs. Samples higher than STDEV_THRESH will be ignored..
#define STAT_FILE "tstat-stats/stats00.txt"		// define the file statistics are put in. Every instance has got statsX.txt file

/* Global vars */
/* System vars */
static int nb_sys_ports;
static int nb_sys_cores;
static int nb_istance;
static int current_core;
/* Use the next 2 variables to set up port direction */
static struct port_dir port_directions [] = {};
static uint8_t nb_port_directions = 0;
/* Data structs of the system*/
static struct rte_ring    * intermediate_ring;
static struct rte_mempool * pktmbuf_pool [MAX_QUEUES];
/* Stats */
static uint64_t stats_rate = 0;
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
static uint64_t old_tcp_cleaned = 0;
static uint64_t old_udp_cleaned = 0;
static uint64_t old_nic_missed_pkts = 0;

static void set_scheduling_policy_and_affinity(void);

/* Main function */
int main(int argc, char **argv)
{
	int ret;
	int i;
	char name [50], nb_str [10], command [1000];
	char pool_name [50];
	char * libtstat_conf_file = strdup(TSTAT_CONF_FILE);
	char * in = strdup(TSTAT_LOG_DIR);
	pthread_t producer_t;
	struct timeval tv;

	/* Create handler for SIGTERM and SIGINT for CTRL + C closing */
	signal(SIGTERM, sig_handler);
	signal(SIGINT, sig_handler);

	/* Make standard output silent 
	rte_set_log_level 	( 1);
	rte_set_log_type (RTE_LOGTYPE_EAL, 0);
	rte_set_log_type (RTE_LOGTYPE_PMD, 0);
	rte_openlog_stream(fopen("/dev/null", "w"));*/

	/* Initialize DPDK enviroment with args, then shift argc and argv to get application parameters */
	ret = rte_eal_init(argc, argv);
	if (ret < 0) FATAL_ERROR("Cannot init EAL\n");
	argc -= ret;
	argv += ret;

	/* Check if this application can use one core*/
	ret = rte_lcore_count ();
	if (ret != 1) FATAL_ERROR("This application needs exactly one (1) cores.");
	current_core = rte_lcore_id ();

	/* Parse arguments (must retrieve the total number of cores, which core I am, and time engine to use) */
	ret = parse_args(argc, argv);
	if (ret < 0) FATAL_ERROR("Wrong arguments\n");

	/* Probe PCI bus for ethernet devices, mandatory only in DPDK < 1.8.0 */
	#if RTE_VER_MAJOR == 1 && RTE_VER_MINOR < 8
		ret = rte_eal_pci_probe();
		if (ret < 0) FATAL_ERROR("Cannot probe PCI\n");
	#endif

	/* Get number of ethernet devices */
	nb_sys_ports = rte_eth_dev_count();
	if (nb_sys_ports <= 0) FATAL_ERROR("Cannot find ETH devices\n");

	/* Init libtstat, with per-instance log directory and conf files */
	libtstat_conf_file[16] += nb_istance/10;
	libtstat_conf_file[17] += nb_istance%10;
	tstat_init(libtstat_conf_file);
	gettimeofday(&tv, NULL);
	in[16] += nb_istance/10;
	in[17] += nb_istance%10;
	tstat_new_logdir(in, &tv);

	/* Init intermediate queue data structures: the ring. Give each RING of different instances a different name */
	strcpy(name, INTERMEDIATERING_NAME);
	sprintf(nb_str, "%d", nb_istance);
	strcat(name, nb_str);
	intermediate_ring = rte_ring_create 	(name, INTERMEDIATERING_SZ, rte_socket_id(), RING_F_SP_ENQ | RING_F_SC_DEQ );
 	if (intermediate_ring == NULL ) FATAL_ERROR("Cannot create ring");

	/* The master process initializes the ports and the memory pools for the NICs */
	if (rte_eal_process_type() == RTE_PROC_PRIMARY){

		/* One pool for each instance */
		for (i = 0; i < nb_sys_cores; i++){
			sprintf( pool_name, MEMPOOL_NAME "%2d", i);
			pktmbuf_pool[i] = rte_mempool_create(pool_name, MEMPOOL_ELEM_NB, MEMPOOL_ELEM_SZ, MEMPOOL_CACHE_SZ, sizeof(struct rte_pktmbuf_pool_private), rte_pktmbuf_pool_init, NULL, rte_pktmbuf_init, NULL,  rte_lcore_to_socket_id  (i), 0);
			if (pktmbuf_pool[i] == NULL) FATAL_ERROR("Cannot create cluster_mem_pool %d. Errno: %d [ENOMEM: %d, ENOSPC: %d, E_RTE_NO_TAILQ: %d, E_RTE_NO_CONFIG: %d, E_RTE_SECONDARY: %d, EINVAL: %d, EEXIST: %d]\n", i, rte_errno, ENOMEM, ENOSPC, E_RTE_NO_TAILQ, E_RTE_NO_CONFIG, E_RTE_SECONDARY, EINVAL, EEXIST  );
			
		}
		/* Operations needed for each ethernet device */			
		for(i=0; i < nb_sys_ports; i++)
			init_port(i);
	}
	/* The slave process just opens the memory pools */
	else{
		/* One pool for each instance */
		for (i = 0; i < nb_sys_cores; i++){
			sprintf( pool_name, MEMPOOL_NAME "%2d", i);
			pktmbuf_pool[i] = rte_mempool_lookup(pool_name);
			if (pktmbuf_pool[i] == NULL) FATAL_ERROR("Cannot open mem_pool\n");

		}
	}

	/* Start producer thread (on the same core) */
	ret = pthread_create(&producer_t, NULL, (void *(*)(void *))main_loop_producer, NULL);
	if (ret != 0) FATAL_ERROR("Cannot start producer thread\n");	

	/* Start next instance of cluster */
	if (nb_istance < nb_sys_cores - 1){
		sprintf(command, "sudo %s -c 0X%04x -n %d --proc-type=secondary -- -m %d -p %d &", argv[0], (int)pow(2,nb_istance+1), rte_memory_get_nchannel() ,nb_sys_cores, nb_istance+1);
		printf("Starting instance %d...\nExecuting '%s'\n", nb_istance+2, command);	
		system(command);
	}
	else
		printf("All the %d instances are up.\n", nb_sys_cores);
	
	/* ... and then loop in consumer */
	main_loop_consumer ( NULL );	

	return 0;
}


/* This function set sched_deadline policy and create a cpu set for the current thread */
static void set_scheduling_policy_and_affinity(void){
	
	struct sched_attr sc_attr;
	uint64_t mask;
	char * cpu_set_name = strdup(CPU_SET_NAME);
	char dir_command [1000];
	int ret;

	/* Remove affinity set atomatically by DPDK by setting affinity to all cores */
	mask = 0xFFFFFFFFFFFFFFFF;
	ret = sched_setaffinity(0, sizeof(mask), (cpu_set_t*)&mask);
	if (ret != 0) FATAL_ERROR("Error: cannot set affinity. Quitting...\n");

	/* Set affinity by means of cpuset (the only way to have affinity with SCHED_DEADLINE).*/ 
	/* NOTE: The cpuset MUST have exclusive use of that CPU. Be sure that in you system there are not other CPU sets in conflict */
	system("mkdir -p /dev/cpuset; mount -t cpuset cpuset /dev/cpuset");						/* Create cpusets directory, if not exist */
	cpu_set_name[10] += nb_istance/10;										/* Set name of new cpuset */
	cpu_set_name[11] += nb_istance%10;		
	sprintf(dir_command, "mkdir -p /dev/cpuset/%s", cpu_set_name);							/* Create the command to create the cpu set ...*/
	system(dir_command);													/* ... And execute it */
	sprintf(dir_command, "/bin/echo %d > /dev/cpuset/%s/cpus", current_core, cpu_set_name);				/* Create the command to set preferred CPU of the cpu set... */
	system(dir_command);													/* ... And execute it */	
	sprintf(dir_command, "/bin/echo %d > /dev/cpuset/%s/mems", rte_socket_id(), cpu_set_name);			/* Create the command to set preferred memory on current numa node... */
	system(dir_command);													/* ... And execute it */
	sprintf(dir_command, "/bin/echo 0 > /dev/cpuset/sched_load_balance; /bin/echo 1 > /dev/cpuset/cpu_exclusive; ");/* Create the command to set random parameters needed by cpuset to work with SCHED_DEADLINE*/
	system(dir_command);													/* ... And execute it */
	sprintf(dir_command, "/bin/echo %ld > /dev/cpuset/%s/tasks", syscall(SYS_gettid), cpu_set_name);		/* Create command to put current thread in cpuset... */
	system(dir_command);													/* ... And execute it */

	/* Set up SCHED_DEADLINE policy */
	sc_attr.size = sizeof(struct sched_attr);
	sc_attr.sched_policy = SCHED_DEADLINE;
	sc_attr.sched_runtime =  SCHED_RUNTIME_NS;
	sc_attr.sched_deadline = SCHED_TOTALTIME_NS ;
	sc_attr.sched_period = SCHED_TOTALTIME_NS;
	ret = syscall(__NR_sched_setattr,0, &sc_attr, 0);
	if (ret != 0) FATAL_ERROR("Cannot set thread scheduling policy. Ret code=%d Errno=%d (EINVAL=%d ESRCH=%d E2BIG=%d EINVAL=%d E2BIG=%d EBUSY=%d EINVAL=%d EPERM=%d). Quitting...\n",ret, errno, EINVAL, ESRCH , E2BIG, EINVAL ,E2BIG, EBUSY, EINVAL, EPERM);

}

/* Loop function, batch timing implemented */
static int main_loop_producer(__attribute__((unused)) void * arg){
	struct rte_mbuf * pkts_burst[PKT_BURST_SZ];
	struct timeval t_new, t_pack;
	struct rte_mbuf * m;
	struct ipv4_hdr * ip_h;
	int i, nb_rx, read_from_port = 0, ret = 0;
	uint64_t diff_usec;
	uint64_t time = 0, older_time = 0, diff_time=0;
	double time_ns=0;


	/* Call this function; its aim is evident */
	set_scheduling_policy_and_affinity();
	printf("NIC polling thread created with tid:%ld\n", syscall(SYS_gettid));

	/* Reset ports stats */
	for (i=0;i<nb_sys_ports; i++)
		rte_eth_stats_reset ( i );

	/* Init time variables*/
	ret = gettimeofday(&t_pack , NULL);
	if (ret != 0) FATAL_ERROR("Error: gettimeofday failed. Quitting...\n");
	ret = gettimeofday(&t_new, NULL);
	if (ret != 0) FATAL_ERROR("Error: gettimeofday failed. Quitting...\n");
	older_time = rte_get_tsc_cycles();

	/* Infinite loop */
	for (;;) {

		#ifdef DEBUG_DEADLINE
		/* Check if a deadline was lost */
		if (read_from_port == 0){
			time = rte_get_tsc_cycles();
			diff_time = time - older_time;
			older_time = time;
			time_ns = (double)diff_time/rte_get_tsc_hz()*1000000000;

			/* If lost deadline, print a warning message */
			if (time_ns > SCHED_TOTALTIME_NS*1.9)
				printf("\nWARNING on instance %2d: lost scheduling deadline: %8.3fus instead of %8.3fus \n", nb_istance, (double)time_ns/1000 , (double)SCHED_TOTALTIME_NS/1000);
		}
		#endif

		/* Read a burst for current port at queue 'nb_istance'*/
		nb_rx = rte_eth_rx_burst(read_from_port, nb_istance, pkts_burst, PKT_BURST_SZ);

		/* Create the batch times according to "Batch to the future" (various artists, most of them spanish)*/
		t_pack = t_new;
		ret = gettimeofday(&t_new, NULL);
		if ( unlikely( ret != 0) ) FATAL_ERROR("Error: gettimeofday failed. Quitting...\n");
		diff_usec = t_new.tv_sec * 1000000 + t_new.tv_usec - t_pack.tv_sec * 1000000 - t_pack.tv_usec;
		if ( likely( nb_rx != 0) ) diff_usec /= nb_rx;
		
		/* For each received packet. */
		for (i = 0; likely( i < nb_rx ) ; i++) {
			/* Retreive packet from burst, increase the counter */
			m = pkts_burst[i];
			nb_packets++;

			/* Adding to t_pack the diff_time */
			t_pack.tv_usec += diff_usec;
			if (unlikely ( t_pack.tv_usec > 1000000 )){
				t_pack.tv_sec  += t_pack.tv_usec / 1000000;
				t_pack.tv_usec %= 1000000;
			}

			/* Writing packet timestamping in unused mbuf fields. (wild approac ! ) */
			m->tx_offload = t_pack.tv_sec;
			m->udata64 =  t_pack.tv_usec;

			/* Sum a number to incoming IP addresses according to incoming port. Useful just in lab test with duplicate packets */
			#ifdef SUM_IP
				/* Add a number to ip address if needed */
				ip_h = (struct ipv4_hdr*)(rte_pktmbuf_mtod(m, char*) + sizeof(struct  ether_hdr));
				ip_h->src_addr+=read_from_port*256*256*256;
				ip_h->dst_addr+=read_from_port*256*256*256;
			
			#endif // SUM_IP

			/*Enqueue in FIFO */
			ret = rte_ring_enqueue (intermediate_ring, m);

			/* If the ring is full, free the buffer */
			if( unlikely(ret != 0 )) {
				nb_drop++;
				rte_pktmbuf_free((struct rte_mbuf *)m);
			} 

		}

		/* Increasing reading port number in Round-Robin logic */
		read_from_port = (read_from_port + 1) % nb_sys_ports;
		/* If all the ports have been polled, make the thread sleep */
		if (read_from_port == 2)
			sched_yield();
	}
	return 0;
}

static int main_loop_consumer(__attribute__((unused)) void * arg){

	int ret, i;
	uint64_t time, interval, end_time,freq, local_nb_packets, local_nb_drop, nic_missed_pkts, thresh;
	double std_dev, average;
	char * file_name;
	char command [100];
	FILE * debug_file;
	struct rte_mbuf * m;
	struct timeval tv;
	struct rte_eth_stats stat;

	/* Create stats directory if not exists */
	sprintf(command,"mkdir -p %s", dirname(strdup(STAT_FILE)));
	system (command);

	/* Init stats_rate as CPU clock: therefore stats will be printed each second. Convert threshold value from millisecs to clocks*/
	stats_rate = rte_get_tsc_hz();
	thresh = STDEV_THRESH * stats_rate / 1000;

	/* Open stats file */
	file_name = strdup (STAT_FILE);
	file_name[17] += nb_istance/10;
	file_name[18] += nb_istance%10;	
	debug_file = fopen(file_name, "w");
	fprintf(debug_file, "# PKTS        AVG          MAX      STDEV  TCP_FLW  UDP_FLW  INC_RATE LOSS_RATE MEM_PERC\n");

	/* Infinite loop for consumer thread */
	for(;;){

		/* Dequeue packet */
		ret = rte_ring_dequeue(intermediate_ring, (void**)&m);
		
		/* Continue polling if no packet available */
		if( unlikely (ret != 0)) continue;

		/* Read timestamp of the packet from unused fields of mbuf structure*/
		tv.tv_usec = m->udata64;
		tv.tv_sec = m->tx_offload;

		/* When debugging calc and print a lot of stats */
		#ifdef DEBUG

		/* Init 'old_time' after first packet read */
		if ( unlikely (old_time == 0 ) )
			old_time = rte_get_tsc_cycles();

		/* Pass to tstat and take time before and after */
		time = rte_get_tsc_cycles();
		tstat_next_pckt(&(tv), (void* )(rte_pktmbuf_mtod(m, char*)  + sizeof(struct ether_hdr)), rte_pktmbuf_mtod(m, char*) + rte_pktmbuf_data_len(m) , (rte_pktmbuf_data_len(m) - sizeof(struct ether_hdr)), 0);
		end_time = rte_get_tsc_cycles();
		interval = end_time - time;

		/* Update stats */
		nb_tstat_packets++;
		avg = avg + interval;
		if(interval > max)
			max = interval;
		/* Do not count values greater than the threshold */
		if (interval < thresh)
			avg_quad = avg_quad + (double)interval*interval;

		/* Print stats */
		if ( end_time - old_time > stats_rate ){

			/* Read time, freq and packets (copy them to avoid concurrency problems)*/
			time = rte_get_tsc_cycles();
			freq = rte_get_tsc_hz();
			local_nb_packets = nb_packets;
			local_nb_drop = nb_drop;

			/* Calculate avg, max and stdev */
			average = (double)avg/(nb_tstat_packets-old_nb_tstat_packets)*1000000/freq; /* calculated in us */
			std_dev = sqrt (avg_quad*1000000/freq*1000000/freq/(nb_tstat_packets-old_nb_tstat_packets) - average*average); /* calculated in us */
			max = max*1000000/freq; /* convert in us */

			/* Calculate the number of packet dropped by the NICs */
			nic_missed_pkts = 0;
			for (i = 0; i < nb_sys_ports; i++){
				rte_eth_stats_get(i, &stat);
				nic_missed_pkts += stat.imissed;
			}
			nic_missed_pkts /= nb_sys_cores;

			/* Print on file all the stats*/
			fprintf(debug_file, "%6ld "                 , (nb_tstat_packets-old_nb_tstat_packets));		/* Analyzied packets */
			fprintf(debug_file, "%10.5f %12.5f %10.5f " , average,max,std_dev);				/* Avg, max, stdev*/
			fprintf(debug_file, "%8ld %8ld "            , tcp_cleaned-old_tcp_cleaned, udp_cleaned-old_udp_cleaned); 		/* tcp and udp flow closing rate */
			fprintf(debug_file, "%9.3f "                , (double)(local_nb_packets - old_nb_packets)/(time-old_time)*freq/1000000  );	/* Rate */
			fprintf(debug_file, "%9.3f "                , (double)(local_nb_drop-old_nb_drop + nic_missed_pkts-old_nic_missed_pkts )/( time-old_time )*freq/1000000 );	/* Losses */
			fprintf(debug_file, "%8.1f \n"              , 100.0 - (float)rte_ring_free_count(intermediate_ring)/INTERMEDIATERING_SZ*100.0);	/* Memory usage*/

			/* Print stats */
			printf("Instance: %2d ", nb_istance);
			printf("Pkts: %6ld ", (nb_tstat_packets-old_nb_tstat_packets));
			printf("Avg: %5.3fus Max: %11.3fus StdDev: %8.3f ", average, max, std_dev );
			printf("TCP cl.: %5ld UDP cl.: %5ld ", tcp_cleaned-old_tcp_cleaned, udp_cleaned-old_udp_cleaned);
			printf("Rate: %5.3fMpps ", (double)(local_nb_packets - old_nb_packets)/(time-old_time)*freq/1000000 );
			printf("Loss: %5.3fMpps ", (double)(   local_nb_drop-old_nb_drop   +   nic_missed_pkts-old_nic_missed_pkts   )/(   time-old_time   )*freq/1000000 );
			printf("Mem. occupation: %3.0f%% ", 100.0 - (float)rte_ring_free_count(intermediate_ring)/INTERMEDIATERING_SZ*100.0);
			printf("(%3.0f%% in tot.) ", (float)rte_mempool_free_count(pktmbuf_pool[nb_istance])/MEMPOOL_ELEM_NB*100.0);		

	
			/* If the NICs or the buffer are loosing packets, signal it */
			if (  (nic_missed_pkts-old_nic_missed_pkts > 0)  || (local_nb_drop - old_nb_drop > 0) )
				printf (" <--------L-O-S-I-N-G---------");
			/* Print new line, print 2 new lines if I'm the last instance */
			printf ("\n");
			if (nb_istance == nb_sys_cores - 1)
				printf("\n");

			/* Update variables, and reset counters */
			old_time = time;
			avg = 0;
			avg_quad = 0;
			max = 0;
			old_nb_packets = local_nb_packets;
			old_nb_drop = local_nb_drop;
			old_nb_tstat_packets = nb_tstat_packets;
			old_tcp_cleaned = tcp_cleaned;
			old_udp_cleaned = udp_cleaned;
			old_nic_missed_pkts = nic_missed_pkts;
		}

		/* If not debugging, just analyze the packets */
		#else
		tstat_next_pckt(&(tv), (void* )(rte_pktmbuf_mtod(m, char*)  + sizeof(struct ether_hdr)), rte_pktmbuf_mtod(m, char*) + rte_pktmbuf_data_len(m) , (rte_pktmbuf_data_len(m) - sizeof(struct ether_hdr)), 0);
		#endif
		
		
		/* Release buffer where the processed packet was stored */
		rte_pktmbuf_free((struct rte_mbuf *)m);

	}

	return 0;
}

/* Signal handling function */
static void sig_handler(int signo)
{
	/* Catch just SIGTERM */
	if (signo == SIGTERM || signo == SIGINT){
		
		/* Waiting time proportional to istance for correct output writing*/
		usleep( (float)nb_istance/4*1000000);

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
		printf("\nISTANCE: %d Rx: %ld Drp: %ld Tot: %ld Perc: %.3f%%\n", nb_istance, nb_tstat_packets, nb_drop,  nb_tstat_packets + nb_drop, (float)nb_drop/(nb_drop+nb_tstat_packets)*100);
		printf("TSTAT STATISTICS:\n");
		/* Can use instead  tstat_print_report(&report, stdout); */
		printf("Total packets: %lu\nTCP flows: %lu\nUDP flows: %lu\nTime elapsed: %.3lfs\n", report.pnum, report.f_TCP_count, report.f_UDP_count, report.wallclock / 1000000 );

		/* The father waits for the children to die */
		if (nb_istance == 0)
			usleep( (float)nb_sys_cores/4*1000000);

		exit(0);	
	}
}

/* Init each port with the configuration contained in the structs. Every interface has nb_sys_cores queues */
static void init_port(int i) {

		int j;
		int ret;
		uint8_t rss_key [40];
		char pci_address[10];
		struct rte_eth_link link;
		struct rte_eth_dev_info dev_info;
		struct rte_eth_rss_conf rss_conf;
		struct rte_eth_conf port_conf_temp;
		struct rte_eth_fdir fdir_conf;

		/* Retreiving and printing device infos */
		rte_eth_dev_info_get(i, &dev_info);
		printf("Name:%s\n\tDriver name: %s\n\tMax rx queues: %d\n\tMax tx queues: %d\n", dev_info.pci_dev->driver->name,dev_info.driver_name, dev_info.max_rx_queues, dev_info.max_tx_queues);
		printf("\tPCI Adress: %04d:%02d:%02x.%01d\n", dev_info.pci_dev->addr.domain, dev_info.pci_dev->addr.bus, dev_info.pci_dev->addr.devid, dev_info.pci_dev->addr.function);
		if (dev_info.max_rx_queues < nb_sys_cores) FATAL_ERROR("Every interface must have a queue on each core, but this is not supported. Quitting...\n");

		/* Decide seed to give the port, by default use the classical symmetrical*/
		port_conf.rx_adv_conf.rss_conf.rss_key = rss_seed;
		port_conf_temp = port_conf;
		for (j=0; j < nb_port_directions; j++){
			sprintf(pci_address, "%02d:%02x.%01d", dev_info.pci_dev->addr.bus, dev_info.pci_dev->addr.devid, dev_info.pci_dev->addr.function);
			
			/* If the port is outgoing, load balancing by ip source.*/
			if ( strcmp(pci_address,port_directions[j].pci_address) == 0  &&  port_directions[j].is_out) {
				port_conf_temp.rx_adv_conf.rss_conf.rss_key = rss_seed_src_ip;
				printf("\t\tPort detected as outgoing. Load balancing by ip source.\n");
			}

			/* If the port is incoming, load balancing by ip destination.*/		
			if ( strcmp(pci_address,port_directions[j].pci_address) == 0  &&  !port_directions[j].is_out) {
				port_conf_temp.rx_adv_conf.rss_conf.rss_key = rss_seed_dst_ip;
				printf("\t\tPort detected as incoming. Load balancing by ip destination.\n");
			}	
		}
		/* Configure device with 'nb_sys_cores' rx queues and 1 tx queue */
		ret = rte_eth_dev_configure(i, nb_sys_cores, 1, &port_conf_temp);
		if (ret < 0) rte_panic("Error configuring the port\n");

		/* For each RX queue in each NIC rte_socket_id()  */
		for (j = 0; j < nb_sys_cores; j++){
			/* Configure rx queue j of current device on current NUMA socket. It takes elements from the mempool */
			ret = rte_eth_rx_queue_setup(i, j, RX_QUEUE_SZ,  rte_lcore_to_socket_id ( j ), &rx_conf, pktmbuf_pool[j]);
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

		/* Sperimental, try unbalanced queue distribution for RSS in order to mitigate asymmetrical speed of instance due to hypethreading 
		#if RTE_VER_MAJOR == 1 && RTE_VER_MINOR < 8
			struct rte_eth_rss_reta reta_3_cores;
			if (nb_sys_cores == 3){
				/* Create Redirection Table of RSS; weight are  35 58 35 
				reta_3_cores.mask_lo = 0xFFFFFFFFFFFFFFFF;
				reta_3_cores.mask_hi = 0xFFFFFFFFFFFFFFFF;
				for (j=0; j < 35 ; j++)reta_3_cores.reta [j] = 0;	//34
				for (j=35; j < 35+58; j++)reta_3_cores.reta [j] = 1;	// 34 34 60
				for (j=35+58; j < 128; j++)reta_3_cores.reta [j] = 2;	//34 60 128
				/* Updating table on port 'i' 
				rte_eth_dev_rss_reta_update(i, &reta_3_cores);
			}
		#else
			struct rte_eth_rss_reta_entry64 reta_3_cores [2];
			if (nb_sys_cores == 3){
				/* Create Redirection Table of RSS; weight are  35 58 35 
				reta_3_cores[0].mask = 0xFFFFFFFFFFFFFFFF;
				reta_3_cores[1].mask = 0xFFFFFFFFFFFFFFFF;
				for (j=0; j < 35 ; j++)reta_3_cores[0].reta [j] = 0;	//34
				for (j=35; j < 64; j++)reta_3_cores[0].reta [j] = 1;	// 34 34 60
				for (j=0; j < 35+58-64; j++)reta_3_cores[1].reta [j] = 1;	// 34 34 60
				for (j=35+58-64; j < 64; j++)reta_3_cores[1].reta [j] = 2;	//34 60 128
				/* Updating table on port 'i' 
				rte_eth_dev_rss_reta_update(i, reta_3_cores,128);
			}
		
		#endif */
	
}

static int parse_args(int argc, char **argv)
{
	int option;
	
	/* Initialize variables to detect missing arguments */
	nb_sys_cores = -1;
	nb_istance = 0;

	/* Retrive arguments */
	while ((option = getopt(argc, argv,"m:p:")) != -1) {
        	switch (option) {
             		case 'm' : nb_sys_cores = atoi(optarg); 
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



