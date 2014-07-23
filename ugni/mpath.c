/*
* Multiple paths data movement
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

    proc_t *proc = (roc*)malloc(sizeof(Proc));
    proc->world_rank = node.world_rank;
    proc->nid = node.nid;
    proc->coord_x = node.coord.mesh_x;
    proc->coord_y = node.coord.mesh_y;
    proc->coord_z = node.coord.mesh_z;

    Proc *procs = (Proc*)malloc(sizeof(Proc)*node.world_size);
    MPI_Allgather(proc, 5, MPI_INT, procs, 5, MPI_INT, MPI_COMM_WORLD); 

    if(node.world_rank == 0) {
	for(int i = 0; i < node.world_size; i++) {
	    printf("Rank %d [%d %d %d %d]\n", procs[i].world_rank, procs[i].coord_x, procs[i].coord_y, procs[i].coord_z, procs[i].nid);
	}
    }

    Proc *distinct_procs = (Proc*)malloc(sizeof(Proc)*node.world_size);
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
		distinct_procs[i].world_rank = procs[i].world_rank;
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

    for(int i = 0; i < num_aries; i++) {
	
    }
    

    PMI_Finalize();

    MPI_Finalize();

}
