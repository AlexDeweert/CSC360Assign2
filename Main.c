#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/queue.h>
#define NUM_THREADS 5
#define COUNT_LIM 13
#define DEBUG 1
#define LIVE 1
#define DEAD 0
#define EMPTY 0
#define FULL 1
#define TRUE 1
#define FALSE 0

//Structs
typedef struct Trains{
	char direction;
	long loading_time;
	long crossing_time;
	long train_id;
	long live;
	long loaded;
	long dispatched;
	pthread_cond_t convar_train_waiting;
	TAILQ_ENTRY(Trains) entries;
} Train;

// //Tailqueue from "man queue" example
// struct tailqueue_entry {
// 	int train_id;
// 	TAILQ_ENTRY(tailqueue_entry) entries;
// };

//Initializing tailqueue head
TAILQ_HEAD(, Trains) my_tailq_head;

//Prototype
void* dispatch(void*);
void* load(void*);
void initializeThreads(Train**,int*,pthread_t**);
void printArray(Train**,int*);
void readInput(char*,int*,Train**,int*);
void* load_controller(void*);
void mutexTest( long id );

//Mutexes
pthread_mutex_t mutex_load;
pthread_mutex_t mutex_get_dispatched; //Train will lock this if ready to get sent to PQ
pthread_mutex_t mutex_eastbound_queue;
pthread_mutex_t mutex_post_loading_wait;

//Convars
pthread_cond_t convar_begin_loading;
pthread_cond_t convar_ready_for_dispatch; //The train will use this to signal the dispatcher
pthread_cond_t convar_dispatcher_add_to_queue;

//Attributes
pthread_attr_t thread_attr; //also joinable
pthread_attr_t thread_attr_joinable;

//Semaphores
sem_t semaphore_all_threads_dead;
sem_t semaphore_trains_live;

//Globals and definitions
int numTrains;
int num_live_threads = 0;
int num_dispatched_trains = 0;
int finished_loading_count = 0;
int all_trains_live = 0; //used by convar to signal start of "loading" or "SayHello" for now.
int num_loaded_trains = 0;
int loaded_train_index = 0;
int sleptCount = 0;

int main( int argc, char** argv ) {


	/*TAILQ INIT*/
	Train* item;
	Train* temp_item;
	TAILQ_INIT(&my_tailq_head);

	// /*Add items to queue*/
	// item = malloc( sizeof(int));


	// sem_init(&semaphore_all_threads_dead, 0, 0);
	// sem_init(&semaphore_trains_live, 0, 6);
	//pthread_attr_init(&thread_attr);
	//pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_DETACHED);
	/* Initialize mutex and condition variable objects */
	pthread_mutex_init(&mutex_load, NULL);
	pthread_mutex_init(&mutex_get_dispatched, NULL);
	pthread_mutex_init(&mutex_eastbound_queue, NULL);
	pthread_mutex_init(&mutex_post_loading_wait, NULL);
	pthread_cond_init (&convar_begin_loading, NULL);
	pthread_cond_init (&convar_ready_for_dispatch, NULL);
	pthread_cond_init (&convar_dispatcher_add_to_queue, NULL);
	pthread_attr_init(&thread_attr);
	pthread_attr_init(&thread_attr_joinable);
	pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_JOINABLE);
	pthread_attr_setdetachstate(&thread_attr_joinable, PTHREAD_CREATE_JOINABLE);
	
	int train_count = 0;
	int arr_size = 50;
	int i;
	Train* train_data;
	pthread_t* train_threads;
	pthread_t dispatcher_thread;
	long t;
	int rc;
	void* status;
	
	if( argc < 2 ) {
		printf("[ERROR] Must have at least one argument (HINT: The name of a text file with train data!)\n");
		return 1;
	}
	//Read input file and allocate train structs to array
	readInput(argv[1],&train_count,&train_data,&arr_size);



	




	//Create dispatcher to poll loaded trains
	pthread_create( &dispatcher_thread, &thread_attr, dispatch, (void*)&train_data );

	//Create all train threads
	initializeThreads(&train_data,&train_count,&train_threads);	
	
	//INVALUABLE: https://computing.llnl.gov/tutorials/pthreads/samples/dotprod_mutex.c
	//Can get concurrency without having to call DETACH
	//by doing the join down here...wtf
	pthread_attr_destroy(&thread_attr);
	pthread_join( dispatcher_thread, NULL );
	for( i = 0; i < numTrains; i++ ) {
		pthread_join( train_threads[i], NULL );
	}

	/* Traverse the tail QUEUE forward. */
    printf("Forward traversal: ");
    TAILQ_FOREACH(item, &my_tailq_head, entries) {
            printf("%ld ", item->train_id);
    }
    printf("\n");

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
	(*train_threads) = malloc( sizeof(pthread_t)*(*train_count) );
	int i;
	for( i = 0; i < (*train_count); i++ ) {
		pthread_mutex_lock(&mutex_load);
		printf("Train %ld waiting to load, %d livethreads exist\n", (*train_data)[i].train_id, num_live_threads);
		num_live_threads++;
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
		pthread_cond_wait( &convar_begin_loading, &mutex_load );
		
	}
	printf("Train %ld loading...\n", train_data->train_id);
	pthread_mutex_unlock(&mutex_load);
	usleep( train_data->loading_time * 100000 );
	train_data->loaded = 1;
	printf("Train %ld finished loading!\n", train_data->train_id);

	//For now put everything into queue
	pthread_mutex_lock(&mutex_eastbound_queue);
	//mutexTest(train_data->train_id);
	TAILQ_INSERT_TAIL(&my_tailq_head, train_data, entries);
	pthread_mutex_unlock(&mutex_eastbound_queue);

	/*When done loading, sleep here.
	-Train sleeps, but signals that it's ready to be loaded
	-When it signals, the index of the train to be loaded is added to "ready_loaded" queue
	-The dispatcher is waiting until the "ready_loaded" count > 0
	-The dispatcher adds the approprite train index to the Eb, eb, Wb, wb queues
	-There, the track manager first picks between HIGH pri and LOW pri queues, and sends train according to rule (thread goes to crossing function)
	-Once the Eb and Wb queues are empty, it does the same thing to the low priority queues.
	*/
	// printf("Train %ld going to sleep\n", train_data->train_id);
	// //sleep((int)train_data->crossing_time);
	// printf("Train %ld woke up\n", train_data->train_id);

	pthread_exit((void*) 0);
}

//Poll loaded trains for dispatch
void* dispatch( void* arg ) {

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

void mutexTest( long id ) {
	sleptCount++;
	printf("Train: %ld called MUTEX TEST, sleptCount is: %d\n", id, sleptCount);
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
		(*train_data)[(*train_count)].loaded = EMPTY;
		(*train_data)[(*train_count)].dispatched = FALSE;
		pthread_cond_init(&((*train_data)[(*train_count)].convar_train_waiting), NULL);
		
		//Tailqueue practice
		/*
         * Add our item to the end of tail queue. The first
         * argument is a pointer to the head of our tail
         * queue, the second is the item we want to add, and
         * the third argument is the name of the struct
         * variable that points to the next and previous items
         * in the tail queue.
         */
        //TAILQ_INSERT_TAIL(&my_tailq_head, &(*train_data)[(*train_count)], entries);


		(*train_count)++;
		numTrains++;

		

	}
}