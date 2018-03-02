#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/queue.h>
#include <math.h>
#define BILLION 1000000000L
#define MILLION 1000000L
#define HUND_THOUSAND 100000L
#define NUM_THREADS 5
#define COUNT_LIM 13
#define DEBUG 0
#define LIVE 1
#define DEAD 0
#define EMPTY 0
#define FULL 1
#define TRUE 1
#define FALSE 0
#define QUEUE_EMPTY -1
//Structs
typedef struct Trains{
	char direction;
	char dir[4];
	long loading_time;
	long crossing_time;
	long train_id;
	long live;
	long ready;
	long dispatched;
	pthread_cond_t convar_cross;
} Train;

typedef struct Node {
	struct Node* next;
	int train_index;
} Node;

struct timespec start={0,0}, stop={0,0}, current={0,0};

//Queue heads
Node* eb_q = NULL;
Node* Eb_q = NULL;
Node* wb_q = NULL;
Node* Wb_q = NULL;

//Prototypes
void printTimeStamp();
void* timer(void*);
void* dispatcher(void*);
void* train_function(void*);
void initializeThreads(Train**,pthread_t**);
void printArray(Train**);
void readInput(char*,Train**,int*);
void* load_controller(void*);
//int peek(Node*);

//Mutexes
pthread_mutex_t mutex_load = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_dispatch = PTHREAD_MUTEX_INITIALIZER; //Train will lock this if ready to get sent to PQ
pthread_mutex_t mutex_cross = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_train_data = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_queue = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_done = PTHREAD_MUTEX_INITIALIZER;

//Convars
pthread_cond_t convar_begin_loading = PTHREAD_COND_INITIALIZER;
pthread_cond_t convar_dispatch = PTHREAD_COND_INITIALIZER;
pthread_cond_t convar_input_finished = PTHREAD_COND_INITIALIZER;
pthread_cond_t convar_done = PTHREAD_COND_INITIALIZER;

//Attributes
pthread_attr_t thread_attr; //also joinable
pthread_attr_t thread_attr_joinable;

//Semaphores
sem_t semaphore_all_threads_dead;
sem_t semaphore_trains_live;

//Globals and definitions
int num_live_threads = 0;
int num_loaded_trains = 0;
int train_count = 0;
int num_dispatched_trains = 0;
int sleptCount = 0;
int queue_count = 0;
int accum;


int main( int argc, char** argv ) {

	/* Initialize mutex and condition variable objects */
	pthread_attr_init(&thread_attr);
	pthread_attr_init(&thread_attr_joinable);
	pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_JOINABLE);
	pthread_attr_setdetachstate(&thread_attr_joinable, PTHREAD_CREATE_JOINABLE);
	
	int arr_size = 50;
	int i;
	Train* train_data;
	pthread_t* train_threads;
	pthread_t dispatcher_thread;
	pthread_t timer_thread;
	long t;
	int rc;
	void* status;
	
	if( argc < 2 ) {
		printf("[ERROR] Must have at least one argument (HINT: The name of a text file with train data!)\n");
		return 1;
	}
	//Read input file and allocate train structs to array
	readInput(argv[1],&train_data,&arr_size);

	//Clock thread
	pthread_create( &timer_thread, &thread_attr, timer, (void*)&accum );

	//Create all train threads
	initializeThreads(&train_data,&train_threads);	

	//Create dispatcher to poll loaded trains
	pthread_create( &dispatcher_thread, &thread_attr, dispatcher, (void*)&train_data );
	
	//INVALUABLE: https://computing.llnl.gov/tutorials/pthreads/samples/dotprod_mutex.c
	//Can get concurrency without having to call DETACH
	//by doing the join down here...wtf
	pthread_attr_destroy(&thread_attr);
	pthread_attr_destroy(&thread_attr_joinable);
	pthread_join( dispatcher_thread, NULL );
	for( i = 0; i < train_count; i++ ) {
		pthread_join( train_threads[i], NULL );
	}
	pthread_join( timer_thread, NULL );

	
	//AFTER JOINING (killing) CLEAN UP
	pthread_mutex_destroy(&mutex_load);
	pthread_mutex_destroy(&mutex_dispatch);
	pthread_mutex_destroy(&mutex_cross);
	pthread_mutex_destroy(&mutex_train_data);
	pthread_mutex_destroy(&mutex_queue);
	//Convars
	pthread_cond_destroy(&convar_begin_loading);
	pthread_cond_destroy(&convar_dispatch);
	pthread_cond_destroy(&convar_input_finished);
	free(&train_data[0]);
	free(&train_threads[0]);
	train_data = NULL;
	train_threads = NULL;
	pthread_exit(NULL);
}

