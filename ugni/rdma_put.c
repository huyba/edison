/*
 * Copyright (c) 2011 Cray Inc.  All Rights Reserved.
 */

/*
 * RDMA Put test example - this test only uses PMI
 *
 * Note: this test should not be run oversubscribed on nodes, i.e. more
 * instances on a given node than cpus, owing to the busy wait for
 * incoming data.
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

#include "node.h"

#define CACHELINE_MASK           0x3F   /* 64 byte cacheline */
#define NUMBER_OF_TRANSFERS      10
#define SEND_DATA                0xdddd000000000000
#define TRANSFER_LENGTH          1024
#define TRANSFER_LENGTH_IN_BYTES ((TRANSFER_LENGTH)*sizeof(uint64_t))

int             compare_data_failed = 0;
struct utsname  uts_info;
//int             v_option = 0;

#include "ugni_util.h"

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

    unsigned int   *all_nic_addresses;
    int             cookie;
    int             create_destination_cq = 1;
    int             create_destination_overrun = 0;
    gni_cq_entry_t  current_event;
    uint64_t        data = SEND_DATA;
    int             data_transfers_sent = 0;
    int             device_id = 0;
    int             event_id;
    gni_post_descriptor_t *event_post_desc_ptr;
    register int    i;
    register int    j;
    unsigned int    local_address;
    int             modes = GNI_CDM_MODE_BTE_SINGLE_CHANNEL;
    int             my_id;
    //mdh_addr_t      my_memory_handle;
    int             my_receive_from;
    int             rc;
    gni_post_descriptor_t *rdma_data_desc;
    int             receive_from;
    unsigned int    remote_address;
    //gni_mem_handle_t receive_memory_handle;
    //mdh_addr_t     *remote_memory_handle_array;
    uint64_t       *receive_buffer;
    uint64_t        receive_data = SEND_DATA;
    uint64_t       *send_buffer;
    int             send_to;
    //gni_mem_handle_t source_memory_handle;
    gni_return_t    status = GNI_RC_SUCCESS;
    char           *text_pointer;
    //gni_ep_handle_t *endpoint_handles_array;

    int number_of_cq_entries  = iters;
    int number_of_dest_cq_entries = 1;

    Node node;
    node.uGNI_init();

    node.uGNI_createBasicCQ(number_of_cq_entries, number_of_dest_cq_entries);
    node.uGNI_createAndBindEndpoints();

    /*gni_cq_handle_t cq_handle, destination_cq_handle;

    status = GNI_CqCreate(node.nic_handle, number_of_cq_entries, 0, GNI_CQ_NOBLOCK, NULL, NULL, &cq_handle);
    if (status != GNI_RC_SUCCESS) {
	fprintf(stdout, "[%s] Rank: %4i GNI_CqCreate source ERROR status: %d\n", uts_info.nodename, node.world_rank, status);
	INCREMENT_ABORTED;
	goto EXIT_DOMAIN;
    }

    status = GNI_CqCreate(node.nic_handle, number_of_dest_cq_entries, 0, GNI_CQ_NOBLOCK, NULL, NULL, &destination_cq_handle);
    if (status != GNI_RC_SUCCESS) {
	fprintf(stdout,
		"[%s] Rank: %4i GNI_CqCreate      destination ERROR status: %d\n",
		uts_info.nodename, node.world_rank, status);
	INCREMENT_ABORTED;
	goto EXIT_CQ;
    }*/

    /* Allocate the endpoint handles array. */
   /* endpoint_handles_array = (gni_ep_handle_t *) calloc(node.world_size, sizeof(gni_ep_handle_t));
    assert(endpoint_handles_array != NULL);

    uGNI_CreateAndBindEndpoints(node.world_rank, node.world_size, node.cq_handle, node.nic_handle, endpoint_handles_array);
    */

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

    /*status = GNI_MemRegister(node.nic_handle, (uint64_t) send_buffer,
	    (transfer_length_in_bytes * iters), NULL,
	    GNI_MEM_READWRITE, -1,
	    &source_memory_handle);
    if (status != GNI_RC_SUCCESS) {
	fprintf(stdout,
		"[%s] Rank: %4i GNI_MemRegister  send_buffer ERROR status: %d\n",
		uts_info.nodename, node.world_rank, status);
    }

    status = GNI_MemRegister(node.nic_handle, (uint64_t) receive_buffer,
	    transfer_length_in_bytes * iters, node.destination_cq_handle,
	    GNI_MEM_READWRITE,
	    -1, &receive_memory_handle);
    if (status != GNI_RC_SUCCESS) {
	fprintf(stdout,
		"[%s] Rank: %4i GNI_MemRegister   receive_buffer ERROR status: %d\n",
		uts_info.nodename, node.world_rank, status);
    }

    remote_memory_handle_array = (mdh_addr_t *) calloc(node.world_size, sizeof(mdh_addr_t));
    assert(remote_memory_handle_array);

    my_memory_handle.addr = (uint64_t) receive_buffer;
    my_memory_handle.mdh = receive_memory_handle;

    allgather(&my_memory_handle, remote_memory_handle_array, sizeof(mdh_addr_t));
    */

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
	send_to = 3;
	receive_from = 3;
    }

    if(node.world_rank == 3) {
	send_to = 0;
	receive_from = 0;
    }

    topo_info(node.world_rank);

    /* Allocate the rdma_data_desc array. */
    rdma_data_desc = (gni_post_descriptor_t *) calloc(iters, sizeof(gni_post_descriptor_t));
    assert(rdma_data_desc != NULL);

    if(node.world_rank == 0) {
	// Start measuring data
	if(node.world_rank == 0) {
	    printf("Size\tBandwidth\tLatency\n");
	    gettimeofday(&t1, NULL);
	}
	for (i = 0; i < iters; i++) {
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
	    rdma_data_desc[i].local_addr += i * transfer_length_in_bytes;
	    rdma_data_desc[i].local_mem_hndl = node.send_mem_handle;
	    rdma_data_desc[i].remote_addr = node.remote_memory_handle_array[send_to].addr + sizeof(uint64_t);
	    rdma_data_desc[i].remote_addr += i * transfer_length_in_bytes;
	    rdma_data_desc[i].remote_mem_hndl = node.remote_memory_handle_array[send_to].mdh;
	    rdma_data_desc[i].length = transfer_length_in_bytes - sizeof(uint64_t);
	    rdma_data_desc[i].rdma_mode = GNI_RDMAMODE_FENCE;
	    rdma_data_desc[i].src_cq_hndl = node.cq_handle;

	    /* Send the data. */

	    status = GNI_PostRdma(node.endpoint_handles_array[send_to], &rdma_data_desc[i]);
	    if (status != GNI_RC_SUCCESS) {
		fprintf(stdout, "[%s] Rank: %4i GNI_PostRdma data ERROR status: %d\n", uts_info.nodename, node.world_rank, status);
		continue;
	    }

	    data_transfers_sent++;
	}   /* end of for loop for transfers */

	//Check to make sure that all sends are done.
	uGNI_WaitAllSendDone(node.world_rank, send_to, node.cq_handle, iters);

	if(node.world_rank == 0) {
	    gettimeofday(&t2, NULL);
	    double latency = ((t2.tv_sec * 1000000 + t2.tv_usec) - (t1.tv_sec * 1000000 + t1.tv_usec))*1.0/iters;
	    double bandwidth = transfer_length_in_bytes*1000000.0/(latency*1024*1024);
	    printf("%d \t %8.6f \t %8.4f\n", transfer_length_in_bytes, bandwidth, latency);
	}
    }

    if(node.world_rank == 4) {
	//Check to get all data received
	uGNI_WaitAllRecvDone(node.world_rank, receive_from, node.destination_cq_handle, iters);
    }

