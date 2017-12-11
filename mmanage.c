/**
 * @file mmanage.c
 * @author Prof. Dr. Wolfgang Fohl, HAW Hamburg
 * @date  2014

 * @brief Memory Manager module of TI BSP A3 virtual memory
 * 
 * This is the memory manager process that
 * works together with the vmaccess process to
 * manage virtual memory management.
 *
 * The memory manager process will be invoked
 * via a SIGUSR1 signal. It maintains the page table
 * and provides the data pages in shared memory.
 *
 * This process starts shared memory, so
 * it has to be started prior to the vmaccess process.
 *
 */

#include "mmanage.h"
#include "debug.h"
#include "pagefile.h"
#include "logger.h"
#include "vmem.h"
#include "pthread.h"
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#define SNAME "semaphore"
/*
 * Signatures of private / static functions
 */

/**
 *****************************************************************************************
 *  @brief      This function fetchs a page out of the pagefile.
 *
 * It is mainly a wrapper of the corresponding function of module pagefile.c
 *
 *  @param      pt_idx Index of the page that should be fetched.
 * 
 *  @return     void 
 ****************************************************************************************/
static void fetch_page(int pt_idx);

/**
 *****************************************************************************************
 *  @brief      This function writes a page into the pagefile.
 *
 * It is mainly a wrapper of the corresponding function of module pagefile.c
 *
 *  @param      pt_idx Index of the page that should be written into the pagefile.
 * 
 *  @return     void 
 ****************************************************************************************/
static void store_page(int pt_idx);

/**
 *****************************************************************************************
 *  @brief      This function initializes the virtual memory.
 *
 *  In particular it creates the shared memory. The application just attachs to the
 *  shared memory.
 *
 *  @return     void 
 ****************************************************************************************/
static void vmem_init(void);

/**
 *****************************************************************************************
 *  @brief      This function finds an unused frame.
 *
 *  The framepage array of pagetable marks unused frames with VOID_IDX. 
 *  Based on this information find_free_frame searchs in vmem->pt.framepage for the 
 *  free frame with the smallest frame number.
 *
 *  @return     idx of the unused frame with the smallest idx. 
 *              If all frames are in use, VOID_IDX will be returned.
 ****************************************************************************************/
static int find_free_frame();

/**
 *****************************************************************************************
 *  @brief      This function update the page table for page vmem->adm.req_pageno.
 *              It will be stored in frame.
 *
 *  @param      frame The frame that stores the now allocated page vmem->adm.req_pageno.
 *
 *  @return     void 
 ****************************************************************************************/
static void update_pt(int frame);

/**
 *****************************************************************************************
 *  @brief      This function allocates a new page into memory. If all frames are in 
 *              use the corresponding page replacement algorithm will be called.
 *
 *  allocate_page gets the requested page via vmem->adm.req_pageno. Please take into
 *  account that allocate_page must update the page table and log the page fault 
 *  as well.
 *  allocate_page does all actions that must be down when the SIGUSR1 signal 
 *  indicates a page fault.
 *
 *  @return     void 
 ****************************************************************************************/
static void allocate_page(void);

/**
 *****************************************************************************************
 *  @brief      This function is the signal handler attached to system call sigaction
 *              for signal SIGUSR1, SIGUSR2 aund SIGINT.
 *
 * These three signals have the same signal handler. Based on the parameter signo the 
 * corresponding action will be started.
 *
 *  @param      signo Current signal that has be be handled.
 * 
 *  @return     void 
 ****************************************************************************************/
static void sighandler(int signo);

/**
 *****************************************************************************************
 *  @brief      This function dumps the page table to stderr.
 *
 *  @return     void 
 ****************************************************************************************/
static void dump_pt(void);

/**
 *****************************************************************************************
 *  @brief      This function implements page replacement algorithm aging.
 *
 *  @return     idx of the page that should be replaced.
 ****************************************************************************************/
