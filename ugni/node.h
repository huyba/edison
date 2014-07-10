#ifndef NODE_H
#define NODE_H

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <sys/utsname.h>
#include <sched.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <malloc.h>
#include <sched.h>
#include <errno.h>

#include "gni_pub.h"
#include "pmi.h"

#include "node_util.h"

class Node {
    public:
	int world_rank;
	int world_size;
	int nid;
	pmi_mesh_coord_t coord;
	//rca_mesh_coord_t coord;
	gni_cdm_handle_t cdm_handle;
	gni_nic_handle_t nic_handle;
	int modes;
	gni_cq_handle_t cq_handle;
	gni_cq_handle_t destination_cq_handle;
	gni_ep_handle_t *endpoint_handles_array;
	mdh_addr_t      my_memory_handle;
	mdh_addr_t *remote_memory_handle_array;
	gni_mem_handle_t send_mem_handle;
	gni_mem_handle_t recv_mem_handle;
	unsigned int *all_nic_addresses;
	struct utsname uts_info;

    public:
	void uGNI_getTopoInfo();
	void uGNI_init();
	void uGNI_createBasicCQ(int, int);
	void uGNI_regAndExchangeMem(void *, int, void *, int);
        void uGNI_put();
	void uGNI_createAndBindEndpoints();
	void uGNI_waitSendDone(int dest_rankId, gni_cq_handle_t cq_handle);
	void uGNI_waitAllSendDone(int dest_rankID, gni_cq_handle_t cq_handle, int num_req);
	void uGNI_waitRecvDone(int source_rankId, gni_cq_handle_t cq_handle);
	void uGNI_waitAllRecvDone(int source_rankId, gni_cq_handle_t cq_handle, int num_req);
	int uGNI_get_cq_event(gni_cq_handle_t cq_handle, unsigned int source_cq, unsigned int retry, gni_cq_entry_t *next_event);
	void uGNI_printInfo();
	void uGNI_finalize();
};

#endif