EXIT_WAIT_BARRIER:
    /*
     * Wait for all the processes to finish before we clean up and exit.
     */

    rc = PMI_Barrier();
    assert(rc == PMI_SUCCESS);

    /*
     * Free allocated memory.
     */

    free(node.remote_memory_handle_array);

    /*
     * Deregister the memory associated for the receive buffer with the NIC.
     *     node.nic_handle is our NIC handle.
     *     receive_memory_handle is the handle for this memory region.
     */

    status = GNI_MemDeregister(node.nic_handle, &node.recv_mem_handle);
    if (status != GNI_RC_SUCCESS) {
	fprintf(stdout,
		"[%s] Rank: %4i GNI_MemDeregister receive_buffer ERROR status: %d\n",
		uts_info.nodename, node.world_rank, status);
	//INCREMENT_ABORTED;
    } else {
	if (v_option > 1) {
	    fprintf(stdout,
		    "[%s] Rank: %4i GNI_MemDeregister receive_buffer    NIC: %p\n",
		    uts_info.nodename, node.world_rank, node.nic_handle);
	}

	/*
	 * Free allocated memory.
	 */

	free(receive_buffer);
    }

EXIT_MEMORY_SOURCE:

    /*
     * Deregister the memory associated for the send buffer with the NIC.
     *     node.nic_handle is our NIC handle.
     *     source_memory_handle is the handle for this memory region.
     */

    status = GNI_MemDeregister(node.nic_handle, &node.send_mem_handle);
    if (status != GNI_RC_SUCCESS) {
	fprintf(stdout,
		"[%s] Rank: %4i GNI_MemDeregister send_buffer ERROR status: %d\n",
		uts_info.nodename, node.world_rank, status);
	//INCREMENT_ABORTED;
    } else {
	if (v_option > 1) {
	    fprintf(stdout,
		    "[%s] Rank: %4i GNI_MemDeregister send_buffer       NIC: %p\n",
		    uts_info.nodename, node.world_rank, node.nic_handle);
	}

	/*
	 * Free allocated memory.
	 */

	free(send_buffer);
    }

