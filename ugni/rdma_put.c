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
    int transfer_length_in_bytes, transfer_length, iters;

    if(argc == 3) {
	transfer_length_in_bytes = atoi(argv[1]);
	transfer_length = transfer_length_in_bytes/sizeof(uint64_t);
	iters = atoi(argv[2]);
    } else {
	transfer_length_in_bytes = TRANSFER_LENGTH_IN_BYTES;
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
	    (transfer_length_in_bytes * iters));
    assert(rc == 0);

    /*Initialize the buffer to all zeros.*/
    memset(send_buffer, 0, (transfer_length_in_bytes * iters));

    rc = posix_memalign((void **) &receive_buffer, 64,
	    (transfer_length_in_bytes * iters));
    assert(rc == 0);

    /*Initialize the buffer to all zeros.*/
    memset(receive_buffer, 0, (transfer_length_in_bytes * iters));

    node.uGNI_regAndExchangeMem(send_buffer, transfer_length_in_bytes * iters, receive_buffer, transfer_length_in_bytes * iters);

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

    if(node.world_rank == 2) {
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

    if(node.world_rank == 0)
        printf("Size   Bandwidth   Latency\n");

    int nbytes = transfer_length_in_bytes;
    int win_size =0;
    int min_size = 128*1024;
    int max_size = 4*1024*1024;
    int num_loops = 0;
    int max_loops = nbytes/min_size*iters;

    /* Allocate the rdma_data_desc array. */
    rdma_data_desc = (gni_post_descriptor_t *) calloc(max_loops, sizeof(gni_post_descriptor_t));
    assert(rdma_data_desc != NULL);

    /*Set up the data request*/
    for (i = 0; i < max_loops; i++) {
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

    int num_transfers = 0;

    for(win_size = min_size; win_size <= max_size; win_size *= 2) {
	num_transfers = nbytes/win_size;
	num_loops = iters*num_transfers;

	gettimeofday(&t1, NULL);

	if(node.world_rank == 0) {
	    for (i = 0; i < num_loops; i++) {
		/* Send the data. */
		rdma_data_desc[i].local_addr = (uint64_t) send_buffer;
		rdma_data_desc[i].local_addr += i * win_size;
		rdma_data_desc[i].remote_addr = node.remote_memory_handle_array[send_to].addr + sizeof(uint64_t);
		rdma_data_desc[i].remote_addr += i * win_size;
		rdma_data_desc[i].length = win_size - sizeof(uint64_t);
		status = GNI_PostRdma(node.endpoint_handles_array[send_to], &rdma_data_desc[i]);
		if (status != GNI_RC_SUCCESS) {
		    fprintf(stdout, "[%s] Rank: %4i GNI_PostRdma data ERROR status: %d\n", uts_info.nodename, node.world_rank, status);
		    continue;
		}
	    }   /* end of for loop for transfers */

	    //Check to make sure that all sends are done.
	    node.uGNI_waitAllSendDone(send_to, node.cq_handle, num_loops);
	}

	/*Act as a proxy*/
	if(node.world_rank == 2) {
	    for(i = 0; i < num_loops; i++) {
		node.uGNI_waitRecvDone(receive_from, node.destination_cq_handle);

		rdma_data_desc[i].local_addr = (uint64_t) send_buffer;
                rdma_data_desc[i].local_addr += i * win_size;
                rdma_data_desc[i].remote_addr = node.remote_memory_handle_array[send_to].addr + sizeof(uint64_t);
                rdma_data_desc[i].remote_addr += i * win_size;
		rdma_data_desc[i].length = win_size - sizeof(uint64_t);

		status = GNI_PostRdma(node.endpoint_handles_array[send_to], &rdma_data_desc[i]);
		if (status != GNI_RC_SUCCESS) {
		    fprintf(stdout, "[%s] Rank: %4i GNI_PostRdma data ERROR status: %d\n", uts_info.nodename, node.world_rank, status);
		    continue;
		}
	    }
	    //Check to make sure that all sends are done.
	    node.uGNI_waitAllSendDone(send_to, node.cq_handle, num_loops);
	}

	/*Destination to receive data*/
	if(node.world_rank == 3) {
	    //Check to get all data received
	    node.uGNI_waitAllRecvDone(receive_from, node.destination_cq_handle, num_loops);
	}

	gettimeofday(&t2, NULL);

	double latency = ((t2.tv_sec * 1000000 + t2.tv_usec) - (t1.tv_sec * 1000000 + t1.tv_usec))*1.0/iters;
	double max_latency = 0;
	MPI_Reduce(&latency, &max_latency, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

	if(node.world_rank == 0) {
	    double bandwidth = transfer_length_in_bytes*1000000.0/(max_latency*1024*1024);
	    printf("%d \t %8.6f \t %8.4f\n", win_size, bandwidth, latency);
	}

    }

    node.uGNI_finalize();

    free(receive_buffer);
    free(send_buffer);
    free(rdma_data_desc);

    PMI_Finalize();

    return rc;
}
