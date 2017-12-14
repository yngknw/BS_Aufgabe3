/**
 * @file vmaccess.c
 * @author Prof. Dr. Wolfgang Fohl, HAW Hamburg
 * @date 2010
 * @brief The access functions to virtual memory.
 */

#include "vmem.h"
#include "debug.h"
#include "signal.h"
/*
 * static variables
 */

static struct vmem_struct *vmem = NULL; //!< Reference to virtual memory

/**
 *****************************************************************************************
 *  @brief      This function setup the connection to virtual memory.
 *              The virtual memory has to be created by mmanage.c module.
 *
 *  @return     void
 ****************************************************************************************/
static void vmem_init(void) {

	key_t key;
	int shmid;

	key = ftok(SHMKEY , SHMPROCID);
	TEST_AND_EXIT_ERRNO(key == -1,"ftok: ftok failed");

	shmid = shmget(key, SHMSIZE, 0666 | IPC_CREAT);
	TEST_AND_EXIT_ERRNO(shmid < 0, "shmget: shmget failed");

	vmem = (struct vmem_struct*) shmat(shmid, NULL, 0);
	TEST_AND_EXIT_ERRNO(shmid < 0, "shmat: shmat failed");

	sem_t *local_sem = sem_open(NAMED_SEM, 0); /* Open a preexisting semaphore. */

}

/**
 *****************************************************************************************
 *  @brief      This function does aging for aging page replacement algorithm.
 *              It will be called periodic based on g_count.
 *              This function must be used only when aging page replacement algorithm is active.
 *              Otherwise update_age_reset_ref may interfere with other page replacement 
 *              algorithms that base on PTF_REF bit.
 *
 *  @return     void
 ****************************************************************************************/
static void update_age_reset_ref(void) {
	int referencedBit;
	int virtualPage;
	int i;
	for(i = 0; i < VMEM_NFRAMES; i++) {
		virtualPage = vmem->pt.framepage[i];
		if(virtualPage != VOID_IDX) {
			referencedBit = vmem->pt.entries[virtualPage].flags & PTF_REF;
//			printf("referenced bit: %d, age: %d \n" , referencedBit, vmem->pt.entries[virtualPage].age);
			vmem->pt.entries[virtualPage].age = (vmem->pt.entries[virtualPage].age / 2);
//			printf("rechtsshift: %d \n", vmem->pt.entries[virtualPage].age);
			referencedBit = referencedBit * 32;
//			printf("referenced bit linksshift: %d\n", referencedBit);
			vmem->pt.entries[virtualPage].age |= referencedBit;
			vmem->pt.entries[virtualPage].flags &= ~PTF_REF;
//			printf("new age: %d\n", vmem->pt.entries[virtualPage].age);

		}
	}
}

/**
 *****************************************************************************************
 *  @brief      This function puts a page into memory (if required).
 *              It must be called by vmem_read and vmem_write
 *
 *  @param      address The page that stores the contents of this address will be put in (if required).
 * 
 *  @return     void
 ****************************************************************************************/
static void vmem_put_page_into_mem(int address) {
	int page_idx = address / VMEM_PAGESIZE;
	if((vmem->pt.entries[page_idx].flags & PTF_PRESENT) == 0) {
		vmem->adm.pf_count++;
		vmem->adm.req_pageno = page_idx;
		// mmanage signal
		kill(vmem->adm.mmanage_pid, SIGUSR1);
		sem_wait(sem_open(NAMED_SEM, 0));
	}

	vmem->adm.g_count++;
}

int vmem_read(int address) {
	if(vmem == NULL) {
		vmem_init();
	}
	int page_idx = address / VMEM_PAGESIZE;
	int offset = address - (VMEM_PAGESIZE * page_idx);
	vmem_put_page_into_mem(address);

	vmem->pt.entries[page_idx].flags |= PTF_REF;
	if(vmem->adm.g_count % UPDATE_AGE_COUNT == 0 && vmem->adm.page_rep_algo == VMEM_ALGO_AGING) {
		update_age_reset_ref();
	}
	page_idx = vmem->pt.entries[page_idx].frame;
	return vmem->data[(page_idx * VMEM_PAGESIZE) + offset];
}

void vmem_write(int address, int data) {
	if(vmem == NULL) {
		vmem_init();
	}
//	if(vmem->pt.framepage[15] != VOID_IDX)
//	{
////		int i;
////		printf("\n");
////		printf("%d\n", vmem->adm.next_alloc_idx);
////		for(i = 0; i < VMEM_NFRAMES; i++) {
////			printf("virAdr: %d: physAdr: %d  \n", vmem->pt.framepage[i], vmem->pt.entries[vmem->pt.framepage[i]].frame );
////			printf("r-bit: %d \n", vmem->pt.entries[vmem->pt.framepage[i]].flags & PTF_REF);
////	//				printf("replacedFrame: %d\n");
////			}
//	}

	int page_idx = address / VMEM_PAGESIZE;
	int offset = address - (VMEM_PAGESIZE * page_idx);

	vmem_put_page_into_mem(address);

	vmem->pt.entries[page_idx].flags |= PTF_DIRTY; //seite wurde beschrieben
	vmem->pt.entries[page_idx].flags |= PTF_REF; //seite wurde referenziert
	if(vmem->adm.g_count % UPDATE_AGE_COUNT == 0 && vmem->adm.page_rep_algo == VMEM_ALGO_AGING) {
		update_age_reset_ref();
	}
	int frame_idx = vmem->pt.entries[page_idx].frame;
	vmem->data[(frame_idx * VMEM_PAGESIZE) + offset] = data;




}

// EOF
