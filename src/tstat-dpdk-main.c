/*
 * Tstat-DPDK: a DPDK wrapper for Tstat.
 *
 * The NICs distribute the packets among nb_instances rx queues with RSS. Each queue is
 * analyzed by an instance, i.e., a separate process running Libtstat:
 * - instance 0 is the parent process, instances 1 ... nb_instances-1 are forked children;
 * - each instance has two threads, each one with an exclusive core: the producer reads the
 *   packets from its queue of every port and puts them in a ring, the consumer takes them
 *   from the ring and passes them to Libtstat;
 * - instance i uses cores 2*i (producer) and 2*i+1 (consumer).
 */
#include "tstat-dpdk-main.h"
#include "tstat-dpdk-params.h"

#define PRODUCER_CORE(i) (2*(i))
#define CONSUMER_CORE(i) (2*(i)+1)

/* The timestamp of a packet is stored in mbuf fields unused on rx */
#define SET_TIMESTAMP(m, tv) do { (m)->tx_offload = (tv).tv_sec; (m)->ol_flags = (tv).tv_usec; } while (0)
#define GET_TIMESTAMP(m, tv) do { (tv).tv_sec = (m)->tx_offload; (tv).tv_usec = (m)->ol_flags; } while (0)

/* Command line options */
static int nb_instances;	/* -m: number of instances */
static int extra_header;	/* -e: bytes to skip before the IP header, e.g., 4 for a VLAN tag */
static int vlan_offload;	/* -v: strip the VLAN tags in the NICs, needed for double-tagged traffic */
static const char * conf_format;	/* -c: format of the Tstat configuration file, with the number of the instance */

/* System */
static int nb_ports;
static int max_link_speed_mbps = 0; /* Fastes NIC, used for OFFLOAD_POLLING */
static int offload_polling_sleep_us = 0;
static struct rte_mempool * pktmbuf_pool [MAX_INSTANCES];
static struct rte_ring * rings [MAX_INSTANCES];
static pid_t children [MAX_INSTANCES];
static volatile sig_atomic_t stop = 0;

/* State of this instance */
static int instance;			/* Number of this instance */
static struct rte_ring * ring;		/* Ring from the producer to the consumer */
static uint64_t nb_tstat_packets = 0;	/* Packets passed to Libtstat */

/* Counters of the producer of each instance, in memory shared among the processes:
   the producer writes them, the parent reads them to print the stats */
struct instance_counters {
	uint64_t rx_pkts;	/* Packets received */
	uint64_t rx_bytes;	/* Bytes received */
	uint64_t drops;		/* Packets dropped, because the ring is full */
	uint64_t ring_max;	/* Max number of packets in the ring, reset by the parent every second */
} __rte_cache_aligned;
static struct instance_counters * counters;

static int parse_args(int argc, char **argv);
static int check_conf_format(const char * format);
static void sig_handler(int signo);
static void init_port(int port);
static void start_instances(void);
static void pin_thread(pthread_t thread, int core, const char * name);
static void * producer_loop(void * arg);
static void consumer_loop(void);
static void process_packet(struct rte_mbuf * m);
static void check_children(void);
static void print_final_stats(void);
static void display_init(void);
static void display_update(void);


