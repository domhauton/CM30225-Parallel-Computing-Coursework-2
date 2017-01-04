//
// Created by dominic on 31/10/16.
//

#include <stdlib.h>
#include <stdio.h>
#include <mpi.h>
#include "mat_itr.h"
#include "../smoothing/smoother.h"
#include "mat_factory.h"

typedef struct MAT_T {
    int xSize, ySize;
    double *data;
} mat_t;

/* Creates the matrix and allocates values */
mat_t *mat_init(double *values, int xSize, int ySize) {
    mat_t *matrix = malloc(sizeof(mat_t));
    matrix->data = values;
    matrix->xSize = xSize;
    matrix->ySize = ySize;
    return matrix;
}

/* Returns a matrix data pointer at the given coordinates
        Out of bounds coordinates will result in undefined behaviour */
double *mat_data_ptr(mat_t *matrix, long x, long y) {
    if(matrix == NULL) {
        return NULL;
    }
    return matrix->data + x + (matrix->xSize * y);
}

/* Returns a matrix iterator starting at the given co-ordinates for the given width and height
        Out of bounds area will result in undefined behaviour */
mat_itr_t *mat_itr_create_partial(mat_t *matrix, long x, long y, long width, long height) {
    double *initialDataPtr = mat_data_ptr(matrix, x, y);
    return mat_itr_init(initialDataPtr, matrix->xSize, width, height);
}

mat_itr_edge_t *mat_itr_edge_create(mat_t *matrix) {
    return mat_itr_edge_init(matrix->data, matrix->xSize, matrix->ySize);
}

/* Initialises a matrix smoother for an area starting at the given co-ordinates for the given width and height
        Will smooth until doubles do not surpass limit.
        overLimit may be shared between multiple smoothers (thread safe).
        target Matrix *must* have same dimensions as source.
        */
smoother_t *smoother_create_partial(mat_t *source, mat_t *tmp,
                                      double limit, bool *overLimit,
                                      long x, long y,
                                      long xSize, long ySize) {
    // Get all the pointers required and convert them to iterators for easy management.
    mat_itr_t *resItr = mat_itr_create_partial(tmp, x, y, xSize, ySize);
    mat_itr_t *ctrItr = mat_itr_create_partial(source, x, y, xSize, ySize);
    mat_itr_t *topItr = mat_itr_create_partial(source, x - 1, y, xSize, ySize);
    mat_itr_t *botItr = mat_itr_create_partial(source, x + 1, y, xSize, ySize);
    mat_itr_t *lftItr = mat_itr_create_partial(source, x, y - 1, xSize, ySize);
    mat_itr_t *rgtItr = mat_itr_create_partial(source, x, y + 1, xSize, ySize);
    return smoother_init(resItr, ctrItr, topItr, botItr, lftItr, rgtItr, overLimit, limit);
}

/* Initialise a smoother for the inside of the matrix (excluding outer edge) */
smoother_t *smoother_single_init(mat_t *source, mat_t *target, double limit, bool *overLimit) {
    long itrWidth = target->xSize - 2;
    long itrHeight = target->ySize - 2;
    return smoother_create_partial(source, target, limit, overLimit, 1, 1, itrWidth, itrHeight);
}

/* Smooths the inside (exclude outer edge) of the given matrix until it changes < limit. */
mat_t *mat_smooth(mat_t *source, mat_t *target, double limit, bool *overLimit) {
    long ctr = 0;
    mat_t *tmp;
    do {
        *overLimit = false;
        smoother_t *smoother = smoother_single_init(source, target, limit, overLimit);
        smoother_run(smoother);
        smoother_destroy(smoother);

        tmp = target;
        target = source;
        source = tmp;
        ctr++;
    } while (*overLimit);

    printf("%08li,", ctr);

    return source;
}

/* Used for type punning between a double and a unsigned long long int */
union {
    double d; // C99 double == 64 bytes
    unsigned long long int ll; // C99 long long >= 64 bytes
} double_ulld_punner_u;

/* Calculates a 64 byte (C99) parity of the given matrix section across all processes.
   Must be called on all nodes. */
