#include "gni_pub.h"
#include "pmi.h"
#include <rca_lib.h>

#define MAXIMUM_CQ_RETRY_COUNT 1000000

gni_return_t    status = GNI_RC_SUCCESS;

int             v_option = 0;
int             rc;

/*
 * get_cq_event will process events from the completion queue.
 *
 *   cq_handle is the completion queue handle.
 *   uts_info contains the node name.
 *   rank_id is the rank of this process.
 *   source_cq determines if the CQ is a source or a
 *       destination completion queue. 
 *   retry determines if GNI_CqGetEvent should be called multiple
 *       times or only once to get an event.
 *
 *   Returns:  gni_cq_entry_t for success
 *             0 on success
 *             1 on an error
 *             2 on an OVERRUN error
 *             3 on no event found error
 */

	static int
get_cq_event(gni_cq_handle_t cq_handle, struct utsname uts_info,
		int rank_id, unsigned int source_cq, unsigned int retry,
		gni_cq_entry_t *next_event)
{
	gni_cq_entry_t  event_data = 0;
	uint64_t        event_type;
	gni_return_t    status = GNI_RC_SUCCESS;
	int             wait_count = 0;

	status = GNI_RC_NOT_DONE;
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
								uts_info.nodename, rank_id,
								event_type,
								GNI_CQ_GET_INST_ID(event_data),
								GNI_CQ_GET_TID(event_data),
								event_data);
					} else {
						fprintf(stdout,
								"[%s] Rank: %4i GNI_CqGetEvent    destination type: POST(%lu) inst_id: %lu event: 0x%16.16lx\n",
								uts_info.nodename, rank_id,
								event_type,
								GNI_CQ_GET_INST_ID(event_data),
								event_data);
					}
				} else if (event_type == GNI_CQ_EVENT_TYPE_SMSG) {
					if (source_cq == 1) {
						fprintf(stdout,
								"[%s] Rank: %4i GNI_CqGetEvent    source      type: SMSG(%lu) msg_id: 0x%8.8x event: 0x%16.16lx\n",
								uts_info.nodename, rank_id,
								event_type,
								(unsigned int) GNI_CQ_GET_MSG_ID(event_data),
								event_data);
					} else {
						fprintf(stdout,
								"[%s] Rank: %4i GNI_CqGetEvent    destination type: SMSG(%lu) data: 0x%16.16lx event: 0x%16.16lx\n",
								uts_info.nodename, rank_id,
								event_type,
								GNI_CQ_GET_DATA(event_data),
								event_data);
					}
				} else if (event_type == GNI_CQ_EVENT_TYPE_MSGQ) {
					if (source_cq == 1) {
						fprintf(stdout,
								"[%s] Rank: %4i GNI_CqGetEvent    source      type: MSGQ(%lu) msg_id: 0x%8.8x event: 0x%16.16lx\n",
								uts_info.nodename, rank_id,
								event_type,
								(unsigned int) GNI_CQ_GET_MSG_ID(event_data),
								event_data);
					} else {
						fprintf(stdout,
								"[%s] Rank: %4i GNI_CqGetEvent    destination type: MSGQ(%lu) data: 0x%16.16lx event: 0x%16.16lx\n",
								uts_info.nodename, rank_id,
								event_type,
								GNI_CQ_GET_DATA(event_data),
								event_data);
					}
				} else {
					if (source_cq == 1) {
						fprintf(stdout,
								"[%s] Rank: %4i GNI_CqGetEvent    source      type: %lu inst_id: %lu event: 0x%16.16lx\n",
								uts_info.nodename, rank_id,
								event_type,
								GNI_CQ_GET_DATA(event_data),
								event_data);
					} else {
						fprintf(stdout,
								"[%s] Rank: %4i GNI_CqGetEvent    destination type: %lu data: 0x%16.16lx event: 0x%16.16lx\n",
								uts_info.nodename, rank_id,
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
							uts_info.nodename, rank_id);
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
							uts_info.nodename, rank_id, cqOverrunErrorStr, status,
							GNI_CQ_GET_INST_ID(event_data),
							event_data,
							cqErrorStr);
				} else {

					/*
					 * Print the error number.
					 */

					fprintf(stdout,
							"[%s] Rank: %4i GNI_CqGetEvent    ERROR %sstatus: %d inst_id: %lu event: 0x%16.16lx\n",
							uts_info.nodename, rank_id, cqOverrunErrorStr, status,
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
						uts_info.nodename, rank_id, cqOverrunErrorStr, status,
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
						uts_info.nodename, rank_id, status, wait_count);
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

void topo_info(int rank){
        int nid;
        rc = PMI_Get_nid(rank, &nid);
        if (rc!=PMI_SUCCESS)
                PMI_Abort(rc,"PMI_Get_nid failed");
        printf("rank %d PMI_Get_nid gives nid %d \n", rank, nid);


        pmi_mesh_coord_t xyz;
        PMI_Get_meshcoord( (uint16_t) nid, &xyz);
        printf("rank %d rca_get_meshcoord returns (%2u,%2u,%2u)\n",
                        rank, xyz.mesh_x, xyz.mesh_y, xyz.mesh_z);
}

void uGNI_Init(int *rank_id, int *number_of_ranks, int modes, gni_cdm_handle_t *cdm_handle, gni_nic_handle_t *nic_handle) {
	int device_id = 0;
	int             rc;
	unsigned int    local_address;
	int             first_spawned;

	// Get job attributes from PMI.
	rc = PMI_Init(&first_spawned);
	assert(rc == PMI_SUCCESS);

	rc = PMI_Get_size(number_of_ranks);
	assert(rc == PMI_SUCCESS);

	rc = PMI_Get_rank(rank_id);
	assert(rc == PMI_SUCCESS);

	// Get job attributes from PMI.
	uint8_t ptag = get_ptag();
	int cookie = get_cookie();

	// Create a handle to the communication domain. GNI_CDM_MODE_BTE_SINGLE_CHANNEL was used for modes variable.
	status = GNI_CdmCreate(*rank_id, ptag, cookie, modes, cdm_handle);

	if (status != GNI_RC_SUCCESS) {
		fprintf(stdout, "[%s] Rank: %4i GNI_CdmCreate ERROR status: %d\n", uts_info.nodename, *rank_id, status);
	}

	// Attach the communication domain handle to the NIC.
	status = GNI_CdmAttach(*cdm_handle, device_id, &local_address, nic_handle);
	if (status != GNI_RC_SUCCESS) {
		fprintf(stdout, "[%s] Rank: %4i GNI_CdmAttach     ERROR status: %d\n", uts_info.nodename, *rank_id, status);
	}
}

void uGNI_CreateAndBindEndpoints(int rank_id, int number_of_ranks, gni_cq_handle_t cq_handle, gni_nic_handle_t nic_handle, gni_ep_handle_t *endpoint_handles_array) {
	register int i;
	unsigned int    remote_address;

	// Get all of the NIC address for all of the ranks.
	unsigned int *all_nic_addresses = (unsigned int *) gather_nic_addresses();

	// Create logical endpoint and bind it to a corresponding 
	for (i = 0; i < number_of_ranks; i++) {
		if (i == rank_id) {
			continue;
		}

		// Create the logical endpoints
		status = GNI_EpCreate(nic_handle, cq_handle, &endpoint_handles_array[i]);
		if (status != GNI_RC_SUCCESS) {
			fprintf(stdout, "[%s] Rank: %4i GNI_EpCreate ERROR status: %d\n", uts_info.nodename, rank_id, status);
		}

		// Get the remote address to bind to.
		remote_address = all_nic_addresses[i];

		// Bind the remote address to the endpoint handler.
		status = GNI_EpBind(endpoint_handles_array[i], remote_address, i);
		if (status != GNI_RC_SUCCESS) {
			fprintf(stdout, "[%s] Rank: %4i GNI_EpBind ERROR status: %d\n", uts_info.nodename, rank_id, status);
		}
	}
}

/*
 * Check the completion queue to verify that the message request has
 * been sent.  The source completion queue needs to be checked and
 * events to be removed so that it does not become full and cause
 * succeeding calls to PostRdma to fail.
 */
void uGNI_WaitSendDone(int rank_id, int send_to, gni_cq_handle_t cq_handle) {
	gni_cq_entry_t  current_event;
	int             event_id;
	gni_post_descriptor_t *event_post_desc_ptr;
	int 		rc;

	rc = get_cq_event(cq_handle, uts_info, rank_id, 1, 1, &current_event);

	//An event was received. Complete the event, which removes the current event's post descriptor from the event queue.
	if (rc == 0) {
		status = GNI_GetCompleted(cq_handle, current_event, &event_post_desc_ptr);
		if (status != GNI_RC_SUCCESS) {
			fprintf(stdout,	"[%s] Rank: %4i GNI_GetCompleted  data ERROR status: %d\n", uts_info.nodename, rank_id, status);
		} else {
			//Validate the current event's instance id with the expected id.
			event_id = GNI_CQ_GET_INST_ID(current_event);
			if (event_id != send_to) {
				// The event's inst_id was not the expected inst_id value.
				fprintf(stdout, "[%s] Rank: %4i CQ Event data ERROR received inst_id: %d, expected inst_id: %d in event_data\n", uts_info.nodename, rank_id, event_id, send_to);
			}
		}
	}
}

void uGNI_WaitAllSendDone(int rank_id, int sent_to, gni_cq_handle_t cq_handle, int queue_size) {
	int i = 0;
	for(i = 0; i < queue_size; i++)
		uGNI_WaitSendDone(rank_id, sent_to, cq_handle);
}

/*
 * Check the completion queue to verify that the data has
 * been received.  The destination completion queue needs to be
 * checked and events to be removed so that it does not become full
 * and cause succeeding events to be lost.
 */
void uGNI_WaitRecvDone(int rank_id, int receive_from, gni_cq_handle_t cq_handle) {
	gni_cq_entry_t  current_event;
	int             event_id;
	int 		rc;

	rc = get_cq_event(cq_handle, uts_info, rank_id, 0, 1, &current_event);

	//An event was received.Validate the current event's instance id with the expected id.
	if (rc == 0) {
		event_id = GNI_CQ_GET_INST_ID(current_event);
		if (event_id != receive_from) {
			// The event's inst_id was not the expected inst_id value.
			fprintf(stdout, "[%s] Rank: %4i CQ Event destination ERROR received inst_id: %d, expected inst_id: %d in event_data\n", uts_info.nodename, rank_id, event_id, receive_from);
		}
	} else {		 
		//An error occurred while receiving the event.
		fprintf(stderr,"[%s] Rank: %4i CQ Event ERROR destination queue did not receieve data event\n", uts_info.nodename, rank_id);
	}
}

void uGNI_WaitAllRecvDone(int rank_id, int recv_from, gni_cq_handle_t cq_handle, int queue_size) {
	int i = 0;
	for(i = 0; i < queue_size; i++)
		uGNI_WaitRecvDone(rank_id, recv_from, cq_handle);
}
