// This is code for a node to communicate with other nodes using uGNI libraries
// @author: Huy Bui
// @date: July 1st, 2014

#include "node.h"


void Node::uGNI_printInfo() {
    printf("rank %d PMI_Get_nid gives nid %d \n", world_rank, nid);
    printf("rank %d rca_get_meshcoord returns (%2u,%2u,%2u)\n",
	    world_rank, coord.mesh_x, coord.mesh_y, coord.mesh_z);   
}

void Node::uGNI_getTopoInfo() {
    int rc = PMI_Get_nid(world_rank, &nid);
    if (rc!=PMI_SUCCESS)
	PMI_Abort(rc,"PMI_Get_nid failed");

    PMI_Get_meshcoord((uint16_t) nid, &coord);
    //rca_get_meshcoord( (uint16_t) nid, &coord);
}

void Node::uGNI_init() {
    int device_id = 0;
    int             rc;
    unsigned int    local_address;
    int             first_spawned;

    // Get job attributes from PMI.
    rc = PMI_Init(&first_spawned);
    assert(rc == PMI_SUCCESS);

    rc = PMI_Get_size(&world_size);
    assert(rc == PMI_SUCCESS);

    rc = PMI_Get_rank(&world_rank);
    assert(rc == PMI_SUCCESS);

    // Get job attributes from PMI.
    uint8_t ptag = get_ptag();
    int cookie = get_cookie();

    // Create a handle to the communication domain. GNI_CDM_MODE_BTE_SINGLE_CHANNEL was used for modes variable.
    gni_return_t status = GNI_CdmCreate(world_rank, ptag, cookie, modes, &cdm_handle);

    if (status != GNI_RC_SUCCESS) {
	fprintf(stdout, "[%s] Rank: %4i GNI_CdmCreate ERROR status: %d\n", uts_info.nodename, world_rank, status);
    }

    // Attach the communication domain handle to the NIC.
    status = GNI_CdmAttach(cdm_handle, device_id, &local_address, &nic_handle);
    if (status != GNI_RC_SUCCESS) {
	fprintf(stdout, "[%s] Rank: %4i GNI_CdmAttach     ERROR status: %d\n", uts_info.nodename, world_rank, status);
    }

    /* Allocate the endpoint handles array. */
    endpoint_handles_array = (gni_ep_handle_t *) calloc(world_size, sizeof(gni_ep_handle_t));
    assert(endpoint_handles_array != NULL);


    /* Get all nic address from all of the other ranks */
    unsigned int local_addr = get_gni_nic_address(0);

    /* Allocate a buffer to hold the nic address from all of the other  ranks. */
    all_nic_addresses = (unsigned int *) malloc(sizeof(unsigned int) * world_size);
    assert(all_nic_addresses != NULL);

    /* Get the nic addresses from all of the other ranks.*/
    allgather(&local_addr, all_nic_addresses, sizeof(int));

    /*Allocate a buffer to contain all of the remote memory handle's */
    remote_memory_handle_array = (mdh_addr_t *) calloc(world_size, sizeof(mdh_addr_t));
    assert(remote_memory_handle_array);
}

void Node::uGNI_createBasicCQ(int number_of_cq_entries, int number_of_dest_cq_entries) {
    gni_return_t status = GNI_CqCreate(nic_handle, number_of_cq_entries, 0, GNI_CQ_NOBLOCK, NULL, NULL, &cq_handle);
    if (status != GNI_RC_SUCCESS) {
	fprintf(stdout, "[%s] Rank: %4i GNI_CqCreate source ERROR status: %d\n", uts_info.nodename, world_rank, status);
    }

    status = GNI_CqCreate(nic_handle, number_of_dest_cq_entries, 0, GNI_CQ_NOBLOCK, NULL, NULL, &destination_cq_handle);
    if (status != GNI_RC_SUCCESS) {
	fprintf(stdout,
		"[%s] Rank: %4i GNI_CqCreate      destination ERROR status: %d\n",
		uts_info.nodename, world_rank, status);
    }
}
void Node::uGNI_regAndExchangeMem(void *send_buf, int send_length, void *recv_buf, int recv_length) {
    gni_return_t status = GNI_MemRegister(nic_handle, (uint64_t)send_buf, send_length, NULL,
	    GNI_MEM_READWRITE, -1,
	    &send_mem_handle);
    if (status != GNI_RC_SUCCESS) {
	fprintf(stdout,
		"[%s] Rank: %4i GNI_MemRegister  send_buffer ERROR status: %d\n",
		uts_info.nodename, world_rank, status);
    }

    status = GNI_MemRegister(nic_handle, (uint64_t)recv_buf,
	    recv_length, destination_cq_handle,
	    GNI_MEM_READWRITE,
	    -1, &recv_mem_handle);
    if (status != GNI_RC_SUCCESS) {
	fprintf(stdout,
		"[%s] Rank: %4i GNI_MemRegister   receive_buffer ERROR status: %d\n",
		uts_info.nodename, world_rank, status);
    }
    my_memory_handle.addr = (uint64_t)recv_buf;
    my_memory_handle.mdh = recv_mem_handle;

    /*
     * Gather up all of the remote memory handle's.
     * This also acts as a barrier to get all of the ranks to sync up.
     */

    allgather(&my_memory_handle, remote_memory_handle_array, sizeof(mdh_addr_t));
}