void* timer( void* arg ) {
	//Wait for loading to begin, the convar is broadcasted
	pthread_mutex_lock(&mutex_load);
	while( num_live_threads < train_count ) {
		pthread_cond_wait( &convar_begin_loading, &mutex_load );
		
	}
	if( DEBUG ) printf("CLOCK START!\n");
	clock_gettime(CLOCK_MONOTONIC, &start);
	pthread_mutex_unlock(&mutex_load);
    pthread_exit((void*) 0);
}

/*Can be used to very accurate timestamps if required*/
void printTimeStamp() {
	clock_gettime( CLOCK_MONOTONIC, &current);
	float hours = fmodf(((((double)current.tv_sec + 1.0e-9*current.tv_nsec ) - ((double)start.tv_sec + 1.0e-9*start.tv_nsec )) / (float)3600.00), (float)24.00);
	float minutes = fmodf(((((double)current.tv_sec + 1.0e-9*current.tv_nsec ) - ((double)start.tv_sec + 1.0e-9*start.tv_nsec )) / (float)60.00), (float)60.00);
	float seconds = fmodf(((((double)current.tv_sec + 1.0e-9*current.tv_nsec ) - ((double)start.tv_sec + 1.0e-9*start.tv_nsec ))), (float)60.00);
	printf("%02.0f:%02.0f:%04.1f",hours,minutes,seconds);
}

/*Read in the train file*/
void readInput(char* filename, Train** train_data, int* arr_size) {
	char priority;
	int loading_time;
	int crossing_time;
	int i;
	(*train_data) = malloc(sizeof(Train)*(*arr_size)); //array of trains
	if( DEBUG ) printf("[DEBUG] Called readInput(), filename input was: \"%s\"\n", filename);
	FILE* in = fopen(filename, "r");
	while( fscanf( in, "%c %d %d *[\n]", &priority, &loading_time, &crossing_time ) != EOF ) {
		(*train_data)[train_count].direction = priority;
		(*train_data)[train_count].loading_time = loading_time;
		(*train_data)[train_count].crossing_time = crossing_time;
		(*train_data)[train_count].train_id = train_count;
		(*train_data)[train_count].live = LIVE;
		(*train_data)[train_count].ready = FALSE;
		(*train_data)[train_count].dispatched = FALSE;
		pthread_cond_init(&((*train_data)[train_count].convar_cross), NULL);
		switch(priority){
			case 'E': snprintf ( (*train_data)[train_count].dir, 5, "East"); break;
			case 'e': snprintf ( (*train_data)[train_count].dir, 5, "East"); break;
			case 'W': snprintf ( (*train_data)[train_count].dir, 5, "West"); break;
			case 'w': snprintf ( (*train_data)[train_count].dir, 5, "West"); break;
		}
		train_count++;
	}
}

void initializeThreads( Train** train_data, pthread_t** train_threads ) {

	(*train_threads) = malloc( sizeof(pthread_t)*train_count );
	int i;
	for( i = 0; i < train_count; i++ ) {
		pthread_mutex_lock(&mutex_load);
		if( DEBUG ) printf("Train %ld waiting to load, %d livethreads exist\n", (*train_data)[i].train_id, num_live_threads);
		num_live_threads++;
		pthread_create( &(*train_threads)[i], &thread_attr, train_function, (void*)&(*train_data)[i] );
		
		if( num_live_threads == train_count ) {
			pthread_cond_broadcast(&convar_begin_loading);
			pthread_cond_signal(&convar_dispatch);
		}
		pthread_mutex_unlock(&mutex_load);
	}
}