static int find_remove_aging(void);

/**
 *****************************************************************************************
 *  @brief      This function implements page replacement algorithm fifo.
 *
 *  @return     idx of the page that should be replaced.
 ****************************************************************************************/
static int find_remove_fifo(void);

/**
 *****************************************************************************************
 *  @brief      This function implements page replacement algorithm clock.
 *
 *  @return     idx of the page that should be replaced.
 ****************************************************************************************/
static int find_remove_clock(void);

/**
 *****************************************************************************************
 *  @brief      This function selects and starts a page replacement algorithm.
 *
 *  It is just a wrapper for the three page replacement algorithms.
 *
 *  @return     The idx of the page that should be replaced.
 ****************************************************************************************/
static int find_remove_frame(void);

/**
 *****************************************************************************************
 *  @brief      This function cleans up when mmange runs out.
 *
 *  @return     void 
 ****************************************************************************************/
static void cleanup(void) ;

/**
 *****************************************************************************************
 *  @brief      This function scans all parameters of the porgram.
 *              The corresponding global variables vmem->adm.page_rep_algo will be set.
 * 
 *  @param      argc number of parameter 
 *
 *  @param      argv parameter list 
 *
 *  @return     void 
 ****************************************************************************************/
static void scan_params(int argc, char **argv);

/**
 *****************************************************************************************
 *  @brief      This function prints an error message and the usage information of 
 *              this program.
 *
 *  @param      err_str pointer to the error string that should be printed.
 *
 *  @return     void 
 ****************************************************************************************/
static void print_usage_info_and_exit(char *err_str);

/*
 * variables
 */

static struct vmem_struct *vmem;// = NULL; //!< Reference to shared memory
static int signal_number = 0;           //!< Number of signal received last
static sem_t *local_sem = NULL;                //!< OS-X Named semaphores will be stored locally due to pointer
pid_t mmanage_id;
int removedFrames;

int main(int argc, char **argv) {
    struct sigaction sigact;

    init_pagefile(); // init page file
    open_logger();   // open logfile

    /* Create shared memory and init vmem structure */
    vmem_init();
    TEST_AND_EXIT_ERRNO(!vmem, "Error initialising vmem");
    PRINT_DEBUG((stderr, "vmem successfully created\n"));

    // scan parameter 
    vmem->adm.program_name = argv[0];
    vmem->adm.page_rep_algo = VMEM_ALGO_FIFO;
    scan_params(argc, argv);

    /* Setup signal handler */
    sigact.sa_handler = sighandler;
    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = 0;

    TEST_AND_EXIT_ERRNO(sigaction(SIGUSR1, &sigact, NULL) == -1, "Error installing signal handler for USR1");
    PRINT_DEBUG((stderr, "USR1 handler successfully installed\n"));

    TEST_AND_EXIT_ERRNO(sigaction(SIGUSR2, &sigact, NULL) == -1, "Error installing signal handler for USR2");
    PRINT_DEBUG((stderr,  "USR2 handler successfully installed\n"));

    TEST_AND_EXIT_ERRNO(sigaction(SIGINT, &sigact, NULL) == -1, "Error installing signal handler for INT");
    PRINT_DEBUG((stderr, "INT handler successfully installed\n"));

    /* Signal processing loop */
    while(1) {
        signal_number = 0;
        pause();
        if(signal_number == SIGUSR1) {  /* Page fault */
            PRINT_DEBUG((stderr, "Processed SIGUSR1\n"));
            signal_number = 0;
        }
        else if(signal_number == SIGUSR2) {     /* PT dump */
            PRINT_DEBUG((stderr, "Processed SIGUSR2\n"));
            signal_number = 0;
        }
        else if(signal_number == SIGINT) {
            PRINT_DEBUG((stderr, "Processed SIGINT\n"));
        }
    }

    return 0;
}