void Node::uGNI_put() {

}

void Node::uGNI_finalize() {

    /*
     * Remove the endpoints to all of the ranks.
     *
     * Note: if there are outstanding events in the completion queue,
     *       the endpoint can not be unbound.
     */
    int i;
    int v_option = 0;
    gni_return_t status = GNI_RC_SUCCESS;

    for (i = 0; i < world_size; i++) {
	if (i == world_rank) {
	    continue;
	}

	if (endpoint_handles_array[i] == 0) {

	    /*
	     * This endpoint does not exist.
	     */

	    continue;
	}

	/*
	 * Unbind the remote address from the endpoint handler.
	 *     endpoint_handles_array is the endpoint handle that is being unbound
	 */

	status = GNI_EpUnbind(endpoint_handles_array[i]);
	if (status != GNI_RC_SUCCESS) {
	    fprintf(stdout,
		    "[%s] Rank: %4i GNI_EpUnbind      ERROR remote rank: %4i status: %d\n",
		    uts_info.nodename, world_rank, i, status);
	    continue;
	}

	if (v_option > 1) {
	    fprintf(stdout,
		    "[%s] Rank: %4i GNI_EpUnbind      remote rank: %4i EP:  %p\n",
		    uts_info.nodename, world_rank, i,
		    endpoint_handles_array[i]);
	}

	/*
	 * You must do an EpDestroy for each endpoint pair.
	 *
	 * Destroy the logical endpoint for each rank.
	 *     endpoint_handles_array is the endpoint handle that is being
	 *         destroyed.
	 */

	status = GNI_EpDestroy(endpoint_handles_array[i]);
	if (status != GNI_RC_SUCCESS) {
	    fprintf(stdout,
		    "[%s] Rank: %4i GNI_EpDestroy     ERROR remote rank: %4i status: %d\n",
		    uts_info.nodename, world_rank, i, status);
	    continue;
	}

	if (v_option > 1) {
	    fprintf(stdout,
		    "[%s] Rank: %4i GNI_EpDestroy     remote rank: %4i EP:  %p\n",
		    uts_info.nodename, world_rank, i,
		    endpoint_handles_array[i]);
	}
    }


    /*
     * Free allocated memory.
     */

    free (endpoint_handles_array);

    if (destination_cq_handle != NULL) {
	/*
	 * Destroy the destination completion queue.
	 *     cq_handle is the handle that is being destroyed.
	 */

	status = GNI_CqDestroy(destination_cq_handle);
	if (status != GNI_RC_SUCCESS) {
	    fprintf(stdout,
		    "[%s] Rank: %4i GNI_CqDestroy     destination ERROR status: %d\n",
		    uts_info.nodename, world_rank, status);
	} else if (v_option > 1) {
	    fprintf(stdout,
		    "[%s] Rank: %4i GNI_CqDestroy     destination\n",
		    uts_info.nodename, world_rank);
	}
    }

    /*
     * Destroy the completion queue.
     *     cq_handle is the handle that is being destroyed.
     */

    status = GNI_CqDestroy(cq_handle);
    if (status != GNI_RC_SUCCESS) {
	fprintf(stdout,
		"[%s] Rank: %4i GNI_CqDestroy     source ERROR status: %d\n",
		uts_info.nodename, world_rank, status);
    } else if (v_option > 1) {
	fprintf(stdout, "[%s] Rank: %4i GNI_CqDestroy     source\n",
		uts_info.nodename, world_rank);
    }

    /*
     * Clean up the communication domain handle.
     */

    status = GNI_CdmDestroy(cdm_handle);
    if (status != GNI_RC_SUCCESS) {
	fprintf(stdout,
		"[%s] Rank: %4i GNI_CdmDestroy    ERROR status: %d\n",
		uts_info.nodename, world_rank, status);
    } else if (v_option > 1) {
	fprintf(stdout, "[%s] Rank: %4i GNI_CdmDestroy\n",
		uts_info.nodename, world_rank);
    }

}