EXIT_ENDPOINT:

    /*
     * Remove the endpoints to all of the ranks.
     *
     * Note: if there are outstanding events in the completion queue,
     *       the endpoint can not be unbound.
     */

    for (i = 0; i < node.world_size; i++) {
	if (i == node.world_rank) {
	    continue;
	}

	if (node.endpoint_handles_array[i] == 0) {

	    /*
	     * This endpoint does not exist.
	     */

	    continue;
	}

	/*
	 * Unbind the remote address from the endpoint handler.
	 *     endpoint_handles_array is the endpoint handle that is being unbound
	 */

	status = GNI_EpUnbind(node.endpoint_handles_array[i]);
	if (status != GNI_RC_SUCCESS) {
	    fprintf(stdout,
		    "[%s] Rank: %4i GNI_EpUnbind      ERROR remote rank: %4i status: %d\n",
		    uts_info.nodename, node.world_rank, i, status);
	    continue;
	}

	if (v_option > 1) {
	    fprintf(stdout,
		    "[%s] Rank: %4i GNI_EpUnbind      remote rank: %4i EP:  %p\n",
		    uts_info.nodename, node.world_rank, i,
		    node.endpoint_handles_array[i]);
	}

	/*
	 * You must do an EpDestroy for each endpoint pair.
	 *
	 * Destroy the logical endpoint for each rank.
	 *     endpoint_handles_array is the endpoint handle that is being
	 *         destroyed.
	 */

	status = GNI_EpDestroy(node.endpoint_handles_array[i]);
	if (status != GNI_RC_SUCCESS) {
	    fprintf(stdout,
		    "[%s] Rank: %4i GNI_EpDestroy     ERROR remote rank: %4i status: %d\n",
		    uts_info.nodename, node.world_rank, i, status);
	    continue;
	}

	if (v_option > 1) {
	    fprintf(stdout,
		    "[%s] Rank: %4i GNI_EpDestroy     remote rank: %4i EP:  %p\n",
		    uts_info.nodename, node.world_rank, i,
		    node.endpoint_handles_array[i]);
	}
    }

    /*
     * Free allocated memory.
     */

    free (node.endpoint_handles_array);

    if (create_destination_cq != 0) {
	/*
	 * Destroy the destination completion queue.
	 *     cq_handle is the handle that is being destroyed.
	 */

	status = GNI_CqDestroy(node.destination_cq_handle);
	if (status != GNI_RC_SUCCESS) {
	    fprintf(stdout,
		    "[%s] Rank: %4i GNI_CqDestroy     destination ERROR status: %d\n",
		    uts_info.nodename, node.world_rank, status);
	} else if (v_option > 1) {
	    fprintf(stdout,
		    "[%s] Rank: %4i GNI_CqDestroy     destination\n",
		    uts_info.nodename, node.world_rank);
	}
    }

EXIT_CQ:

    /*
     * Destroy the completion queue.
     *     cq_handle is the handle that is being destroyed.
     */

    status = GNI_CqDestroy(node.cq_handle);
    if (status != GNI_RC_SUCCESS) {
	fprintf(stdout,
		"[%s] Rank: %4i GNI_CqDestroy     source ERROR status: %d\n",
		uts_info.nodename, node.world_rank, status);
    } else if (v_option > 1) {
	fprintf(stdout, "[%s] Rank: %4i GNI_CqDestroy     source\n",
		uts_info.nodename, node.world_rank);
    }

EXIT_DOMAIN:

    /*
     * Clean up the communication domain handle.
     */

    status = GNI_CdmDestroy(node.cdm_handle);
    if (status != GNI_RC_SUCCESS) {
	fprintf(stdout,
		"[%s] Rank: %4i GNI_CdmDestroy    ERROR status: %d\n",
		uts_info.nodename, node.world_rank, status);
    } else if (v_option > 1) {
	fprintf(stdout, "[%s] Rank: %4i GNI_CdmDestroy\n",
		uts_info.nodename, node.world_rank);
    }

EXIT_TEST:

    /*
     * Free allocated memory.
     */

    free(rdma_data_desc);

    /*
     * Clean up the PMI information.
     */

    PMI_Finalize();

    return rc;
}