int main(int argc, char **argv)
{
	int ret, i;
	long nb_cores;
	char name [64];
	static char conf_file [PATH_MAX], log_dir [64];	/* Libtstat keeps a pointer to them */
	pthread_t producer;
	struct timeval tv;

	/* Terminate gracefully on SIGTERM and SIGINT (CTRL + C) */
	signal(SIGTERM, sig_handler);
	signal(SIGINT, sig_handler);

	/* Initialize DPDK enviroment with args, then shift argc and argv to get application parameters */
	ret = rte_eal_init(argc, argv);
	if (ret < 0) FATAL_ERROR("Cannot init EAL");
	argc -= ret;
	argv += ret;

	/* Make standard output more silent */
	rte_log_set_global_level(RTE_LOG_ERR);

	if (parse_args(argc, argv) < 0)
		FATAL_ERROR("Usage: %s <EAL options> -- -m <instances> [-c <conf file format>] [-e <extra header bytes>] [-v]", argv[0]);

	/* Each instance needs two cores */
	nb_cores = sysconf(_SC_NPROCESSORS_ONLN);
	if (2 * nb_instances > nb_cores)
		FATAL_ERROR("%d instances need %d cores, but only %ld are available", nb_instances, 2 * nb_instances, nb_cores);

	nb_ports = rte_eth_dev_count_avail();
	if (nb_ports <= 0) FATAL_ERROR("Cannot find ETH devices");
	if (nb_ports > MAX_PORTS) FATAL_ERROR("Too many ETH devices: at most %d are supported", MAX_PORTS);

	/* One mempool and one ring for each instance, on the NUMA socket of its producer */
	for (i = 0; i < nb_instances; i++){
		int socket = rte_lcore_to_socket_id(PRODUCER_CORE(i));

		snprintf(name, sizeof(name), MEMPOOL_NAME "%02d", i);
		pktmbuf_pool[i] = rte_mempool_create(name, MEMPOOL_ELEM_NB, MEMPOOL_ELEM_SZ, MEMPOOL_CACHE_SZ,
		                                     sizeof(struct rte_pktmbuf_pool_private), rte_pktmbuf_pool_init, NULL,
		                                     rte_pktmbuf_init, NULL, socket, 0);
		if (pktmbuf_pool[i] == NULL) FATAL_ERROR("Cannot create mempool %d: %s", i, rte_strerror(rte_errno));

		snprintf(name, sizeof(name), INTERMEDIATERING_NAME "%02d", i);
		rings[i] = rte_ring_create(name, INTERMEDIATERING_SZ, socket, RING_F_SP_ENQ | RING_F_SC_DEQ);
		if (rings[i] == NULL) FATAL_ERROR("Cannot create ring %d: %s", i, rte_strerror(rte_errno));
	}

	/* Each port has a rx queue for each instance */
	for (i = 0; i < nb_ports; i++)
		init_port(i);
	for (i = 0; i < nb_ports; i++)
		rte_eth_stats_reset(i);
	/* Compute how long should sleep to avoid queues to get full. If no link is up, never sleep */
	if (max_link_speed_mbps > 0)
		offload_polling_sleep_us = (nb_instances*RX_QUEUE_SZ*AVG_PKT_SIZE*8)/(max_link_speed_mbps*OFFLOAD_POLLING_GUARD);

	/* Counters shared among the processes: created before the fork, so the children inherit them */
	counters = mmap(NULL, nb_instances * sizeof(*counters), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (counters == MAP_FAILED) FATAL_ERROR("Cannot allocate the shared counters: %s", strerror(errno));

	/* Fork the children: from now on, 'instance' identifies the process */
	start_instances();
	ring = rings[instance];

	/* Init libtstat, with per-instance log directory and conf files */
	if (snprintf(conf_file, sizeof(conf_file), conf_format, instance) >= (int)sizeof(conf_file))
		FATAL_ERROR("Configuration file name too long");
	tstat_init(conf_file);
	gettimeofday(&tv, NULL);
	snprintf(log_dir, sizeof(log_dir), TSTAT_LOG_DIR, instance);
	tstat_new_logdir(log_dir, &tv);

	/* Start the producer, and run the consumer in this thread, each on its own core */
	pin_thread(pthread_self(), CONSUMER_CORE(instance), "consumer");
	ret = pthread_create(&producer, NULL, producer_loop, NULL);
	if (ret != 0) FATAL_ERROR("Cannot start producer thread");
	pin_thread(producer, PRODUCER_CORE(instance), "producer");
	printf("Instance %d: producer on core %d, consumer on core %d\n", instance, PRODUCER_CORE(instance), CONSUMER_CORE(instance));

	consumer_loop();

	/* Terminate */
	pthread_join(producer, NULL);
	print_final_stats();
	if (instance == 0){
		/* Terminate the children (if still running) and wait for them */
		for (i = 1; i < nb_instances; i++)
			if (children[i] > 0)
				kill(children[i], SIGINT);
		for (i = 1; i < nb_instances; i++)
			if (children[i] > 0)
				waitpid(children[i], NULL, 0);
	}

	return 0;
}


static int parse_args(int argc, char **argv)
{
	int option;

	nb_instances = -1;
	extra_header = 0;
	vlan_offload = 0;
	conf_format = TSTAT_CONF_FILE;

	while ((option = getopt(argc, argv, "m:c:e:v")) != -1) {
		switch (option) {
			case 'm' : nb_instances = atoi(optarg);
				break;
			case 'c' : conf_format = optarg;
				break;
			case 'e' : extra_header = atoi(optarg);
				break;
			case 'v' : vlan_offload = 1;
				break;
			default: return -1;
		}
	}

	if (nb_instances < 1 || nb_instances > MAX_INSTANCES || extra_header < 0)
		return -1;
	if (check_conf_format(conf_format) < 0){
		fprintf(stderr, "The format of the configuration file must contain one %%d (or %%0Nd) and no other conversion\n");
		return -1;
	}

	return 0;
}

/* The format is passed to snprintf() with the number of the instance:
   accept only one integer conversion (with optional flag '0' and width) and literal '%%' */
static int check_conf_format(const char * format)
{
	const char * p;
	int nb_conversions = 0;

	for (p = format; *p != '\0'; p++){
		if (*p != '%')
			continue;
		p++;
		if (*p == '%')
			continue;
		while (*p >= '0' && *p <= '9')
			p++;
		if (*p != 'd')
			return -1;
		nb_conversions++;
	}

	return nb_conversions == 1 ? 0 : -1;
}


/* Ask all the threads to terminate */
static void sig_handler(int signo)
{
	if (signo == SIGTERM || signo == SIGINT)
		stop = 1;
}


/* Init a port with nb_instances rx queues */
static void init_port(int port)
{
	int i, ret;
	uint8_t rss_key [64];
	char port_name [RTE_ETH_NAME_MAX_LEN];
	struct rte_eth_link link;
	struct rte_eth_dev_info dev_info;
	struct rte_eth_rss_conf rss_conf;
	struct rte_eth_conf conf = port_conf;

	/* Retrieving and printing device infos */
	ret = rte_eth_dev_info_get(port, &dev_info);
	if (ret != 0) FATAL_ERROR("Cannot get the info of port %d: %s", port, rte_strerror(-ret));
	rte_eth_dev_get_name_by_port(port, port_name);
	printf("Port %d: %s\n\tDriver name: %s\n\tMax rx queues: %d\n\tMax tx queues: %d\n",
	       port, port_name, dev_info.driver_name, dev_info.max_rx_queues, dev_info.max_tx_queues);
	if (dev_info.max_rx_queues < nb_instances)
		FATAL_ERROR("Port %d has %d rx queues, but %d instances need one each", port, dev_info.max_rx_queues, nb_instances);

	/* Symmetrical seed, so that both directions of a flow go to the same instance */
	conf.rx_adv_conf.rss_conf.rss_key = rss_seed;
	conf.rx_adv_conf.rss_conf.rss_key_len = sizeof(rss_seed);

	/* Use only the RSS hash functions supported by the device */
	if ((conf.rx_adv_conf.rss_conf.rss_hf & dev_info.flow_type_rss_offloads) != conf.rx_adv_conf.rss_conf.rss_hf){
		printf("\tWarning: the device does not support RSS on all of IPv4/IPv6 TCP/UDP\n");
		conf.rx_adv_conf.rss_conf.rss_hf &= dev_info.flow_type_rss_offloads;
	}

	/* Strip the VLAN tags, needed for double-tagged traffic */
	if (vlan_offload){
		conf.rxmode.offloads = RTE_ETH_RX_OFFLOAD_VLAN_STRIP | RTE_ETH_RX_OFFLOAD_QINQ_STRIP;
		rte_eth_dev_set_vlan_offload(port, RTE_ETH_VLAN_STRIP_OFFLOAD | RTE_ETH_QINQ_STRIP_OFFLOAD);
	}

	/* Configure device with nb_instances rx queues and 1 tx queue */
	ret = rte_eth_dev_configure(port, nb_instances, 1, &conf);
	if (ret < 0) FATAL_ERROR("Cannot configure port %d: %s", port, rte_strerror(-ret));

	/* The rx queue of each instance takes the buffers from its mempool */
	for (i = 0; i < nb_instances; i++){
		ret = rte_eth_rx_queue_setup(port, i, RX_QUEUE_SZ, rte_lcore_to_socket_id(PRODUCER_CORE(i)),
		                             &dev_info.default_rxconf, pktmbuf_pool[i]);
		if (ret < 0) FATAL_ERROR("Cannot configure rx queue %d of port %d: %s", i, port, rte_strerror(-ret));

		/* Configure mapping [queue] -> [element in stats array] */
		ret = rte_eth_dev_set_rx_queue_stats_mapping(port, i, i);
		if (ret < 0 && ret != -ENOTSUP) printf("\tCannot configure the stats of rx queue %d\n", i);
	}

	/* Configure tx queue. Mandatory even if packets are only received */
	ret = rte_eth_tx_queue_setup(port, 0, TX_QUEUE_SZ, rte_socket_id(), &tx_conf);
	if (ret < 0) FATAL_ERROR("Cannot configure tx queue of port %d: %s", port, rte_strerror(-ret));

	ret = rte_eth_dev_start(port);
	if (ret < 0) FATAL_ERROR("Cannot start port %d: %s", port, rte_strerror(-ret));

	rte_eth_promiscuous_enable(port);

	/* Print link status */
	rte_eth_link_get_nowait(port, &link);
	if (link.link_status){
		printf("\tLink Up - speed %u Mbps - %s\n", (unsigned)link.link_speed,
		       (link.link_duplex == RTE_ETH_LINK_FULL_DUPLEX) ? "full-duplex" : "half-duplex");
        if (link.link_speed >max_link_speed_mbps)
            max_link_speed_mbps=link.link_speed;
    }
	else
		printf("\tLink Down\n");

	/* Print RSS support, not reliable because a NIC could support rss configuration just in
	   rte_eth_dev_configure without supporting rte_eth_dev_rss_hash_conf_get */
	rss_conf.rss_key = rss_key;
	rss_conf.rss_key_len = sizeof(rss_key);
	ret = rte_eth_dev_rss_hash_conf_get(port, &rss_conf);
	if (ret == 0) printf("\tDevice supports RSS\n");
	else printf("\tDevice DOES NOT support RSS: %s\n", rte_strerror(-ret));
}


/* Fork instances 1 ... nb_instances-1; the parent is instance 0 */
static void start_instances(void)
{
	int i;
	pid_t pid;

	instance = 0;
	for (i = 1; i < nb_instances; i++){
		printf("Starting instance %d...\n", i);
		pid = fork();
		if (pid == 0){
			instance = i;
			return;
		}
		if (pid < 0) FATAL_ERROR("Cannot fork instance %d", i);
		children[i] = pid;
	}
}


/* Run a thread exclusively on a core */
static void pin_thread(pthread_t thread, int core, const char * name)
{
	int ret;
	cpu_set_t cpuset;

	CPU_ZERO(&cpuset);
	CPU_SET(core, &cpuset);
	ret = pthread_setaffinity_np(thread, sizeof(cpuset), &cpuset);
	if (ret != 0) FATAL_ERROR("Instance %d: cannot run the %s on core %d: %s", instance, name, core, strerror(ret));
}


/* Producer: reads the packets of this instance from all the ports and puts them in the ring */
static void * producer_loop(__rte_unused void * arg)
{
	struct rte_mbuf * burst [PKT_BURST_SZ];
	struct timeval t_last, t_pkt;
	int port, i, nb_rx, nb_enq, nb_empty_ifaces;
	unsigned int ring_free;
	uint64_t nb_bytes, ring_count;
	long diff_usec;
	struct instance_counters * c = &counters[instance];
    static uint64_t start_time, end_time, elapsed;

	gettimeofday(&t_last, NULL);

	while (!stop){

        nb_empty_ifaces = 0;
        start_time = rte_get_tsc_cycles();

		for (port = 0; port < nb_ports; port++){

			nb_rx = rte_eth_rx_burst(port, instance, burst, PKT_BURST_SZ);
			/* The queue is now empty, including when no packet was read */
			if (nb_rx < PKT_BURST_SZ)
				nb_empty_ifaces++;
			if (nb_rx == 0)
				continue;
			nb_bytes = 0;

			/* The packets of the burst are timestamped evenly between the previous burst and now */
			t_pkt = t_last;
			gettimeofday(&t_last, NULL);
			diff_usec = ((t_last.tv_sec - t_pkt.tv_sec) * 1000000 + t_last.tv_usec - t_pkt.tv_usec) / nb_rx;

			for (i = 0; i < nb_rx; i++){
				if (unlikely(rte_pktmbuf_pkt_len(burst[i]) > rte_pktmbuf_data_len(burst[i])))
					FATAL_ERROR("The NIC is segmenting packets, not supported. Try increasing MEMPOOL_ELEM_SZ");

				t_pkt.tv_usec += diff_usec;
				if (unlikely(t_pkt.tv_usec >= 1000000)){
					t_pkt.tv_sec += t_pkt.tv_usec / 1000000;
					t_pkt.tv_usec %= 1000000;
				}
				SET_TIMESTAMP(burst[i], t_pkt);
				nb_bytes += rte_pktmbuf_pkt_len(burst[i]);
			}

			/* If the ring is full, drop the packets */
			nb_enq = rte_ring_enqueue_burst(ring, (void **)burst, nb_rx, &ring_free);
			for (i = nb_enq; i < nb_rx; i++)
				rte_pktmbuf_free(burst[i]);

			/* Only this thread writes the counters, but the parent reads them */
			__atomic_store_n(&c->rx_pkts, c->rx_pkts + nb_rx, __ATOMIC_RELAXED);
			__atomic_store_n(&c->rx_bytes, c->rx_bytes + nb_bytes, __ATOMIC_RELAXED);
			if (nb_enq < nb_rx)
				__atomic_store_n(&c->drops, c->drops + nb_rx - nb_enq, __ATOMIC_RELAXED);
			ring_count = rte_ring_get_capacity(ring) - ring_free;
			if (ring_count > __atomic_load_n(&c->ring_max, __ATOMIC_RELAXED))
				__atomic_store_n(&c->ring_max, ring_count, __ATOMIC_RELAXED);
		}

        /* If all queues have are now empty, sleep for remaining time */
        end_time = rte_get_tsc_cycles();
        #ifdef OFFLOAD_POLLING
        if (nb_empty_ifaces==nb_ports){
            elapsed = (end_time - start_time)*1000000/rte_get_tsc_hz();
            if (elapsed < offload_polling_sleep_us){
                usleep( offload_polling_sleep_us - elapsed);
            }

        }
        #endif
	}

	return NULL;
}


/* Consumer: takes the packets from the ring and passes them to Libtstat */
static void consumer_loop(void)
{
	struct rte_mbuf * m;
	uint64_t now, last_check;

	if (instance == 0)
		display_init();
	last_check = rte_get_tsc_cycles();

	while (!stop){

		/* Every second, the parent checks that the children are running and prints the stats */
		if (instance == 0){
			now = rte_get_tsc_cycles();
			if (now - last_check > rte_get_tsc_hz()){
				check_children();
				display_update();
				last_check = now;
			}
		}

		if (rte_ring_dequeue(ring, (void **)&m) != 0){
#ifdef OFFLOAD_CPU
			usleep(SLEEP_TIME_US);
#endif
			continue;
		}

		process_packet(m);
		rte_pktmbuf_free(m);
	}
}


static void process_packet(struct rte_mbuf * m)
{
	char * data = rte_pktmbuf_mtod(m, char *);
	uint32_t len = rte_pktmbuf_pkt_len(m);
	uint32_t ip_offset = sizeof(struct rte_ether_hdr) + extra_header;
	uint16_t ethertype;
	struct timeval tv;

	/* Skip packets too short to contain an IP header */
	if (unlikely(len <= ip_offset + sizeof(struct rte_ipv4_hdr)))
		return;

	/* Skip non-IP packets. The extra header is before the ether type, as for VLAN tags */
	ethertype = ntohs(*(uint16_t *)(data + extra_header + 12));
	if (ethertype != RTE_ETHER_TYPE_IPV4 && ethertype != RTE_ETHER_TYPE_IPV6)
		return;

	GET_TIMESTAMP(m, tv);
	tstat_next_pckt(&tv, data + ip_offset, data + len - 1, len - ip_offset, 0);
	nb_tstat_packets++;
}


/* If a child has died, terminate */
static void check_children(void)
{
	int i;

	for (i = 1; i < nb_instances; i++){
		if (waitpid(children[i], NULL, WNOHANG) > 0){
			printf("Instance %d (PID=%d) has died. Terminating.\n", i, children[i]);
			children[i] = 0;
			stop = 1;
		}
	}
}


static void print_final_stats(void)
{
	int i, j;
	char buffer [100];
	time_t now;
	uint64_t nb_drop;
	struct rte_eth_stats stat;
	tstat_report report;

	/* Print in order of instance */
	usleep(instance * 250000);

	/* The parent prints per port and per queue stats */
	if (instance == 0){
		now = time(NULL);
		strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", localtime(&now));
		for (i = 0; i < nb_ports; i++){
			rte_eth_stats_get(i, &stat);
			printf("\nTIME: %s - PORT: %d Rx: %lu Drp: %lu Tot: %lu Perc: %.3f%%", buffer, i,
			       stat.ipackets, stat.imissed, stat.ipackets + stat.imissed,
			       (float)stat.imissed / (stat.ipackets + stat.imissed) * 100);
			for (j = 0; j < nb_instances && j < RTE_ETHDEV_QUEUE_STAT_CNTRS; j++)
				printf("\n\tQueue %d Rx: %lu Err: %lu Tot: %lu", j, stat.q_ipackets[j], stat.q_errors[j],
				       stat.q_ipackets[j] + stat.q_errors[j]);
		}
		printf("\n");
	}

	/* Every instance prints its stats and the Tstat ones */
	tstat_close(&report);
	nb_drop = counters[instance].drops;
	printf("\nINSTANCE: %d Rx: %lu Drp: %lu Tot: %lu Perc: %.3f%%\n", instance, nb_tstat_packets, nb_drop,
	       nb_tstat_packets + nb_drop, (float)nb_drop / (nb_tstat_packets + nb_drop) * 100);
	printf("TSTAT STATISTICS:\n");
	printf("Total packets: %lu\nTCP flows: %lu\nUDP flows: %lu\nTime elapsed: %.3lfs\n",
	       report.pnum, report.f_TCP_count, report.f_UDP_count, report.wallclock / 1000000);
}


/* Every second, the parent prints the stats of the ports and of the instances */
static struct {
	int tty;			/* Standard output is a terminal: redraw the screen */
	time_t start;
	uint64_t last_time;		/* TSC cycles */
	uint64_t lost;			/* Packets lost since the start */
	time_t last_loss;		/* Time of the last loss, 0 if none */
	/* The probes always lose packets in the first second: the losses until the first update are not counted */
	int first;			/* The first update has not been printed yet */
	uint64_t lost_first;
	uint64_t offered_first;
	/* Counters at the end of the previous interval */
	struct rte_eth_stats ports [MAX_PORTS];
	struct instance_counters instances [MAX_INSTANCES];
	/* Text of the screen */
	char buf [16384];
	size_t len;
} display;

/* Append to the screen text; on a terminal, also clear the rest of each line */
static void screen_printf(const char * fmt, ...)
{
	va_list ap;
	int ret;

	va_start(ap, fmt);
	ret = vsnprintf(display.buf + display.len, sizeof(display.buf) - display.len, fmt, ap);
	va_end(ap);
	if (ret > 0)
		display.len = RTE_MIN(display.len + ret, sizeof(display.buf) - 1);
}

static void screen_newline(void)
{
	screen_printf(display.tty ? "\033[K\n" : "\n");
}

/* Format an integer with a space as thousands separator, e.g., "1 482" */
static const char * fmt_count(char * buf, size_t size, uint64_t value)
{
	char digits [32];
	int nb_digits, i, j;

	nb_digits = snprintf(digits, sizeof(digits), "%" PRIu64, value);
	for (i = 0, j = 0; i < nb_digits && j < (int)size - 2; i++){
		if (i > 0 && (nb_digits - i) % 3 == 0)
			buf[j++] = ' ';
		buf[j++] = digits[i];
	}
	buf[j] = '\0';
	return buf;
}

static double percent(uint64_t part, uint64_t total)
{
	return total > 0 ? (double)part / total * 100 : 0;
}

static void display_init(void)
{
	int i;

	display.tty = isatty(STDOUT_FILENO);
	display.start = time(NULL);
	display.last_time = rte_get_tsc_cycles();
	display.first = 1;
	for (i = 0; i < nb_ports; i++)
		rte_eth_stats_get(i, &display.ports[i]);
}

/* Rows with losses or with a high ring occupancy: on a terminal they are colored, and a marker is
   appended at the end, so that they can also be found in a file */
enum row_level { ROW_OK, ROW_WARN, ROW_LOSS };

static void row_begin(enum row_level level)
{
	if (display.tty && level == ROW_LOSS)
		screen_printf("\033[31m");	/* Red */
	else if (display.tty && level == ROW_WARN)
		screen_printf("\033[33m");	/* Yellow */
}

static void row_end(enum row_level level)
{
	if (level == ROW_LOSS)
		screen_printf("  LOSS");
	else if (level == ROW_WARN)
		screen_printf("  RING>%d%%", RING_WARN_PERC);
	if (display.tty && level != ROW_OK)
		screen_printf("\033[0m");
	screen_newline();
}

/* Missed includes the packets dropped because the mempool is full */
static uint64_t port_missed(const struct rte_eth_stats * stat)
{
	return stat->imissed + stat->rx_nombuf;
}

static void display_print_ports(const struct rte_eth_stats * ports, double interval)
{
	int i;
	char buf1 [32], buf2 [32];
	uint64_t packets, bytes, missed, errors;
	uint64_t tot_packets = 0, tot_bytes = 0, tot_missed = 0, tot_errors = 0;
	enum row_level level;

	screen_printf("PORT        Mbps      kpps      missed      errors    drop%%");
	screen_newline();
	for (i = 0; i < nb_ports; i++){
		packets = ports[i].ipackets - display.ports[i].ipackets;
		bytes = ports[i].ibytes - display.ports[i].ibytes;
		missed = port_missed(&ports[i]) - port_missed(&display.ports[i]);
		errors = ports[i].ierrors - display.ports[i].ierrors;
		display.ports[i] = ports[i];
		if (display.first)
			missed = errors = 0;

		level = missed + errors > 0 ? ROW_LOSS : ROW_OK;
		row_begin(level);
		screen_printf("%4d  %10.1f  %8.1f  %10s  %10s  %7.3f", i, bytes * 8 / interval / 1e6, packets / interval / 1e3,
		              fmt_count(buf1, sizeof(buf1), missed), fmt_count(buf2, sizeof(buf2), errors),
		              percent(missed + errors, packets + missed + errors));
		row_end(level);
		tot_packets += packets;
		tot_bytes += bytes;
		tot_missed += missed;
		tot_errors += errors;
	}

	level = tot_missed + tot_errors > 0 ? ROW_LOSS : ROW_OK;
	row_begin(level);
	screen_printf(" all  %10.1f  %8.1f  %10s  %10s  %7.3f", tot_bytes * 8 / interval / 1e6, tot_packets / interval / 1e3,
	              fmt_count(buf1, sizeof(buf1), tot_missed), fmt_count(buf2, sizeof(buf2), tot_errors),
	              percent(tot_missed + tot_errors, tot_packets + tot_missed + tot_errors));
	row_end(level);
}

static void display_print_instances(const struct instance_counters * instances, const uint64_t * ring_max, double interval)
{
	int i;
	char buf [32];
	struct instance_counters delta [MAX_INSTANCES];
	uint64_t tot_packets = 0;
	double ring_perc;
	enum row_level level;

	for (i = 0; i < nb_instances; i++){
		delta[i].rx_pkts = instances[i].rx_pkts - display.instances[i].rx_pkts;
		delta[i].rx_bytes = instances[i].rx_bytes - display.instances[i].rx_bytes;
		delta[i].drops = display.first ? 0 : instances[i].drops - display.instances[i].drops;
		display.instances[i] = instances[i];
		tot_packets += delta[i].rx_pkts;
	}

	screen_printf("INST        Mbps      kpps   share%%   ring%%       drops    drop%%");
	screen_newline();
	for (i = 0; i < nb_instances; i++){
		ring_perc = percent(ring_max[i], rte_ring_get_capacity(rings[i]));
		level = delta[i].drops > 0 ? ROW_LOSS : ring_perc >= RING_WARN_PERC ? ROW_WARN : ROW_OK;
		row_begin(level);
		screen_printf("%4d  %10.1f  %8.1f   %6.1f  %6.2f  %10s  %7.3f", i,
		              delta[i].rx_bytes * 8 / interval / 1e6, delta[i].rx_pkts / interval / 1e3,
		              percent(delta[i].rx_pkts, tot_packets), ring_perc,
		              fmt_count(buf, sizeof(buf), delta[i].drops), percent(delta[i].drops, delta[i].rx_pkts));
		row_end(level);
	}
}

/* Print the stats of the last interval: on a terminal, redraw the screen from the top */
static void display_update(void)
{
	int i;
	char date [32], last_loss [32], buf [32];
	time_t now, uptime;
	uint64_t tsc, lost, offered;
	double interval;
	struct rte_eth_stats ports [MAX_PORTS];
	struct instance_counters instances [MAX_INSTANCES];
	uint64_t ring_max [MAX_INSTANCES];

	tsc = rte_get_tsc_cycles();
	interval = (double)(tsc - display.last_time) / rte_get_tsc_hz();
	display.last_time = tsc;

	/* Take a snapshot of the counters of the ports and of the instances */
	for (i = 0; i < nb_ports; i++)
		rte_eth_stats_get(i, &ports[i]);
	for (i = 0; i < nb_instances; i++){
		instances[i].rx_pkts = __atomic_load_n(&counters[i].rx_pkts, __ATOMIC_RELAXED);
		instances[i].rx_bytes = __atomic_load_n(&counters[i].rx_bytes, __ATOMIC_RELAXED);
		instances[i].drops = __atomic_load_n(&counters[i].drops, __ATOMIC_RELAXED);
		ring_max[i] = __atomic_exchange_n(&counters[i].ring_max, 0, __ATOMIC_RELAXED);
	}

	/* Packets lost since the start, by the NICs and by the instances: the stats of the ports are reset at the start,
	   and the packets until the first update are not counted */
	lost = offered = 0;
	for (i = 0; i < nb_ports; i++){
		lost += port_missed(&ports[i]) + ports[i].ierrors;
		offered += ports[i].ipackets + port_missed(&ports[i]) + ports[i].ierrors;
	}
	for (i = 0; i < nb_instances; i++)
		lost += instances[i].drops;
	if (display.first){
		display.lost_first = lost;
		display.offered_first = offered;
	}
	lost -= display.lost_first;
	offered -= display.offered_first;

	now = time(NULL);
	uptime = now - display.start;
	strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S", localtime(&now));
	if (lost > display.lost){
		display.lost = lost;
		display.last_loss = now;
	}
	if (display.last_loss > 0)
		strftime(last_loss, sizeof(last_loss), "%Y-%m-%d %H:%M:%S", localtime(&display.last_loss));
	else
		snprintf(last_loss, sizeof(last_loss), "never");

	display.len = 0;
	if (display.tty)
		screen_printf("\033[H");
	screen_printf("═══ Tstat-DPDK ═══ %s ═══ uptime %ldd %02ld:%02ld:%02ld ═══════════", date,
	              (long)uptime / 86400, (long)uptime / 3600 % 24, (long)uptime / 60 % 60, (long)uptime % 60);
	screen_newline();
	screen_printf("Lost since start: %s packets (%.3f%%), last loss: %s", fmt_count(buf, sizeof(buf), lost),
	              percent(lost, offered), last_loss);
	screen_newline();
	screen_newline();
	display_print_ports(ports, interval);
	screen_newline();
	display_print_instances(instances, ring_max, interval);
	screen_printf(display.tty ? "\033[J" : "\n");
	display.first = 0;

	/* Write the whole screen at once */
	fflush(stdout);
	if (write(STDOUT_FILENO, display.buf, display.len) < 0){
		/* Ignore the errors: the analysis goes on even if the terminal is closed */
	}
}
