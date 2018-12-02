#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "traffic.h"

extern struct intersection isection;

/**
 * Populate the car lists by parsing a file where each line has
 * the following structure:
 *
 * <id> <in_direction> <out_direction>
 *
 * Each car is added to the list that corresponds with 
 * its in_direction
 * 
 * Note: this also updates 'inc' on each of the lanes
 */

void parse_schedule(char *file_name) {
    int id;
    struct car *cur_car;
    struct lane *cur_lane;
    enum direction in_dir, out_dir;
    FILE *f = fopen(file_name, "r");

    /* parse file */
    while (fscanf(f, "%d %d %d", &id, (int*)&in_dir, (int*)&out_dir) == 3) {

        /* construct car */
        cur_car = malloc(sizeof(struct car));
        cur_car->id = id;
        cur_car->in_dir = in_dir;
        cur_car->out_dir = out_dir;

        /* append new car to head of corresponding list */
        cur_lane = &isection.lanes[in_dir];
        cur_car->next = cur_lane->in_cars;
        cur_lane->in_cars = cur_car;
        cur_lane->inc++;
    }

    fclose(f);
}

/**
 * TODO: Fill in this function
 *
 * Do all of the work required to prepare the intersection
 * before any cars start coming
 * 
 */
void init_intersection() {
	int i;
	for(i = 0; i < 4; i++){

        //Initialize mutex quad[i]
        pthread_mutex_init(&isection.quad[i], NULL);

        //Initialize lane[i]
		pthread_mutex_init(&isection.lanes[i].lock, NULL);
		pthread_cond_init(&isection.lanes[i].producer_cv, NULL);
		pthread_cond_init(&isection.lanes[i].consumer_cv, NULL);
		isection.lanes[i].in_cars = NULL;
		isection.lanes[i].out_cars = NULL;
		isection.lanes[i].inc = 0;
		isection.lanes[i].passed = 0;
		isection.lanes[i].buffer = malloc(sizeof(struct car*) * LANE_LENGTH);
		isection.lanes[i].head = 0;
		isection.lanes[i].tail = 0;
		isection.lanes[i].capacity = LANE_LENGTH;
		isection.lanes[i].in_buf = 0;
	}

}

/**
 * TODO: Fill in this function
 *
 * Populates the corresponding lane with cars as room becomes
 * available. Ensure to notify the cross thread as new cars are
 * added to the lane.
 * 
 */
void *car_arrive(void *arg) {
    struct lane *l = arg;

    while(l->in_cars != NULL){

        //Acquire the lock
        pthread_mutex_lock(&l->lock);

        while(l->in_buf == l->capacity){
            //The buffer is full, release the lock and wait for the signal from car_across
            pthread_cond_wait(&l->consumer_cv, &l->lock);
        }

        //Add the current car into the list
        l->buffer[l->tail] = l->in_cars;

        //Update the index of the last element in the list
        l->tail = (l->tail + 1) % l->capacity;

        //Increase the number of elements in the buffer
        l->in_buf += 1;

        //The buffer is not empty, signal car_across
        pthread_cond_signal(&l->producer_cv);

        //Move to the next car that are pending
        l->in_cars = l->in_cars->next;

        //Release the lock
        pthread_mutex_unlock(&l->lock);
    }
    return NULL;
}

/**
 * TODO: Fill in this function
 *
 * Moves cars from a single lane across the intersection. Cars
 * crossing the intersection must abide the rules of the road
 * and cross along the correct path. Ensure to notify the
 * arrival thread as room becomes available in the lane.
 *
 * Note: After crossing the intersection the car should be added
 * to the out_cars list of the lane that corresponds to the car's
 * out_dir. Do not free the cars!
 *
 * 
 * Note: For testing purposes, each car which gets to cross the 
 * intersection should print the following three numbers on a 
 * new line, separated by spaces:
 *  - the car's 'in' direction, 'out' direction, and id.
 * 
 * You may add other print statements, but in the end, please 
 * make sure to clear any prints other than the one specified above, 
 * before submitting your final code. 
 */
void *car_cross(void *arg) {
    struct lane *l = arg;


    while(l->passed < l->inc){
    //Not all cars have passed through the lane

        //Acquire the lock
        pthread_mutex_lock(&l->lock);

        while(l->in_buf == 0){
            //The buffer is empty, release the lock and wait for the signal from car_arrive
            pthread_cond_wait(&l->producer_cv, &l->lock);
        }

        //Compute the car path
        int *path = compute_path(l->buffer[l->head]->in_dir, l->buffer[l->head]->out_dir);

        int i;
        for(i = 0; i < 4; i++){
        //The length of path is 3
            if(path[i] != 0){
                //If there is a quadrant in path[i], lock the quadrant
                pthread_mutex_lock(&isection.quad[i]);
            }
        }

        //Add the first car in the buffer to out_cars list
        l->buffer[l->head]->next = l->out_cars;
        l->out_cars = l->buffer[l->head];

        //Remove the car from the buffer
        l->buffer[l->head] = NULL;

        //Update the index of the first element in the buffer
        l->head = (l->head + 1) % l->capacity;

        //Update the number of passed cars
        l->passed += 1;

        //Update the number of elements in the buffer
        l->in_buf -= 1;

        //Print relavent information
        printf("the car's '%d' direction, '%d' direction, and %d. ", l->out_cars->in_dir, l->out_cars->out_dir, l->out_cars->id);


        //Release the quadrant locks
        for(i = 0; i < 4; i++){
            if(path[i] != 0){
                pthread_mutex_unlock(&isection.quad[i]);
            }
        }

        //Deallocate the memory of path
        free(path);

        //The buffer is not empty, signal car_arrive
        pthread_cond_signal(&l->consumer_cv);

        //Release the lock
        pthread_mutex_unlock(&l->lock);

    }

    //The buffer is empty, free the memory allocated to the buffer
    free(l->buffer);

    return NULL;
}

/**
 * TODO: Fill in this function
 *
 * Given a car's in_dir and out_dir return a sorted 
 * list of the quadrants the car will pass through.
 * 
 */
int *compute_path(enum direction in_dir, enum direction out_dir) {

    //Construct a path
    //Use a path of length 4, each representing a direction
    //path[0]: EAST quad1, path[1]: NORTH quad2, path[2]: WEST quad3, path[3]: SOUTH quad4
    int i;
    int* path = malloc(sizeof(int) * 3);
    for(i = 0; i < 4; i++){
        path[i] = 0;
    }

    //Deal with different in_dir and out_dir
    if(in_dir == NORTH){
        path[1] = 1;
        if(out_dir == EAST){
            //The car needs Q2, Q3 and Q4, 
            //set the corresponding path[i] to 1
            path[2] = 1;
            path[3] = 1;
        }else if(out_dir == SOUTH){
            path[2] = 1;
        }
    }else if(in_dir == EAST){
        path[0] = 1;
        if(out_dir == SOUTH){
            path[1] = 1;
            path[2] = 1;
        }else if(out_dir == WEST){
            path[1] = 1;
        }
    }else if(in_dir == SOUTH){
        path[3] = 1;
        if(out_dir == WEST){
            path[0] = 1;
            path[1] = 1;
        }else if(out_dir == NORTH){
            path[0] = 1;
        }
    }else{
        path[2] = 1;
        if(out_dir == NORTH){
            path[3] = 1;
            path[0] = 1;
        }else if(out_dir == EAST){
            path[3] = 1;
        }
    }

    return path;
}




