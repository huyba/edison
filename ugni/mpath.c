/*
 * Multiple paths data movement
 * @author: Huy Bui
 * @Date: July 24th, 2014
 */

#include <stdio.h>
#include<stdlib.h>

#include <mpi.h>

#include "node.h"

typedef struct {
    int world_rank;
    int coord_x;
    int coord_y;
    int coord_z;
    int nid;
} proc_t;

int main(int argv, char **argc) {

    MPI_Init(&argv, &argc);

    Node node;
    node.uGNI_init();    
    node.uGNI_getTopoInfo();

    proc_t *proc = (proc_t*)malloc(sizeof(proc_t));
    proc->world_rank = node.world_rank;
    proc->nid = node.nid;
    proc->coord_x = node.coord.mesh_x;
    proc->coord_y = node.coord.mesh_y;
    proc->coord_z = node.coord.mesh_z;

    proc_t *procs = (proc_t*)malloc(sizeof(proc_t)*node.world_size);
    MPI_Allgather(proc, 5, MPI_INT, procs, 5, MPI_INT, MPI_COMM_WORLD); 

    if(node.world_rank == 0) {
	for(int i = 0; i < node.world_size; i++) {
	    printf("Rank %d [%d %d %d %d]\n", procs[i].world_rank, procs[i].coord_x, procs[i].coord_y, procs[i].coord_z, procs[i].nid);
	}
    }

    proc_t *distinct_procs = (proc_t*)malloc(sizeof(proc_t)*node.world_size);
    int num_aries;

    distinct_procs[0].world_rank = procs[0].world_rank;
    distinct_procs[0].coord_x = procs[0].coord_x;
    distinct_procs[0].coord_y = procs[0].coord_y;
    distinct_procs[0].coord_z = procs[0].coord_z;
    distinct_procs[0].nid = procs[0].nid;
    num_aries = 1;
    bool existing = false;

    /*Collecting distinct aries, one process per aries can be used in this experiment*/
    for(int i = 1; i < node.world_size; i++) {
	existing = false;
	for(int j = 0; j < num_aries; j++){
	    if(distinct_procs[j].coord_x == procs[i].coord_x && 
		    distinct_procs[j].coord_y == procs[i].coord_y &&
		    distinct_procs[j].coord_z == procs[i].coord_z) {
		existing = true;
		break;
	    }
	}
	if(!existing) {
	    distinct_procs[num_aries].world_rank = procs[i].world_rank;
	    distinct_procs[num_aries].coord_x = procs[i].coord_x;
	    distinct_procs[num_aries].coord_y = procs[i].coord_y;
	    distinct_procs[num_aries].coord_z = procs[i].coord_z;
	    distinct_procs[num_aries].nid = procs[i].nid;
	    num_aries++;
	}
    }

    /*Print distinct aries*/
    if(node.world_rank == 0) {
	printf("Distinct aries coordinates\n");
	for(int i = 0; i < num_aries; i++) {
	    printf("Rank %d [%d %d %d %d]\n", distinct_procs[i].world_rank, distinct_procs[i].coord_x, distinct_procs[i].coord_y, distinct_procs[i].coord_z, distinct_procs[i].nid);
	}
    }

    if(num_aries < 4) {
	if(node.world_rank == 0) {
	    printf("Not enough distinct Aries\n");
	}
	MPI_Abort(MPI_COMM_WORLD, 911);
    }

    if(node.world_rank == distinct_procs[0].world_rank) {
	node.isSource = true;
    }
    if(node.world_rank == distinct_procs[num_aries-1].world_rank) {
	node.isDest = true;
    }
    if(node.world_rank == distinct_procs[num_aries/2].world_rank) {
	node.isProxy = true;
    }
    if(node.world_rank == distinct_procs[num_aries/2-1].world_rank) {
	node.isProxy = true;
    }
    int iters = 30;
    int nbytes = 4*1024*1024;

    int sourceId = distinct_procs[0].world_rank;
    int destId = distinct_procs[num_aries-1].world_rank;
    int proxy1 = distinct_procs[num_aries/2-1].world_rank;
    int proxy2 = distinct_procs[num_aries/2].world_rank;

    char *send_buf = (char*)malloc(nbytes*iters);
    char *recv_buf = (char*)malloc(nbytes*iters);
    MPI_Request *request = (MPI_Request*)malloc(sizeof(MPI_Request)*iters);
    MPI_Status *status = (MPI_Status*)malloc(sizeof(MPI_Status)*iters);

    memset(send_buf, 7, iters*nbytes);
    memset(recv_buf, 0, iters*nbytes);

    struct timeval t1, t2;

    MPI_Barrier(MPI_COMM_WORLD);
    gettimeofday(&t1, NULL);

    if(node.isSource) {
	for(int i = 0; i < iters; i++) {
	    MPI_Isend(&send_buf[i*nbytes], nbytes, MPI_BYTE, destId, 0, MPI_COMM_WORLD, &request[i]);
	}
	MPI_Waitall(iters, request, status);
    }

    if(node.isDest) {
	for(int i = 0; i < iters; i++) {
	    MPI_Irecv(&recv_buf[i*nbytes], nbytes, MPI_BYTE, sourceId, 0, MPI_COMM_WORLD, &request[i]);
	}
	MPI_Waitall(iters, request, status);
    }

    gettimeofday(&t2, NULL);

    double latency = ((t2.tv_sec * 1000000 + t2.tv_usec) - (t1.tv_sec * 1000000 + t1.tv_usec))*1.0/iters;
    double max_latency = 0;
    MPI_Reduce(&latency, &max_latency, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    if(node.world_rank == 0) {
	printf("Direct using MPI_Isend/Irecv\n");
	double bandwidth = nbytes*1000000.0/(max_latency*1024*1024);
	printf("%d \t %8.6f \t %8.4f\n", nbytes, bandwidth, max_latency);
    }

    if(node.isDest) {
	if(memcmp(send_buf, recv_buf, iters*nbytes) != 0)
	    printf("Invalid received data\n");
    }

    MPI_Barrier(MPI_COMM_WORLD);

    gettimeofday(&t1, NULL);

    if(node.isSource) {
	for(int i = 0; i < iters/2; i++) {
	    MPI_Isend(&send_buf[i*nbytes], nbytes, MPI_BYTE, proxy1, 0, MPI_COMM_WORLD, &request[i]);
	}
	for(int i = iters/2; i < iters; i++) {
	    MPI_Isend(&send_buf[i*nbytes], nbytes, MPI_BYTE, proxy2, 0, MPI_COMM_WORLD, &request[i]);
	}
	MPI_Waitall(iters, request, status);
    }

    if(node.world_rank == proxy1) {
	for(int i = 0; i < iters/2; i++) {
	    MPI_Recv(&recv_buf[i*nbytes], nbytes, MPI_BYTE, sourceId, 0, MPI_COMM_WORLD, &status[i]);
	    MPI_Isend(&recv_buf[i*nbytes], nbytes, MPI_BYTE, destId, 0, MPI_COMM_WORLD, &request[i]);
	}
	MPI_Waitall(iters/2, request, status);
    }
    if(node.world_rank == proxy2) {
	for(int i = iters/2; i < iters; i++) {
	    MPI_Recv(&recv_buf[i*nbytes], nbytes, MPI_BYTE, sourceId, 0, MPI_COMM_WORLD, &status[i]);
	    MPI_Isend(&recv_buf[i*nbytes], nbytes, MPI_BYTE, destId, 0, MPI_COMM_WORLD, &request[i]);
	}
	MPI_Waitall(iters/2, &request[iters/2], status);
    }

    if(node.isDest) {
	for(int i = 0; i < iters/2; i++) {            
	    MPI_Irecv(&recv_buf[i*nbytes], nbytes, MPI_BYTE, proxy1, 0, MPI_COMM_WORLD, &request[i]);
	    MPI_Irecv(&recv_buf[(i+iters/2)*nbytes], nbytes, MPI_BYTE, proxy2, 0, MPI_COMM_WORLD, &request[i+iters/2]);
	}
	MPI_Waitall(iters, request, status);
    }

    gettimeofday(&t2, NULL);

    latency = ((t2.tv_sec * 1000000 + t2.tv_usec) - (t1.tv_sec * 1000000 + t1.tv_usec))*1.0/iters;
    max_latency = 0;
    MPI_Reduce(&latency, &max_latency, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    if(node.world_rank == 0) {
	printf("Proxy using MPI_Isend/Irecv\n");
	double bandwidth = nbytes*1000000.0/(max_latency*1024*1024);
	printf("%d \t %8.6f \t %8.4f\n", nbytes, bandwidth, max_latency);
    }

    if(node.isDest) {
	if(memcmp(send_buf, recv_buf, iters*nbytes) != 0)
	    printf("Invalid received data\n");
    }

    if(node.isSource)
	memset(send_buf, 7, iters*nbytes);
    else
	memset(send_buf, 0, iters*nbytes);

    memset(recv_buf, 7, iters*nbytes);

    MPI_Win win;
    MPI_Win_create(send_buf, iters*nbytes, 1, MPI_INFO_NULL, MPI_COMM_WORLD, &win);

    MPI_Barrier(MPI_COMM_WORLD);
    gettimeofday(&t1, NULL);
   
    MPI_Win_fence(0, win);

    if(node.isSource) {
	for(int i = 0; i < iters; i++) {
	    MPI_Put(send_buf+i*nbytes, nbytes, MPI_BYTE, destId, nbytes*i, nbytes, MPI_BYTE, win);
	}
    }

    MPI_Win_fence(0, win);

    gettimeofday(&t2, NULL);

    latency = ((t2.tv_sec * 1000000 + t2.tv_usec) - (t1.tv_sec * 1000000 + t1.tv_usec))*1.0/iters;
    max_latency = 0;
    MPI_Reduce(&latency, &max_latency, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    if(node.world_rank == 0) {
	printf("Proxy using MPI_Put\n");
	double bandwidth = nbytes*1000000.0/(max_latency*1024*1024);
	printf("%d \t %8.6f \t %8.4f\n", nbytes, bandwidth, max_latency);
    }

    if(node.isDest) {
	if(memcmp(send_buf, recv_buf, iters*nbytes) != 0)
	    printf("Invalid received data\n");
    }

    gni_return_t   gni_status = GNI_RC_SUCCESS;
    int create_destination_cq = 1;
    int number_of_cq_entries  = iters;
    int number_of_dest_cq_entries = 1;
    gni_post_descriptor_t *rdma_data_desc;

    node.uGNI_createBasicCQ(number_of_cq_entries, number_of_dest_cq_entries);
    node.uGNI_createAndBindEndpoints();

    node.uGNI_regAndExchangeMem(send_buf, nbytes * iters, recv_buf, nbytes * iters);

    /* Allocate the rdma_data_desc array. */
    rdma_data_desc = (gni_post_descriptor_t *) calloc(iters, sizeof(gni_post_descriptor_t));
    assert(rdma_data_desc != NULL); 

    for (int i = 0; i < iters; i++) {
	rdma_data_desc[i].type = GNI_POST_RDMA_PUT;
	if (create_destination_cq != 0) {
	    rdma_data_desc[i].cq_mode = GNI_CQMODE_GLOBAL_EVENT | GNI_CQMODE_REMOTE_EVENT;
	} else {
	    rdma_data_desc[i].cq_mode = GNI_CQMODE_GLOBAL_EVENT;
	}
	rdma_data_desc[i].dlvr_mode = GNI_DLVMODE_PERFORMANCE;
	rdma_data_desc[i].local_addr = (uint64_t) send_buf;
	rdma_data_desc[i].local_mem_hndl = node.send_mem_handle;
	rdma_data_desc[i].rdma_mode = GNI_RDMAMODE_FENCE;
	rdma_data_desc[i].src_cq_hndl = node.cq_handle;
    }

    if(node.isSource)
	memset(send_buf, 7, iters*nbytes);
    else
	memset(send_buf, 0, iters*nbytes);
    memset(recv_buf, 0, iters*nbytes);

    MPI_Barrier(MPI_COMM_WORLD);
    gettimeofday(&t1, NULL);

    if(node.isSource) {
	for(int i = 0; i < iters; i++) {
	    /* Send the data. */
	    rdma_data_desc[i].local_addr = (uint64_t) send_buf;
	    rdma_data_desc[i].local_addr += i * nbytes;
	    rdma_data_desc[i].remote_addr = node.remote_memory_handle_array[destId].addr;
	    rdma_data_desc[i].remote_addr += i * nbytes;
	    rdma_data_desc[i].remote_mem_hndl = node.remote_memory_handle_array[destId].mdh;
	    rdma_data_desc[i].length = nbytes;
	    gni_status = GNI_PostRdma(node.endpoint_handles_array[destId], &rdma_data_desc[i]);
	    if (gni_status != GNI_RC_SUCCESS) {
		fprintf(stdout, "Rank: %4i GNI_PostRdma data ERROR status: %d\n", node.world_rank, gni_status);
		postRdmaStatus(gni_status);
		continue;
	    }
	}
	node.uGNI_waitAllSendDone(destId, node.cq_handle, iters);
    }
    if(node.isDest) {
	node.uGNI_waitAllRecvDone(sourceId, node.destination_cq_handle, iters);
    }

    gettimeofday(&t2, NULL);

    latency = ((t2.tv_sec * 1000000 + t2.tv_usec) - (t1.tv_sec * 1000000 + t1.tv_usec))*1.0/iters;
    max_latency = 0;
    MPI_Reduce(&latency, &max_latency, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    if(node.world_rank == 0) {
	printf("Direct using uGNI_PostRdma\n");
	double bandwidth = nbytes*1000000.0/(max_latency*1024*1024);
	printf("%d \t %8.6f \t %8.4f\n", nbytes, bandwidth, max_latency);
    }

    if(node.isDest) {
	if(memcmp(send_buf, recv_buf, iters*nbytes) != 0)
	    printf("Invalid received data\n");
    }

    PMI_Finalize();

    MPI_Finalize();

}
