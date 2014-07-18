#include <stdio.h>
#include <sys/time.h>

#include "node.h"

#define NUMBER_OF_TRANSFERS      10
#define SEND_DATA                0xdddd000000000000
#define TRANSFER_LENGTH          1024
#define TRANSFER_LENGTH_IN_BYTES ((TRANSFER_LENGTH)*sizeof(uint64_t))

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

    int number_of_cq_entries  = iters;
    int number_of_dest_cq_entries = 1;

    Node node;

    node.uGNI_init();
    node.uGNI_createBasicCQ(number_of_cq_entries, number_of_dest_cq_entries);
    node.uGNI_createAndBindEndpoints();

    int buf_size = transfer_length_in_bytes*iters;

    uint64_t *send_buf, *recv_buf;

    int rc = posix_memalign((void **) &send_buf, 64, buf_size);
    assert(rc == 0);

    rc = posix_memalign((void **) &recv_buf, 64, buf_size);
    assert(rc == 0);

    /*Initialize the buffer*/
    memset(send_buf, 9, buf_size);

    /*Initialize the buffer to all zeros.*/
    memset(recv_buf, 0, buf_size);

    node.uGNI_regAndExchangeMem(send_buf, buf_size, recv_buf, buf_size);

    /* Allocate the rdma_data_desc array. */
    gni_post_descriptor_t *rdma_data_desc = (gni_post_descriptor_t *) calloc(iters, sizeof(gni_post_descriptor_t));
    assert(rdma_data_desc != NULL);

    /*
     * Determine who we are going to send our data to and
     * who we are going to receive data from.
     */

    struct timeval t1, t2;

    int send_to = (node.world_rank + 1) % node.world_size;
    int receive_from = (node.world_size + node.world_rank - 1) % node.world_size;

    int i = 0, j = 0;

    gni_return_t status;

    if(node.world_rank == 0) {
	// Start measuring data
	if(node.world_rank == 0) {
	    printf("Size\tBandwidth\tLatency\n");
	    gettimeofday(&t1, NULL);
	}
	for (i = 0; i < iters; i++) {
	    for (j = 0; j < transfer_length; j++) {
		send_buf[j + (i * transfer_length)] = iters+1;
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

	    int create_destination_cq = 1;
	    rdma_data_desc[i].type = GNI_POST_RDMA_PUT;
	    if (create_destination_cq != 0) {
		rdma_data_desc[i].cq_mode = GNI_CQMODE_GLOBAL_EVENT | GNI_CQMODE_REMOTE_EVENT;
	    } else {
		rdma_data_desc[i].cq_mode = GNI_CQMODE_GLOBAL_EVENT;
	    }
	    rdma_data_desc[i].dlvr_mode = GNI_DLVMODE_PERFORMANCE;
	    rdma_data_desc[i].local_addr = (uint64_t) send_buf;
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
		fprintf(stdout, "[%s] Rank: %4i GNI_PostRdma data ERROR status: %d\n", node.uts_info.nodename, node.world_rank, status);
		postRdmaStatus(status);
		continue;
	    }
	}   /* end of for loop for transfers */

	//Check to make sure that all sends are done.
	node.uGNI_waitAllSendDone(send_to, node.cq_handle, iters);

	if(node.world_rank == 0) {
	    gettimeofday(&t2, NULL);
	    double latency = ((t2.tv_sec * 1000000 + t2.tv_usec) - (t1.tv_sec * 1000000 + t1.tv_usec))*1.0/iters;
	    double bandwidth = transfer_length_in_bytes*1000000.0/(latency*1024*1024);
	    printf("%d \t %8.6f \t %8.4f\n", transfer_length_in_bytes, bandwidth, latency);
	}
    }

    if(node.world_rank == 1) {
	//Check to get all data received
	node.uGNI_waitAllRecvDone(receive_from, node.destination_cq_handle, iters);
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
     *     recv_mem_handle is the handle for this memory region.
     */

    status = GNI_MemDeregister(node.nic_handle, &node.recv_mem_handle);
    if (status != GNI_RC_SUCCESS) {
	fprintf(stdout,
		"[%s] Rank: %4i GNI_MemDeregister recv_buf ERROR status: %d\n",
		node.uts_info.nodename, node.world_rank, status);
	//INCREMENT_ABORTED;
    } else {
	free(recv_buf);
    }

EXIT_MEMORY_SOURCE:

    /*
     * Deregister the memory associated for the send buffer with the NIC.
     *     node.nic_handle is our NIC handle.
     *     node.send_mem_handle is the handle for this memory region.
     */

    status = GNI_MemDeregister(node.nic_handle, &node.send_mem_handle);
    if (status != GNI_RC_SUCCESS) {
	fprintf(stdout,
		"[%s] Rank: %4i GNI_MemDeregister send_buf ERROR status: %d\n",
		node.uts_info.nodename, node.world_rank, status);
	//INCREMENT_ABORTED;
    } else {
	free(send_buf);
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