void* train_function( void* arg ) {

	Train *train_data = (Train*)arg;
	pthread_mutex_lock(&mutex_load);
		while( num_live_threads < train_count ) {
			pthread_cond_wait( &convar_begin_loading, &mutex_load );
			
		}
		if( DEBUG ) printf("Train %ld loading...\n", train_data->train_id);
	pthread_mutex_unlock(&mutex_load);
	usleep( train_data->loading_time * 100000 );
	if( DEBUG ) printf("Train %ld finished loading!\n", train_data->train_id);

	//Lock the queue mutex and add a new train entry to the appropriate queue
	pthread_mutex_lock(&mutex_queue);
	//Make new node
	//Node* temp = (Node*)malloc( sizeof(Node*) );
	Node* temp = malloc( sizeof(struct Node) );
	temp->train_index = train_data->train_id;
	temp->next = NULL;

	if( DEBUG ) printf("Adding train to queue: %d\n", temp->train_index);
	printTimeStamp();
	printf(" Train %ld is ready to go %s\n", train_data->train_id, train_data->dir);
	//Any loaded train goes into the loaded queue
	//Clearly, loaded trains get sent to their respective queues first
	if( train_data->direction == 'E') {
		if( Eb_q != NULL ) {
			Node* cur = Eb_q;
			while( cur->next != NULL ) {
				cur = cur->next;
			}
			cur->next = temp;
		}
		else {
			Eb_q = temp;
		}
	}
	else if( train_data->direction == 'W') {
		if( Wb_q != NULL ) {
			Node* cur = Wb_q;
			while( cur->next != NULL ) {
				cur = cur->next;
			}
			cur->next = temp;
		}
		else {
			Wb_q = temp;
		}
	}
	else if( train_data->direction == 'e') {
		if( eb_q != NULL ) {
			Node* cur = eb_q;
			while( cur->next != NULL ) {
				cur = cur->next;
			}
			cur->next = temp;
		}
		else {
			eb_q = temp;
		}

	}
	else { //its w
		if( wb_q != NULL ) {
			Node* cur = wb_q;
			while( cur->next != NULL ) {
				cur = cur->next;
			}
			cur->next = temp;
		}
		else {
			wb_q = temp;
		}
	}
	//to prevent spurious wakeup of dispatch convar
	queue_count++;
	//There are trains in the queues, signal dispatcher
	pthread_cond_signal(&convar_dispatch);
	pthread_mutex_unlock(&mutex_queue);

	//WAIT HERE FOR DISPATCHER TO SIGNAL GREEN LIGHT
	pthread_mutex_lock(&mutex_cross);
	while( !train_data->dispatched ) {
		pthread_cond_wait( &(train_data->convar_cross), &mutex_cross );
	}
	//Critical section here
	printTimeStamp();
	printf(" Train %ld is ON the main track going %s\n", train_data->train_id, train_data->dir);
	usleep( train_data->crossing_time * 100000 );
	printTimeStamp();
	printf(" Train %ld is OFF the main track after going %s\n", train_data->train_id, train_data->dir);
	num_dispatched_trains++;
	pthread_mutex_unlock(&mutex_cross);
	free(temp);
	pthread_exit((void*) 0);
}

//Poll loaded trains for dispatch
void* dispatcher( void* arg ) {

	//Wait until we get the greenlight from trains
	pthread_mutex_lock(&mutex_dispatch);
	while( queue_count < 1 ) {
		pthread_cond_wait(&convar_dispatch,&mutex_dispatch);	
	}
	pthread_mutex_unlock(&mutex_dispatch);

	Train** train_data = (Train**)arg;

	/* Sim logic pseudo
	   -If only one train in queue, cross
	   -If > 1 train in queue, higher priority first
	   -If > 1 train in queue, same priority, 


	*/

	while(num_dispatched_trains < train_count) {
		int next_train;

		pthread_mutex_lock(&mutex_queue);
		//If EB high pri train, and other high-pri queue is empty, always send high pri over low pri
		if( Eb_q != NULL && Wb_q == NULL ) {
			next_train = Eb_q->train_index;
			if( DEBUG ) printf("Popped from Eb_q: %d\n", next_train);
			Eb_q = Eb_q->next;
		}
		//Same direction, same pri, Eb has priority (FOR NOW!)
		else if( Eb_q != NULL && Wb_q != NULL ) {
			next_train = Eb_q->train_index;
			if( DEBUG ) printf("Popped from Eb_q: %d\n", next_train);
			Eb_q = Eb_q->next;
		}
		//Conversely (send Wb high-pri over low-pri trains)
		else if( Wb_q != NULL && Eb_q == NULL ) {
			next_train = Wb_q->train_index;
			if( DEBUG ) printf("Popped from Wb_q: %d\n", next_train);
			Wb_q = Wb_q->next;
		}
		//Conversely (send Wb high-pri over low-pri trains)
		else if( Wb_q != NULL && Eb_q != NULL ) {
			next_train = Wb_q->train_index;
			if( DEBUG ) printf("Popped from Wb_q: %d\n", next_train);
			Wb_q = Wb_q->next;
		}
		else if( eb_q != NULL ) {
			next_train = eb_q->train_index;
			if( DEBUG ) printf("Popped from eb_q: %d\n", next_train);
			eb_q = eb_q->next;
		}
		else if( wb_q != NULL ) {
			next_train = wb_q->train_index;
			if( DEBUG ) printf("Popped from wb_q: %d\n", next_train);
			wb_q = wb_q->next;
		}
		pthread_mutex_unlock(&mutex_queue);

		pthread_mutex_lock(&mutex_cross);
		(*train_data)[next_train].dispatched = TRUE;
		pthread_cond_signal( &(*train_data)[next_train].convar_cross );
		pthread_mutex_unlock(&mutex_cross);	
	}
	//Timer can stop
	pthread_mutex_lock(&mutex_done);
	pthread_cond_signal(&convar_done);
	pthread_mutex_unlock(&mutex_done);
	
	pthread_exit((void*) 0);
}