void scan_params(int argc, char **argv) {
    int i = 0;
    unsigned char param_ok = FALSE;

    // scan all parameters (argv[0] points to program name)
    if (argc > 2) print_usage_info_and_exit("Wrong number of parameters.\n");

    for (i = 1; i < argc; i++) {
        param_ok = FALSE;
        if (0 == strcasecmp("-fifo", argv[i])) {
            // page replacement strategies fifo selected 
            vmem->adm.page_rep_algo = VMEM_ALGO_FIFO;
            param_ok = TRUE;
        }
        if (0 == strcasecmp("-clock", argv[i])) {
            // page replacement strategies clock selected 
            vmem->adm.page_rep_algo = VMEM_ALGO_CLOCK;
            param_ok = TRUE;
        }
        if (0 == strcasecmp("-aging", argv[i])) {
            // page replacement strategies aging selected 
            vmem->adm.page_rep_algo = VMEM_ALGO_AGING;
            param_ok = TRUE;
        }
        if (!param_ok) print_usage_info_and_exit("Undefined parameter.\n"); // undefined parameter found
    } // for loop
}

void print_usage_info_and_exit(char *err_str) {
    fprintf(stderr, "Wrong parameter: %s\n", err_str);
    fprintf(stderr, "Usage : %s [OPTIONS]\n", vmem->adm.program_name);
    fprintf(stderr, " -fifo     : Fifo page replacement algorithm.\n");
    fprintf(stderr, " -clock    : Clock page replacement algorithm.\n");
    fprintf(stderr, " -aging    : Aging page replacement algorithm.\n");
    fprintf(stderr, " -pagesize=[8,16,32,64] : Page size.\n");
    fflush(stderr);
    exit(EXIT_FAILURE);
}

void sighandler(int signo) {
    signal_number = signo;
    if(signo == SIGUSR1) {
    	printf("signal erhalten \n");
        allocate_page();
    } else if(signo == SIGUSR2) {
        dump_pt();
    } else if(signo == SIGINT) {
        cleanup();
        exit(EXIT_SUCCESS);
    }  
}

/* Your code goes here... */

void vmem_init(void) {
		key_t key;
		int shmid;
		key = ftok(SHMKEY , SHMPROCID);
		TEST_AND_EXIT_ERRNO(key == -1,"ftok: ftok failed");

		shmid = shmget(key, SHMSIZE, 0666 | IPC_CREAT);
		TEST_AND_EXIT_ERRNO(shmid < 0, "shmget: shmget failed");

		vmem = (struct vmem_struct*)shmat(shmid, NULL, 0);
		TEST_AND_EXIT_ERRNO(vmem == NULL, "shmat: shmat failed");

		local_sem = sem_open(NAMED_SEM, O_CREAT, 0644, 1);
		TEST_AND_EXIT_ERRNO(local_sem == NULL, "sem_init:sem_init failed");

//		//physikalischer speicher
//	    int data[VMEM_NFRAMES * VMEM_PAGESIZE];  //!< main memory used by virtual memory simulation
//
//	    //page table
	    int i;
	    for(i = 0; i < VMEM_NPAGES; i++) {
	    	vmem->pt.entries[i].flags = 0;
	    	vmem->pt.entries[i].frame = VOID_IDX;
	    }
	    int j;
	    for(j = 0; j < VMEM_NFRAMES; j++) {
	    	vmem->pt.framepage[j] = VOID_IDX;
	    }

	    //admin data
		vmem->adm.size = VMEM_VIRTMEMSIZE;
		vmem->adm.mmanage_pid = getpid();
		vmem->adm.g_count = 0;
		vmem->adm.pf_count = 0;
		vmem->adm.req_pageno = -1;
		vmem->adm.shm_id = 0;
		vmem->adm.page_rep_algo = VMEM_ALGO_CLOCK;
		vmem->adm.next_alloc_idx = 0;

		removedFrames = VOID_IDX;
	    //virtual memory
		printf("init done\n");
}