unsigned long long int mat_mpi_parity(mat_t *matrix) {
    unsigned long long int currentParity = 0ULL;
    int node, totalNodes;
    MPI_Comm_rank(MPI_COMM_WORLD, &node);
    MPI_Comm_size(MPI_COMM_WORLD, &totalNodes);

    if(node == 0){ // Only calculate top row if first rank
        mat_itr_t* itr = mat_itr_create_partial(matrix, 0, 0, matrix->xSize, 1);
        while (mat_itr_hasNext(itr)) {
            double_ulld_punner_u.d = *mat_itr_next(itr);
            currentParity ^= double_ulld_punner_u.ll;
        }
        mat_itr_destroy(itr);
    }
    // Could be first and last rank
    if(node == totalNodes - 1) { // Only calculate bottom row if last rank
        mat_itr_t* itr = mat_itr_create_partial(matrix, 0, matrix->ySize-1, matrix->xSize, 1);
        while (mat_itr_hasNext(itr)) {
            double_ulld_punner_u.d = *mat_itr_next(itr);
            currentParity ^= double_ulld_punner_u.ll;
        }
        mat_itr_destroy(itr);
    }
    // Calculate middle section
    mat_itr_t* itr = mat_itr_create_partial(matrix, 0, 1, matrix->xSize, matrix->ySize-2);
    while (mat_itr_hasNext(itr)) {
        double_ulld_punner_u.d = *mat_itr_next(itr);
        currentParity ^= double_ulld_punner_u.ll;
    }
    mat_itr_destroy(itr);
    unsigned long long int totalParity = 0;
    MPI_Reduce(&currentParity, &totalParity, 1,
                   MPI_UNSIGNED_LONG_LONG, MPI_BXOR,
                   0, MPI_COMM_WORLD);
    return totalParity;
}

/* Calculates a 64 byte (C99) parity of the given matrix section across all processes.
   Must be called on all nodes. */
unsigned long long int mat_mpi_crc64(mat_t *matrix) {
    unsigned long long int currentCRC = 0ULL;
    int node, totalNodes;
    MPI_Comm_rank(MPI_COMM_WORLD, &node);
    MPI_Comm_size(MPI_COMM_WORLD, &totalNodes);

    if(node == 0){ // Only calculate top row if first rank
        mat_itr_t* itr = mat_itr_create_partial(matrix, 0, 0, matrix->xSize, 1);
        while (mat_itr_hasNext(itr)) {
            double_ulld_punner_u.d = *mat_itr_next(itr);
            currentCRC += double_ulld_punner_u.ll;
        }
        mat_itr_destroy(itr);
    }
    // Could be first and last rank
    if(node == totalNodes - 1) { // Only calculate bottom row if last rank
        mat_itr_t* itr = mat_itr_create_partial(matrix, 0, matrix->ySize-1, matrix->xSize, 1);
        while (mat_itr_hasNext(itr)) {
            double_ulld_punner_u.d = *mat_itr_next(itr);
            currentCRC += double_ulld_punner_u.ll;
        }
        mat_itr_destroy(itr);
    }
    // Calculate middle section
    mat_itr_t* itr = mat_itr_create_partial(matrix, 0, 1, matrix->xSize, matrix->ySize-2);
    while (mat_itr_hasNext(itr)) {
        double_ulld_punner_u.d = *mat_itr_next(itr);
        currentCRC += double_ulld_punner_u.ll;
    }
    mat_itr_destroy(itr);
    unsigned long long int totalCRC = 0;
    MPI_Reduce(&currentCRC, &totalCRC, 1,
                  MPI_UNSIGNED_LONG_LONG, MPI_SUM,
                  0, MPI_COMM_WORLD);
    return totalCRC;
}

/* Calculates a 64 byte (C99) parity of the given matrix */
unsigned long long int mat_parity_local(mat_t *matrix) {
    unsigned long long int currentParity = 0ULL;
    mat_itr_t* itr = mat_itr_create_partial(matrix, 0, 0, matrix->xSize, matrix->ySize);
    while (mat_itr_hasNext(itr)) {
        double_ulld_punner_u.d = *mat_itr_next(itr);
        currentParity ^= double_ulld_punner_u.ll;
    }
    mat_itr_destroy(itr);
    return currentParity;
}

/* Calculates a 64 byte crc of the given Matrix */
unsigned long long int mat_crc64_local(mat_t *matrix) {
    unsigned long long int currentCRC = 0ULL;
    mat_itr_t* itr = mat_itr_create_partial(matrix, 0, 0, matrix->xSize, matrix->ySize);
    while (mat_itr_hasNext(itr)) {
        double_ulld_punner_u.d = *mat_itr_next(itr);
        currentCRC += double_ulld_punner_u.ll;
    }
    mat_itr_destroy(itr);
    return currentCRC;
}

/* Copies the outer edge from source to target matrix
    If matrices of different size, behaviour undefined */
void mat_copy_edge(mat_t *source, mat_t *target) {
    mat_itr_edge_t *edgeIterator1 = mat_itr_edge_create(source);
    mat_itr_edge_t *edgeIterator2 = mat_itr_edge_create(target);
    while (mat_itr_edge_hasNext(edgeIterator1)) {
        *mat_itr_edge_next(edgeIterator2) = *mat_itr_edge_next(edgeIterator1);
    }
    mat_itr_edge_destroy(edgeIterator1);
    mat_itr_edge_destroy(edgeIterator2);
}

