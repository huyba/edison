/*
 ** This code is to test rdma put function of ugni
 ** @author: Huy Bui
 ** @date: July 11, 2014
 **
 */

#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <assert.h>
#include <malloc.h>
#include <sched.h>
#include <sys/utsname.h>
#include <errno.h>

#include "gni_pub.h"
#include "pmi.h"
#include "mpi.h"

#include "node.h"

#define CACHELINE_MASK           0x3F   /* 64 byte cacheline */
#define NUMBER_OF_TRANSFERS      10
#define SEND_DATA                0xdddd000000000000
#define TRANSFER_LENGTH          1024
#define TRANSFER_LENGTH_IN_BYTES ((TRANSFER_LENGTH)*sizeof(uint64_t))

int             compare_data_failed = 0;
struct utsname  uts_info;
//int             v_option = 0;

//#include "ugni_util.h"

int main(int argc, char **argv)
{
    int nbytes, transfer_length, iters;
    int min_wsize, max_wsize;
    if(argc == 5) {
	min_wsize = atoi(argv[1])*1024;
	max_wsize = atoi(argv[2])*1024;
	nbytes = atoi(argv[3])*1024;
	transfer_length = nbytes/sizeof(uint64_t);
	iters = atoi(argv[4]);
    } else {
	min_wsize = 128*1024;
	max_wsize = 4*1024*1024;
	nbytes = TRANSFER_LENGTH_IN_BYTES;
	transfer_length = TRANSFER_LENGTH;
	iters = NUMBER_OF_TRANSFERS;
    }

    MPI_Init(&argc, &argv);

    int             create_destination_cq = 1;
    uint64_t        data = SEND_DATA;
    register int    i;
    register int    j;
    int             my_id;
    int             my_receive_from;
    int             rc;
    gni_post_descriptor_t *rdma_data_desc;
    int             receive_from;
    unsigned int    remote_address;
    uint64_t       *receive_buffer;
    uint64_t        receive_data = SEND_DATA;
    uint64_t       *send_buffer;
    int             send_to;
    gni_return_t    status = GNI_RC_SUCCESS;

    int number_of_cq_entries  = iters;
    int number_of_dest_cq_entries = 1;

    Node node;
    node.uGNI_init();

    node.uGNI_createBasicCQ(number_of_cq_entries, number_of_dest_cq_entries);
    node.uGNI_createAndBindEndpoints();

    rc = posix_memalign((void **) &send_buffer, 64,
	    (nbytes * iters));
    assert(rc == 0);

    /*Initialize the buffer to all zeros.*/
    memset(send_buffer, 0, (nbytes * iters));

    rc = posix_memalign((void **) &receive_buffer, 64,
	    (nbytes * iters));
    assert(rc == 0);

    /*Initialize the buffer to all zeros.*/
    memset(receive_buffer, 0, (nbytes * iters));

    node.uGNI_regAndExchangeMem(send_buffer, nbytes * iters, receive_buffer, nbytes * iters);

    /*
     * Determine who we are going to send our data to and
     * who we are going to receive data from.
     */

    struct timeval t1, t2;

    send_to = (node.world_rank + 1) % node.world_size;
    receive_from = (node.world_size + node.world_rank - 1) % node.world_size;
    my_receive_from = (receive_from & 0xffffff) << 24;
    my_id = (node.world_rank & 0xffffff) << 24;

    if(node.world_rank == 0){
	send_to = 2;
    }

    if(node.world_rank == 2 || node.world_rank == 1) {
	receive_from = 0;
	send_to = 3;
    }

    if(node.world_rank == 3) {
	receive_from = 2;
    }

    node.uGNI_getTopoInfo();

    for(i = 0; i < iters; i++) {
	/*
	 * Initialize the data to be sent.
	 * The source data will look like: 0xddddlllllltttttt
	 *     where: dddd is the actual value
	 *            llllll is the rank for this process
	 *            tttttt is the transfer number
	 */
	data = SEND_DATA + my_id + i + 1;

	for (j = 0; j < transfer_length; j++) {
	    send_buffer[j + (i * transfer_length)] = data;
	}
    }

    if(node.world_rank == 0) {
	printf("min_wsize = %d, max_wsize = %d, message_size = %d, iters = %d\n", min_wsize, max_wsize, nbytes, iters);
        printf("Size   Bandwidth   Latency\n");
    }

    int win_size =0;
    int num_loops = 0;
    int num_wins = nbytes/min_wsize*iters;

    /* Allocate the rdma_data_desc array. */
    rdma_data_desc = (gni_post_descriptor_t *) calloc(num_wins, sizeof(gni_post_descriptor_t));
    assert(rdma_data_desc != NULL);

    /*Set up the data request*/
    for (i = 0; i < num_wins; i++) {
	/*
	 * Setup the data request.
	 *    type is RDMA_PUT.
	 *    cq_mode states what type of events should be sent.
	 *         GNI_CQMODE_GLOBAL_EVENT allows for the sending of an event
	 *             to the local node after the receipt of the data.
	 *         GNI_CQMODE_REMOTE_EVENT allows for the sending of an event
	 *             to the remote node after the receipt of the data.
	 *    dlvr_mode states the delivery mode.
	 *    local_addr is the address of the sending buffer.
	 *    local_mem_hndl is the memory handle of the sending buffer.
	 *    remote_addr is the the address of the receiving buffer.
	 *    remote_mem_hndl is the memory handle of the receiving buffer.
	 *    length is the amount of data to transfer.
	 *    rdma_mode states how the request will be handled.
	 *    src_cq_hndl is the source complete queue handle.
	 */

	rdma_data_desc[i].type = GNI_POST_RDMA_PUT;
	if (create_destination_cq != 0) {
	    rdma_data_desc[i].cq_mode = GNI_CQMODE_GLOBAL_EVENT | GNI_CQMODE_REMOTE_EVENT;
	} else {
	    rdma_data_desc[i].cq_mode = GNI_CQMODE_GLOBAL_EVENT;
	}
	rdma_data_desc[i].dlvr_mode = GNI_DLVMODE_PERFORMANCE;
	rdma_data_desc[i].local_addr = (uint64_t) send_buffer;
	//rdma_data_desc[i].local_addr += i * transfer_length_in_bytes;
	rdma_data_desc[i].local_mem_hndl = node.send_mem_handle;
	rdma_data_desc[i].remote_addr = node.remote_memory_handle_array[send_to].addr + sizeof(uint64_t);
	//rdma_data_desc[i].remote_addr += i * transfer_length_in_bytes;
	rdma_data_desc[i].remote_mem_hndl = node.remote_memory_handle_array[send_to].mdh;
	//rdma_data_desc[i].length = transfer_length_in_bytes - sizeof(uint64_t);
	rdma_data_desc[i].rdma_mode = GNI_RDMAMODE_FENCE;
	rdma_data_desc[i].src_cq_hndl = node.cq_handle;
    }


    MPI_Barrier(MPI_COMM_WORLD);

    int num_transfers = 0;

    /*Through proxies using pipeline technique*/
    for(win_size = min_wsize; win_size <= max_wsize; win_size *= 2) {
	num_transfers = nbytes/win_size;
	num_loops = iters*num_transfers;

	if(node.world_rank == 0)
	    printf("win_size = %d, num_transfers = %d, num_loops = %d\n", win_size, num_transfers, num_loops);

	gettimeofday(&t1, NULL);

	if(node.world_rank == 0) {
	    send_to = 2;
	    for (i = 0; i < num_loops/2; i++) {
		/* Send the data. */
		rdma_data_desc[i].local_addr = (uint64_t) send_buffer;
		rdma_data_desc[i].local_addr += i * win_size;
		rdma_data_desc[i].remote_addr = node.remote_memory_handle_array[send_to].addr + sizeof(uint64_t);
		rdma_data_desc[i].remote_addr += i * win_size;
		rdma_data_desc[i].length = win_size - sizeof(uint64_t);
		status = GNI_PostRdma(node.endpoint_handles_array[send_to], &rdma_data_desc[i]);
		if (status != GNI_RC_SUCCESS) {
		    fprintf(stdout, "[%s] Rank: %4i GNI_PostRdma data ERROR status: %d\n", uts_info.nodename, node.world_rank, status);
		    postRdmaStatus(status);
		    continue;
		}
		//printf("Rank 0 posted a message\n");
	    }   /* end of for loop for transfers */

	    send_to = 1;
            for (i = num_loops/2; i < num_loops; i++) {
                /* Send the data. */
                rdma_data_desc[i].local_addr = (uint64_t) send_buffer;
                rdma_data_desc[i].local_addr += i * win_size;
                rdma_data_desc[i].remote_addr = node.remote_memory_handle_array[send_to].addr + sizeof(uint64_t);
                rdma_data_desc[i].remote_addr += i * win_size;
                rdma_data_desc[i].length = win_size - sizeof(uint64_t);
                status = GNI_PostRdma(node.endpoint_handles_array[send_to], &rdma_data_desc[i]);
                if (status != GNI_RC_SUCCESS) {
                    fprintf(stdout, "[%s] Rank: %4i GNI_PostRdma data ERROR status: %d\n", uts_info.nodename, node.world_rank, status);
                    postRdmaStatus(status);
                    continue;
                }
                //printf("Rank 0 posted a message\n");
            }   /* end of for loop for transfers */

	    //Check to make sure that all sends are done.
	    node.uGNI_waitAllSendDone(2, node.cq_handle, num_loops/2);
	    node.uGNI_waitAllSendDone(1, node.cq_handle, num_loops/2);
	    //printf("Rank 0 done all\n");
	}

	/*Act as a proxy*/
	if(node.world_rank == 2) {
	    for(i = 0; i < num_loops/2; i++) {
		node.uGNI_waitRecvDone(receive_from, node.destination_cq_handle);
		//printf("done waiting at rank 2\n");
		rdma_data_desc[i].local_addr = (uint64_t) send_buffer;
                rdma_data_desc[i].local_addr += i * win_size;
                rdma_data_desc[i].remote_addr = node.remote_memory_handle_array[send_to].addr + sizeof(uint64_t);
                rdma_data_desc[i].remote_addr += i * win_size;
		rdma_data_desc[i].length = win_size - sizeof(uint64_t);

		status = GNI_PostRdma(node.endpoint_handles_array[send_to], &rdma_data_desc[i]);
		if (status != GNI_RC_SUCCESS) {
		    fprintf(stdout, "[%s] Rank: %4i GNI_PostRdma data ERROR status: %d\n", uts_info.nodename, node.world_rank, status);
		    postRdmaStatus(status);
		    continue;
		}
		//printf("Rank 2 posted a message\n");
	    }
	    //Check to make sure that all sends are done.
	    node.uGNI_waitAllSendDone(send_to, node.cq_handle, num_loops/2);
	    //printf("Rank 2 done all\n");
	}

	if(node.world_rank == 1) {
            for(i = num_loops/2; i < num_loops; i++) {
                node.uGNI_waitRecvDone(receive_from, node.destination_cq_handle);
                //printf("done waiting at rank 1\n");
                rdma_data_desc[i].local_addr = (uint64_t) send_buffer;
                rdma_data_desc[i].local_addr += i * win_size;
                rdma_data_desc[i].remote_addr = node.remote_memory_handle_array[send_to].addr + sizeof(uint64_t);
                rdma_data_desc[i].remote_addr += i * win_size;
                rdma_data_desc[i].length = win_size - sizeof(uint64_t);

                status = GNI_PostRdma(node.endpoint_handles_array[send_to], &rdma_data_desc[i]);
                if (status != GNI_RC_SUCCESS) {
                    fprintf(stdout, "[%s] Rank: %4i GNI_PostRdma data ERROR status: %d\n", uts_info.nodename, node.world_rank, status);
                    postRdmaStatus(status);
                    continue;
                }
                //printf("Rank 1 posted a message\n");
            }
            //Check to make sure that all sends are done.
            node.uGNI_waitAllSendDone(send_to, node.cq_handle, num_loops/2);
            //printf("Rank 1 done all\n");
        }

	/*Destination to receive data*/
	if(node.world_rank == 3) {
	    receive_from = 2;
	    //Check to get all data received
	    node.uGNI_waitAllRecvDone(receive_from, node.destination_cq_handle, num_loops/2);
	    receive_from = 1;
	    node.uGNI_waitAllRecvDone(receive_from, node.destination_cq_handle, num_loops/2);
	    //printf("Rank 3 done waiting all\n");
	}

	gettimeofday(&t2, NULL);

	double latency = ((t2.tv_sec * 1000000 + t2.tv_usec) - (t1.tv_sec * 1000000 + t1.tv_usec))*1.0/iters;
	double max_latency = 0;
	MPI_Reduce(&latency, &max_latency, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

	if(node.world_rank == 0) {
	    double bandwidth = nbytes*1000000.0/(max_latency*1024*1024);
	    printf("%d \t %8.6f \t %8.4f\n", win_size, bandwidth, max_latency);
	}

    }

    /*Direct transfer*/

    MPI_Barrier(MPI_COMM_WORLD);

    gettimeofday(&t1, NULL);
    if(node.world_rank == 0) {
        send_to = 3;
        for(i = 0; i < iters; i++) {
            rdma_data_desc[i].local_addr = (uint64_t) send_buffer;
            rdma_data_desc[i].local_addr += i * nbytes;
            rdma_data_desc[i].remote_addr = node.remote_memory_handle_array[send_to].addr + sizeof(uint64_t);
            rdma_data_desc[i].remote_addr += i * nbytes;
            rdma_data_desc[i].length = nbytes- sizeof(uint64_t);
            status = GNI_PostRdma(node.endpoint_handles_array[send_to], &rdma_data_desc[i]);
            if (status != GNI_RC_SUCCESS) {
                fprintf(stdout, "[%s] Rank: %4i GNI_PostRdma data ERROR status: %d\n", uts_info.nodename, node.world_rank, status);
                postRdmaStatus(status);
                continue;
            }
        }
    }

    if(node.world_rank == 3) {
        receive_from = 0;
        node.uGNI_waitAllRecvDone(receive_from, node.destination_cq_handle, iters);
    }

    gettimeofday(&t2, NULL);

    double latency = ((t2.tv_sec * 1000000 + t2.tv_usec) - (t1.tv_sec * 1000000 + t1.tv_usec))*1.0/iters;
    double max_latency = 0;
    MPI_Reduce(&latency, &max_latency, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    if(node.world_rank == 0) {
        double bandwidth = nbytes*1000000.0/(max_latency*1024*1024);
        printf("%d \t %8.6f \t %8.4f\n", nbytes, bandwidth, max_latency);
        printf("\nTransfer through proxies using pipeline\n");
    }

    MPI_Barrier(MPI_COMM_WORLD);

    node.uGNI_finalize();

    free(receive_buffer);
    free(send_buffer);
    free(rdma_data_desc);

    PMI_Finalize();

    return rc;
}