void Node::uGNI_createAndBindEndpoints() {
    register int i;
    unsigned int    remote_address;

    // Get all of the NIC address for all of the ranks.
    all_nic_addresses = (unsigned int *) gather_nic_addresses();

    // Create logical endpoint and bind it to a corresponding 
    for (i = 0; i < world_size; i++) {
	if (i == world_rank) {
	    continue;
	}

	// Create the logical endpoints
	gni_return_t status = GNI_EpCreate(nic_handle, cq_handle, &endpoint_handles_array[i]);
	if (status != GNI_RC_SUCCESS) {
	    fprintf(stdout, "[%s] Rank: %4i GNI_EpCreate ERROR status: %d\n", uts_info.nodename, world_rank, status);
	}

	// Get the remote address to bind to.
	remote_address = all_nic_addresses[i];

	// Bind the remote address to the endpoint handler.
	status = GNI_EpBind(endpoint_handles_array[i], remote_address, i);
	if (status != GNI_RC_SUCCESS) {
	    fprintf(stdout, "[%s] Rank: %4i GNI_EpBind ERROR status: %d\n", uts_info.nodename, world_rank, status);
	}
    }
}

/*
 * Check the completion queue to verify that the message request has
 * been sent.  The source completion queue needs to be checked and
 * events to be removed so that it does not become full and cause
 * succeeding calls to PostRdma to fail.
 */
void Node::uGNI_waitSendDone(int send_to, gni_cq_handle_t cq_handle) {
    gni_cq_entry_t  current_event;
    int             event_id;
    gni_post_descriptor_t *event_post_desc_ptr;
    int 		rc;

    rc = uGNI_get_cq_event(cq_handle, 1, 1, &current_event);

    //An event was received. Complete the event, which removes the current event's post descriptor from the event queue.
    if (rc == 0) {
	gni_return_t status = GNI_GetCompleted(cq_handle, current_event, &event_post_desc_ptr);
	if (status != GNI_RC_SUCCESS) {
	    fprintf(stdout,	"[%s] Rank: %4i GNI_GetCompleted  data ERROR status: %d\n", uts_info.nodename, world_rank, status);
	} else {
	    //Validate the current event's instance id with the expected id.
	    event_id = GNI_CQ_GET_INST_ID(current_event);
	    if (event_id != send_to) {
		// The event's inst_id was not the expected inst_id value.
		fprintf(stdout, "[%s] Rank: %4i CQ Event data ERROR received inst_id: %d, expected inst_id: %d in event_data\n", uts_info.nodename, world_rank, event_id, send_to);
	    }
	}
    }
}

void Node::uGNI_waitAllSendDone(int sent_to, gni_cq_handle_t cq_handle, int queue_size) {
    int i = 0;
    for(i = 0; i < queue_size; i++)
	uGNI_waitSendDone(sent_to, cq_handle);
}

/*
 * Check the completion queue to verify that the data has
 * been received.  The destination completion queue needs to be
 * checked and events to be removed so that it does not become full
 * and cause succeeding events to be lost.
 */