/* Pretty prints the matrix to STDOUT */
void mat_print_local(mat_t *matrix) {
    long size = matrix->xSize * matrix->ySize;
    double *ptr = matrix->data;
    for (long i = 1; i <= size; i++) {
        if (i % matrix->xSize != 0) {
            printf("%f, ", *ptr++);
        } else {
            printf("%f\n", *ptr++);
        }
    }
}

/* Pretty prints the matrix one node at a time in the correct sequence */
void mat_mpi_print(mat_t *matrix) {
    int node, totalNodes;
    MPI_Comm_rank(MPI_COMM_WORLD, &node);
    MPI_Comm_size(MPI_COMM_WORLD, &totalNodes);

    // Block barrier until node's turn
    for(int i = 0; i < node; i++) {
        MPI_Barrier(MPI_COMM_WORLD);
    }
    printf("SRT %d.\n", node);
    mat_print_local(matrix);
    printf("END %d.\n", node);

    // Trigger the barrier for other nodes too
    for(int i = node; i < (totalNodes-1); i++) {
        MPI_Barrier(MPI_COMM_WORLD);
    }
}

/* Compares the two matrices for equality */
bool mat_equals(mat_t *matrix1, mat_t *matrix2) {
    if (matrix1->ySize == matrix2->ySize && matrix1->xSize == matrix2->xSize) {
        bool match = true;
        mat_itr_t *matIterator1 = mat_itr_create_partial(matrix1, 0, 0, matrix1->xSize, matrix1->ySize);
        mat_itr_t *matIterator2 = mat_itr_create_partial(matrix2, 0, 0, matrix2->xSize, matrix2->ySize);
        while (mat_itr_hasNext(matIterator1) && match) {
            match = *mat_itr_next(matIterator1) == *mat_itr_next(matIterator2);
        }
        mat_itr_destroy(matIterator1);
        mat_itr_destroy(matIterator2);
        return match;
    } else {
        return false;
    }
}

/* Sends the top row to the rank above and bottom row to the rank below if they exist
   Returns the amount of requests required for sending */
int mat_mpi_shareRows(mat_t *mat, MPI_Request *mpi_request) {
    int node, totalNodes;
    MPI_Comm_rank(MPI_COMM_WORLD, &node);
    MPI_Comm_size(MPI_COMM_WORLD, &totalNodes);
    int retInt = 0;
    if(node > 0) {
        double* top = mat_data_ptr(mat, 0, 1);
        MPI_Isend(top, mat->xSize, MPI_DOUBLE, node-1, 0, MPI_COMM_WORLD, mpi_request++);
        retInt++;
    }
    if (node < totalNodes - 1){
        double* bottom = mat_data_ptr(mat, 0, mat->ySize - 2);
        MPI_Isend(bottom, mat->xSize, MPI_DOUBLE, node+1, 0, MPI_COMM_WORLD, mpi_request);
        retInt++;
    }
    return retInt;
}

/* Accepts the top row to the rank above and bottom row to the rank below if they exist
   Returns the amount of requests required for receipt*/
int mat_mpi_acceptEdgeRows(mat_t *mat, MPI_Request *mpi_request) {
    int node, totalNodes;
    MPI_Comm_rank(MPI_COMM_WORLD, &node);
    MPI_Comm_size(MPI_COMM_WORLD, &totalNodes);
    int retInt = 0;
    if(node > 0) {
        double* top = mat_data_ptr(mat, 0, 0);
        MPI_Irecv(top, mat->xSize, MPI_DOUBLE, node-1, MPI_ANY_TAG, MPI_COMM_WORLD, mpi_request++);
        retInt++;
    }
    if(node < totalNodes - 1) {
        double* bottom = mat_data_ptr(mat, 0, mat->ySize - 1);
        MPI_Irecv(bottom, mat->xSize, MPI_DOUBLE, node+1, MPI_ANY_TAG, MPI_COMM_WORLD, mpi_request);
        retInt++;
    }
    return retInt;
}

/* Returns a new matrix that has copied all the values on the outer edge of the given matrix */
mat_t *mat_init_clone_edge(mat_t *matrix) {
    mat_t* clone = mat_factory_init_empty(matrix->xSize, matrix->ySize);
    mat_copy_edge(matrix, clone);
    return clone;
}

/* Frees the given matrix from memory. Matrix must not be used afterwards. */
void mat_destroy(mat_t *matrix) {
    free(matrix->data);
    free(matrix);
}