void printArray( Train** train_data ) {
		int i;
		for( i = 0; i < train_count; i++ ) {
		if(DEBUG) printf( "[DEBUG] [READING TRAIN %.3ld with PRI: %c, LT: %ld, CT: %ld, LIVE: %ld]\n", 
			(*train_data)[i].train_id, 
			(*train_data)[i].direction, 
			(*train_data)[i].loading_time, 
			(*train_data)[i].crossing_time,
			(*train_data)[i].live );
	}
}



// void printQueue( Node* tail ) {
// 	if( (*tail) ) {
// 		if( DEBUG) printf("[printQueue()]QUEUE: Tail to head: ");
// 		Node cur = NULL;
// 		//Cur should be the dereferenced tail otherwise it will modify the pointer
// 		//to the original tail (very annoying to debug)
// 		cur = (*tail);
// 		while( cur != NULL ) {
// 			if( cur->p_next == NULL ) printf("%ld", cur->data);
// 			else printf("%ld(next)->", cur->data);
// 			cur = cur->p_next;
// 		}
// 		printf("\n");
// 	}
// 	else {
// 		printf("CANNOT PRINT! Nothing in queue\n");	
// 	}

	
	
// }

// void printQueueReverse( Node* head ) {

// 	if( (*head) ) {
// 		if( DEBUG) printf("[printQueueReverse()]QUEUE: Head to tail: ");
// 		Node cur = NULL;
// 		//Cur should be the dereferenced tail otherwise it will modify the pointer
// 		//to the original tail (very annoying to debug)
// 		cur = (*head);
// 		while( cur != NULL ) {
// 			if( cur->p_prev == NULL ) printf("%ld", cur->data);
// 			else printf("%ld(prev)->", cur->data);
// 			cur = cur->p_prev;
// 		}
// 		printf("\n");	
// 	}
// 	else {
// 		printf("CANNOT PRINT! Nothing in queue\n");
// 	}
	
// }

// int peek( Node* head ) {
// 	if( (*head) ) {
// 		printf("Element: %ld exists at head\n", (*head)->data);
// 		return 1;
// 	}
// 	else {
// 		return 0;
// 	}
// }

// int dequeue( Node* head, Node* tail ) {
// 	if( (*head) ) {
// 		if( DEBUG) printf("[deqeue()]Head was: %ld\n", (*head)->data);
// 		//Set tempHead to head
// 		int tempHeadData = (*head)->data;
// 		Node tempHead = (*head);
// 		//If head and head-prev exists, set new head to head-prev
// 		if( (*head)->p_prev ) {
// 			(*head) = (*head)->p_prev;
// 			(*head)->p_next = NULL;
// 			if( DEBUG) printf("[deqeue()]Head now: %ld\n", (*head)->data);
// 		}
// 		//Else, just the head exists
// 		else {
// 			(*head)->p_prev = NULL;
// 			(*head)->p_next = NULL;
// 			(*tail)->p_next = NULL;
// 			(*tail)->p_prev = NULL;
// 			// (*head) = NULL;
// 			// (*tail) = NULL;
// 			if( DEBUG) printf("[deqeue()]Head AND tail null\n");
// 		}
// 		//Deallocate head
// 		ebLP_queue_count--;
// 		free(tempHead);
// 		return tempHeadData;
// 	}
// 	else {
// 		if(DEBUG) printf("Queue empty, cannot dequeue!\n");
// 		return -1;
// 	}
// }

/*
*/
// void enqueue( Node* head, Node* tail, long* data ) {

// 	Node temp;
// 	temp = (Node)malloc( sizeof(struct Queue) );
// 	temp->data = (*data);
// 	temp->p_prev = NULL;
// 	temp->p_next = NULL;

// 	if( (*head) == NULL ) {
// 		if( DEBUG) printf("Head was NULL, adding node %ld\n", (*data));
// 		(*head) = temp;
// 	}
// 	else {
// 		if( (*tail) == NULL ) {
// 			if( DEBUG) printf("Tail was NULL, adding node %ld\n", (*data));
// 			temp->p_next = (*head);
// 			(*tail) = temp;
// 			(*head)->p_prev = (*tail);
// 		}
// 		else {
// 			if( DEBUG) printf("Tail was NOT null, adding node %ld\n", (*data));
// 			(*tail)->p_prev = temp;
// 			temp->p_next = (*tail);
// 			(*tail) = temp;
// 		}
// 	}
// }