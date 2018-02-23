#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#define NUM_THREADS 5
#define COUNT_LIM 13
#define DEBUG 1
#define LIVE 1
#define DEAD 0

//Structs
typedef struct Trains{
	char direction;
	long loading_time;
	long crossing_time;
	long train_id;
	long live;
} Train;

//Prototype
void* load(void*);
void initializeThreads(Train**,int*,pthread_t**);
void printArray(Train**,int*);
void readInput(char*,int*,Train**,int*);
void* load_controller(void*);


//Mutexes
pthread_mutex_t mutex_load;

//Convars
pthread_cond_t convar_begin_loading;

//Attributes
pthread_attr_t thread_attr;

//Semaphores
sem_t semaphore_all_threads_dead;
sem_t semaphore_trains_live;

//Globals and definitions
int numTrains;
int num_live_threads = 0;
int finished_loading_count = 0;
int all_trains_live = 0; //used by convar to signal start of "loading" or "SayHello" for now.

int main( int argc, char** argv ) {

	// sem_init(&semaphore_all_threads_dead, 0, 0);
	// sem_init(&semaphore_trains_live, 0, 6);
	//pthread_attr_init(&thread_attr);
	//pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_DETACHED);
	/* Initialize mutex and condition variable objects */
	pthread_mutex_init(&mutex_load, NULL);
	pthread_cond_init (&convar_begin_loading, NULL);
	pthread_attr_init(&thread_attr);
	pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_JOINABLE);
	
	int train_count = 0;
	int arr_size = 50;
	int i;
	Train* train_data;
	pthread_t* train_threads;
	pthread_t loading_controller;
	long t;
	int rc;
	void* status;
	
	if( argc < 2 ) {
		printf("[ERROR] Must have at least one argument (HINT: The name of a text file with train data!)\n");
		return 1;
	}
	//Read input file and allocate train structs to array
	readInput(argv[1],&train_count,&train_data,&arr_size);
	//Create all train threads
	initializeThreads(&train_data,&train_count,&train_threads);	
	
	//INVALUABLE: https://computing.llnl.gov/tutorials/pthreads/samples/dotprod_mutex.c
	//Can get concurrency without having to call DETACH
	//by doing the join down here...wtf
	pthread_attr_destroy(&thread_attr);
	for( i = 0; i < numTrains; i++ ) {
		pthread_join( train_threads[i], NULL );
	}

	//AFTER JOINING (killing) CLEAN UP
	pthread_mutex_destroy(&mutex_load);
	pthread_cond_destroy(&convar_begin_loading);
	free(&train_data[0]);
	free(&train_threads[0]);
	train_data = NULL;
	train_threads = NULL;
	pthread_exit(NULL);
}

void initializeThreads( Train** train_data, int* train_count, pthread_t** train_threads ) {
	printf("Got here\n");
	(*train_threads) = malloc( sizeof(pthread_t)*(*train_count) );
	int i;
	for( i = 0; i < (*train_count); i++ ) {
		pthread_mutex_lock(&mutex_load);
		num_live_threads++;
		printf("LIVE THREAD++ = %d\n", num_live_threads );
		pthread_create( &(*train_threads)[i], &thread_attr, load, (void*)&(*train_data)[i] );
		
		if( (*train_count) == numTrains ) { 
			pthread_cond_broadcast(&convar_begin_loading);
		}
		pthread_mutex_unlock(&mutex_load);
	}
}

void* load( void* arg ) {
	Train *train_data = (Train*)arg;
	pthread_mutex_lock(&mutex_load);
	while( num_live_threads < numTrains ) {
		printf("Train %ld waiting to load\n", train_data->train_id);
		pthread_cond_wait( &convar_begin_loading, &mutex_load );
	}
	printf("Train %ld loading...\n", train_data->train_id);
	pthread_mutex_unlock(&mutex_load);
	usleep( train_data->loading_time * 1000000 );
	train_data->live = 0;
	printf("Train %ld finished loading!\n", train_data->train_id);
	pthread_exit((void*) 0);
}

void printArray( Train** train_data,int* train_count ) {
		int i;
		for( i = 0; i < (*train_count); i++ ) {
		if(DEBUG) printf( "[DEBUG] [READING TRAIN %.3ld with PRI: %c, LT: %ld, CT: %ld, LIVE: %ld]\n", 
			(*train_data)[i].train_id, 
			(*train_data)[i].direction, 
			(*train_data)[i].loading_time, 
			(*train_data)[i].crossing_time,
			(*train_data)[i].live );
	}
}

/*Read in the train file*/
void readInput(char* filename, int* train_count, Train** train_data, int* arr_size) {
	char priority;
	int loading_time;
	int crossing_time;
	int i;
	(*train_data) = malloc(sizeof(Train)*(*arr_size)); //array of trains
	if(DEBUG) printf("[DEBUG] Called readInput(), filename input was: \"%s\"\n", filename);
	FILE* in = fopen(filename, "r");
	while( fscanf( in, "%c %d %d *[\n]", &priority, &loading_time, &crossing_time ) != EOF ) {
		(*train_data)[(*train_count)].direction = priority;
		(*train_data)[(*train_count)].loading_time = loading_time;
		(*train_data)[(*train_count)].crossing_time = crossing_time;
		(*train_data)[(*train_count)].train_id = (*train_count);
		(*train_data)[(*train_count)].live = LIVE;
		(*train_count)++;
		numTrains++;
	}
}