int find_free_frame() {
	int i;
	for(i = 0; i < VMEM_NFRAMES; i++) {
			if(vmem->pt.framepage[i] == VOID_IDX) {
				return i;
			}
		}
	return -1;
}

void allocate_page(void) {


	int freeFrameIdx = find_free_frame();
	if(freeFrameIdx == -1) {
		freeFrameIdx = find_remove_frame();
		printf("seite %d ersetzt\n", freeFrameIdx);
		removedFrames++;
		vmem->pt.entries[vmem->pt.framepage[freeFrameIdx]].flags &= ~PTF_PRESENT;
		store_page(freeFrameIdx);

	}
		update_pt(freeFrameIdx);
		fetch_page(vmem->adm.req_pageno);
		vmem->pt.entries[vmem->adm.req_pageno].flags |= PTF_PRESENT;

		dump_pt();
		printf("vor sem_post\n");
		sem_post(local_sem);
}

void fetch_page(int pt_idx) {
	int *frameStart = &vmem->data[vmem->pt.entries[pt_idx].frame * VMEM_PAGESIZE];
	fetch_page_from_pagefile(pt_idx, frameStart);
}

void store_page(int pt_idx) {
	store_page_to_pagefile(vmem->pt.framepage[pt_idx], &vmem->data[pt_idx * VMEM_PAGESIZE]);
}

void update_pt(int frame) {
	// in virtuelle seitentabelle zugehörige physikalische seite eintragen
	int pt_idx = vmem->adm.req_pageno;
	vmem->pt.entries[pt_idx].frame = frame;
	vmem->pt.entries[pt_idx].flags |= PTF_PRESENT;
	vmem->pt.framepage[frame] = pt_idx;
}

int find_remove_frame(void) {
	int frameToRemove;
	if(vmem->adm.page_rep_algo == VMEM_ALGO_FIFO) {
		frameToRemove = find_remove_fifo();
	}
	else if(vmem->adm.page_rep_algo == VMEM_ALGO_CLOCK) {
		frameToRemove = find_remove_clock();
	}
	else {
		frameToRemove = find_remove_aging();
	}
	return frameToRemove;
}

int find_remove_fifo(void) {
	int res = vmem->adm.next_alloc_idx;
	vmem->adm.next_alloc_idx = (vmem->adm.next_alloc_idx + 1) % VMEM_NFRAMES;
	return res;
}

int find_remove_clock(void) {
	int virtualPageIdx = vmem->pt.framepage[vmem->adm.next_alloc_idx];
	while((vmem->pt.entries[virtualPageIdx].flags & PTF_REF) == PTF_REF) {
		vmem->pt.entries[virtualPageIdx].flags = vmem->pt.entries[virtualPageIdx].flags & ~PTF_REF; //set reference bit 0
		vmem->adm.next_alloc_idx++;
		virtualPageIdx = vmem->pt.framepage[vmem->adm.next_alloc_idx];
	}
	vmem->adm.next_alloc_idx++;
	return vmem->adm.next_alloc_idx - 1;
}

int find_remove_aging(void) {
}

void cleanup(void) {
	TEST_AND_EXIT_ERRNO(sem_destroy(local_sem) == -1, "Semaphor konnte nicht zerstoert werden");
	shmdt(vmem);
	shmctl(SHMPROCID, IPC_RMID, NULL);
}

void dump_pt(void) {
	struct logevent logEvent;
	logEvent.alloc_frame = vmem->pt.entries[vmem->adm.req_pageno].frame; //die physikalische Seite die allokiert wurde
	logEvent.g_count = vmem->adm.g_count; //global-count
	logEvent.pf_count = vmem->adm.pf_count; //page fault count
	logEvent.replaced_page = removedFrames; //wie viele frames wurden bisher geloescht
	logEvent.req_pageno = vmem->adm.req_pageno; //die vituelle seite die geladen werden sollte

	logger(logEvent);
}
// EOF