void Node::uGNI_waitRecvDone(int receive_from, gni_cq_handle_t cq_handle) {
    gni_cq_entry_t  current_event;
    int             event_id;
    int 		rc;

    rc = uGNI_get_cq_event(cq_handle, 0, 1, &current_event);

    //An event was received.Validate the current event's instance id with the expected id.
    if (rc == 0) {
	event_id = GNI_CQ_GET_INST_ID(current_event);
	if (event_id != receive_from) {
	    // The event's inst_id was not the expected inst_id value.
	    fprintf(stdout, "[%s] Rank: %4i CQ Event destination ERROR received inst_id: %d, expected inst_id: %d in event_data\n", uts_info.nodename, world_rank, event_id, receive_from);
	}
    } else {		 
	//An error occurred while receiving the event.
	fprintf(stderr,"[%s] Rank: %4i CQ Event ERROR destination queue did not receieve data event\n", uts_info.nodename, world_rank);
    }
}

void Node::uGNI_waitAllRecvDone(int recv_from, gni_cq_handle_t cq_handle, int queue_size) {
    int i = 0;
    for(i = 0; i < queue_size; i++)
	uGNI_waitRecvDone(recv_from, cq_handle);
}

int Node::uGNI_get_cq_event(gni_cq_handle_t cq_handle, unsigned int source_cq, unsigned int retry, gni_cq_entry_t *next_event){
    gni_cq_entry_t  event_data = 0;
    uint64_t        event_type;
    gni_return_t    status = GNI_RC_SUCCESS;
    int             wait_count = 0;

    status = GNI_RC_NOT_DONE;
    int v_option = 0;
    while (status == GNI_RC_NOT_DONE) {

	/*
	 * Get the next event from the specified completion queue handle.
	 */

	status = GNI_CqGetEvent(cq_handle, &event_data);
	if (status == GNI_RC_SUCCESS) {
	    *next_event = event_data;

	    /*
	     * Processed event succesfully.
	     */

	    if (v_option > 1) {
		event_type = GNI_CQ_GET_TYPE(event_data);

		if (event_type == GNI_CQ_EVENT_TYPE_POST) {
		    if (source_cq == 1) {
			fprintf(stderr,
				"[%s] Rank: %4i GNI_CqGetEvent    source      type: POST(%lu) inst_id: %lu tid: %lu event: 0x%16.16lx\n",
				uts_info.nodename, world_rank,
				event_type,
				GNI_CQ_GET_INST_ID(event_data),
				GNI_CQ_GET_TID(event_data),
				event_data);
		    } else {
			fprintf(stdout,
				"[%s] Rank: %4i GNI_CqGetEvent    destination type: POST(%lu) inst_id: %lu event: 0x%16.16lx\n",
				uts_info.nodename, world_rank,
				event_type,
				GNI_CQ_GET_INST_ID(event_data),
				event_data);
		    }
		} else if (event_type == GNI_CQ_EVENT_TYPE_SMSG) {
		    if (source_cq == 1) {
			fprintf(stdout,
				"[%s] Rank: %4i GNI_CqGetEvent    source      type: SMSG(%lu) msg_id: 0x%8.8x event: 0x%16.16lx\n",
				uts_info.nodename, world_rank,
				event_type,
				(unsigned int) GNI_CQ_GET_MSG_ID(event_data),
				event_data);
		    } else {
			fprintf(stdout,
				"[%s] Rank: %4i GNI_CqGetEvent    destination type: SMSG(%lu) data: 0x%16.16lx event: 0x%16.16lx\n",
				uts_info.nodename, world_rank,
				event_type,
				GNI_CQ_GET_DATA(event_data),
				event_data);
		    }
		} else if (event_type == GNI_CQ_EVENT_TYPE_MSGQ) {
		    if (source_cq == 1) {
			fprintf(stdout,
				"[%s] Rank: %4i GNI_CqGetEvent    source      type: MSGQ(%lu) msg_id: 0x%8.8x event: 0x%16.16lx\n",
				uts_info.nodename, world_rank,
				event_type,
				(unsigned int) GNI_CQ_GET_MSG_ID(event_data),
				event_data);
		    } else {
			fprintf(stdout,
				"[%s] Rank: %4i GNI_CqGetEvent    destination type: MSGQ(%lu) data: 0x%16.16lx event: 0x%16.16lx\n",
				uts_info.nodename, world_rank,
				event_type,
				GNI_CQ_GET_DATA(event_data),
				event_data);
		    }
		} else {
		    if (source_cq == 1) {
			fprintf(stdout,
				"[%s] Rank: %4i GNI_CqGetEvent    source      type: %lu inst_id: %lu event: 0x%16.16lx\n",
				uts_info.nodename, world_rank,
				event_type,
				GNI_CQ_GET_DATA(event_data),
				event_data);
		    } else {
			fprintf(stdout,
				"[%s] Rank: %4i GNI_CqGetEvent    destination type: %lu data: 0x%16.16lx event: 0x%16.16lx\n",
				uts_info.nodename, world_rank,
				event_type,
				GNI_CQ_GET_DATA(event_data),
				event_data);
		    }
		}
	    }

	    return 0;
	} else if (status != GNI_RC_NOT_DONE) {
	    int error_code = 1;
	    /*
	     * An error occurred getting the event.
	     */

	    char           *cqErrorStr;
	    char           *cqOverrunErrorStr = "";
	    gni_return_t    tmp_status = GNI_RC_SUCCESS;
#ifdef CRAY_CONFIG_GHAL_ARIES
	    uint32_t        status_code;

	    status_code = GNI_CQ_GET_STATUS(event_data);
	    if (status_code == A_STATUS_AT_PROTECTION_ERR) {
		return 1;
	    }
#endif

	    /*
	     * Did the event queue overrun condition occurred?
	     * This means that all of the event queue entries were used up
	     * and another event occurred, i.e. there was no entry available
	     * to put the new event into.
	     */

	    if (GNI_CQ_OVERRUN(event_data)) {
		cqOverrunErrorStr = "CQ_OVERRUN detected ";
		error_code = 2;

		if (v_option > 2) {
		    fprintf(stdout,
			    "[%s] Rank: %4i ERROR CQ_OVERRUN detected\n",
			    uts_info.nodename, world_rank);
		}
	    }

	    cqErrorStr = (char *) malloc(256);
	    if (cqErrorStr != NULL) {

		/*
		 * Print a user understandable error message.
		 */

		tmp_status = GNI_CqErrorStr(event_data, cqErrorStr, 256);
		if (tmp_status == GNI_RC_SUCCESS) {
		    fprintf(stdout,
			    "[%s] Rank: %4i GNI_CqGetEvent    ERROR %sstatus: %d inst_id: %lu event: 0x%16.16lx GNI_CqErrorStr: %s\n",
			    uts_info.nodename, world_rank, cqOverrunErrorStr, status,
			    GNI_CQ_GET_INST_ID(event_data),
			    event_data,
			    cqErrorStr);
		} else {

		    /*
		     * Print the error number.
		     */

		    fprintf(stdout,
			    "[%s] Rank: %4i GNI_CqGetEvent    ERROR %sstatus: %d inst_id: %lu event: 0x%16.16lx\n",
			    uts_info.nodename, world_rank, cqOverrunErrorStr, status,
			    GNI_CQ_GET_INST_ID(event_data),
			    event_data);
		}

		free(cqErrorStr);
	    } else {

		/*
		 * Print the error number.
		 */

		fprintf(stdout,
			"[%s] Rank: %4i GNI_CqGetEvent    ERROR %sstatus: %d inst_id: %lu event: 0x%16.16lx\n",
			uts_info.nodename, world_rank, cqOverrunErrorStr, status,
			GNI_CQ_GET_INST_ID(event_data),
			event_data);
	    }
	    return error_code;
	} else if (retry == 0) {
	    return 3;
	} else {

	    /*
	     * An event has not been received yet.
	     */

	    wait_count++;

	    if (wait_count >= MAXIMUM_CQ_RETRY_COUNT) {
		/*
		 * This prevents an indefinite retry, which could hang the
		 * application.
		 */

		fprintf(stderr,
			"[%s] Rank: %4i GNI_CqGetEvent    ERROR no event was received status: %d retry count: %d\n",
			uts_info.nodename, world_rank, status, wait_count);
		return 3;
	    }

	    /*
	     * Release the cpu to allow the event to be received.
	     * This is basically a sleep, if other processes need to do some work.
	     */

	    if ((wait_count % (MAXIMUM_CQ_RETRY_COUNT / 10)) == 0) {
		/*
		 * Sometimes it takes a little longer for
		 * the datagram to arrive.
		 */

		usleep(50);
	    } else {
		sched_yield();
	    }
	}
    }

    return 1;
}
