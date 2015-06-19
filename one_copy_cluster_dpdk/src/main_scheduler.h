/* In this file are contained data structures connected with SCHED_DEADLINE policy and needed in main_scheduler.c */

/* Deadline scheduling structure */
struct sched_attr {
	uint32_t size;              /* Size of this structure */
	uint32_t sched_policy;      /* Policy (SCHED_*) */
	uint64_t sched_flags;       /* Flags */
	int32_t sched_nice;        /* Nice value (SCHED_OTHER,SCHED_BATCH) */
	uint32_t sched_priority;    /* Static priority (SCHED_FIFO, SCHED_RR) */
	/* Remaining fields are for SCHED_DEADLINE */
	uint64_t sched_runtime;
	uint64_t sched_deadline;
	uint64_t sched_period;
};

#ifdef __x86_64__
#define __NR_sched_setattr 314
#define __NR_sched_getattr 315
#endif

#ifdef __i386__
#define __NR_sched_setattr 351
#define __NR_sched_getattr 352
#endif

#ifdef __arm__
#define __NR_sched_setattr 380
#define __NR_sched_getattr 381
#endif

#ifndef SCHED_DEADLINE
#define SCHED_DEADLINE 6
#endif

#ifndef SCHED_FLAG_RESET_ON_FORK
#define SCHED_FLAG_RESET_ON_FORK 0x01
#endif 

