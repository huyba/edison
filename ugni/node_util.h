#ifndef NODE_UTIL_H
#define NODE_UTIL_H

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

#include <rca_lib.h>

#define MAXIMUM_CQ_RETRY_COUNT 1000000

typedef struct {
        gni_mem_handle_t mdh;
        uint64_t        addr;
} mdh_addr_t;

/*
 * get_ptag will get the ptag value associated with this process.
 *
 * Returns: the ptag value.
 */
static uint8_t get_ptag(void)
{
    char           *p_ptr;
    uint8_t         ptag;
    char           *token;

    p_ptr = getenv("PMI_GNI_PTAG");
    assert(p_ptr != NULL);

    token = strtok(p_ptr, ":");
    ptag = (uint8_t) atoi(token);

    return ptag;
}

/*
 * get_cookie will get the cookie value associated with this process.
 *
 * Returns: the cookie value.
 */
static uint32_t get_cookie(void)
{
    uint32_t        cookie;
    char           *p_ptr;
    char           *token;

    p_ptr = getenv("PMI_GNI_COOKIE");
    assert(p_ptr != NULL);

    token = strtok(p_ptr, ":");
    cookie = (uint32_t) atoi(token);

    return cookie;
}

/*
 * allgather gather the requested information from all of the ranks.
 */

static void allgather(void *in, void *out, int len)
{
    static int      already_called = 0;
    int             i;
    static int     *ivec_ptr = NULL;
    static int      job_size = 0;
    int             my_rank;
    char           *out_ptr;
    int             rc;
    char           *tmp_buf;

    if (!already_called) {
	rc = PMI_Get_size(&job_size);
	assert(rc == PMI_SUCCESS);

	rc = PMI_Get_rank(&my_rank);
	assert(rc == PMI_SUCCESS);

	ivec_ptr = (int *) malloc(sizeof(int) * job_size);
	assert(ivec_ptr != NULL);


	rc = PMI_Allgather(&my_rank, ivec_ptr, sizeof(int));
	assert(rc == PMI_SUCCESS);

	already_called = 1;
    }

    tmp_buf = (char *) malloc(job_size * len);
    assert(tmp_buf);

    rc = PMI_Allgather(in, tmp_buf, len);
    assert(rc == PMI_SUCCESS);

    out_ptr = (char *)out;

    for (i = 0; i < job_size; i++) {
	memcpy(&out_ptr[len * ivec_ptr[i]], &tmp_buf[i * len], len);
    }

    free(tmp_buf);
}

/*
 * get_gni_nic_address get the nic address for the specified device.
 *
 *   Returns: the nic address for the specified device.
 */

static unsigned int get_gni_nic_address(int device_id)
{
    int             alps_address = -1;
    int             alps_dev_id = -1;
    unsigned int    address,
		    cpu_id;
    gni_return_t    status;
    int             i;
    char           *token,
		   *p_ptr;

    p_ptr = getenv("PMI_GNI_DEV_ID");
    if (!p_ptr) {

	/*
	 * Get the nic address for the specified device.
	 */

	status = GNI_CdmGetNicAddress(device_id, &address, &cpu_id);
	if (status != GNI_RC_SUCCESS) {
	    fprintf(stderr, "GNI_CdmGetNicAddress ERROR status: %d\n", status);
	    abort();
	}
    } else {

	/*
	 * Get the ALPS device id from the PMI_GNI_DEV_ID environment
	 * variable.
	 */

	while ((token = strtok(p_ptr, ":")) != NULL) {
	    alps_dev_id = atoi(token);
	    if (alps_dev_id == device_id) {
		break;
	    }

	    p_ptr = NULL;
	}

	assert(alps_dev_id != -1);

	p_ptr = getenv("PMI_GNI_LOC_ADDR");
	assert(p_ptr != NULL);

	i = 0;

	/*
	 * Get the nic address for the ALPS device.
	 */

	while ((token = strtok(p_ptr, ":")) != NULL) {
	    if (i == alps_dev_id) {
		alps_address = atoi(token);
		break;
	    }

	    p_ptr = NULL;
	    ++i;
	}

	assert(alps_address != -1);
	address = alps_address;
    }

    return address;
}

/*
 * gather_nic_addresses gather all of the nic addresses for all of the
 *                      other ranks.
 *
 *   Returns: an array of addresses for all of the nics from all of the
 *            other ranks.
 */
static void    *gather_nic_addresses(void)
{
    size_t          addr_len;
    unsigned int   *all_addrs;
    unsigned int    local_addr;
    int             rc;
    int             size;

    /*
     * Get the size of the process group.
     */

    rc = PMI_Get_size(&size);
    assert(rc == PMI_SUCCESS);

    /*
     * Assuming a single gemini device.
     */

    local_addr = get_gni_nic_address(0);

    addr_len = sizeof(unsigned int);

    /*
     * Allocate a buffer to hold the nic address from all of the other
     * ranks.
     */

    all_addrs = (unsigned int *) malloc(addr_len * size);
    assert(all_addrs != NULL);

    /*
     * Get the nic addresses from all of the other ranks.
     */

    allgather(&local_addr, all_addrs, sizeof(int));

    return (void *) all_addrs;
}

static void postRdmaStatus(gni_return_t ret) 
{
    if(ret == GNI_RC_SUCCESS)
	printf("Message returned: GNI_RC_SUCCESS\n");
    if(ret == GNI_RC_INVALID_PARAM)
        printf("Error returned: GNI_RC_INVALID_PARAM\n");
    if(ret == GNI_RC_ALIGNMENT_ERROR)
        printf("Error returned: GNI_RC_ALIGNMENT_ERROR\n");
    if(ret == GNI_RC_ERROR_RESOURCE)
	printf("Error returned: GNI_RC_ERROR_RESOURCE\n");
    if(ret == GNI_RC_ERROR_NOMEM)
	printf("Error returned: GNI_RC_ERROR_NOMEM\n");
    if(ret == GNI_RC_PERMISSION_ERROR)
	printf("Error returned: GNI_RC_PERMISSION_ERROR\n");
}

#endif
