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
	    if(!existing) {
		num_aries++;
		distinct_procs[j].world_rank = procs[i].world_rank;
		distinct_procs[j].coord_x = procs[i].coord_x;
		distinct_procs[j].coord_y = procs[i].coord_y;
		distinct_procs[j].coord_z = procs[i].coord_z;
		distinct_procs[j].nid = procs[i].nid;
	    }
	}
    }

    /*Print distinct aries*/
    if(node.world_rank == 0) {
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

    struct timeval t1, t2;
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
	double bandwidth = nbytes*1000000.0/(max_latency*1024*1024);
	printf("%d \t %8.6f \t %8.4f\n", nbytes, bandwidth, max_latency);
    }

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
        double bandwidth = nbytes*1000000.0/(max_latency*1024*1024);
        printf("%d \t %8.6f \t %8.4f\n", nbytes, bandwidth, max_latency);
    }

    PMI_Finalize();

    MPI_Finalize();

}
