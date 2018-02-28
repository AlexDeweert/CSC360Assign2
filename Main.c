#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/queue.h>
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
	long loading_time;
	long crossing_time;
	long train_id;
	long live;
	long ready;
	long dispatched;
	pthread_cond_t convar_cross;
	pthread_cond_t convar_gone;
	//TAILQ_ENTRY(Trains) loaded_queue_entries;
	//TAILQ_ENTRY(Trains) ebhp_queue_entries;
} Train;

typedef struct Queue {
	struct Queue* p_next;
	struct Queue* p_prev;
	long data;
} Queue;

typedef Queue* Node;

// //Tailqueue from "man queue" example
// struct tailqueue_entry {
// 	int train_id;
// 	TAILQ_ENTRY(tailqueue_entry) entries;
// };

//Initializing tailqueue head
//TAILQ_HEAD(, Trains) loaded_queue_head;
//TAILQ_HEAD(, Trains) ebhp_queue_head;

//Prototype
int dequeue( Node* head, Node* tail );
void printQueue(Node*);
void printQueueReverse(Node*);
void enqueue(Node*,Node*,long*);
void* dispatcher(void*);
void* train_function(void*);
void initializeThreads(Train**,pthread_t**);
void printArray(Train**);
void readInput(char*,Train**,int*);
void* load_controller(void*);
void mutexTest( long id );

//Mutexes
pthread_mutex_t mutex_load;
pthread_mutex_t mutex_dispatch; //Train will lock this if ready to get sent to PQ
pthread_mutex_t mutex_loaded_queue;
pthread_mutex_t mutex_cross;
pthread_mutex_t mutex_post_loading_wait;
pthread_mutex_t mutex_increment_loaded_train_count;
pthread_mutex_t mutex_train_data;
pthread_mutex_t mutex_ebHP_queue;
pthread_mutex_t mutex_ebLP_queue;
pthread_mutex_t mutex_wbHP_queue;
pthread_mutex_t mutex_wbLP_queue;

//Convars
pthread_cond_t convar_begin_loading;
pthread_cond_t convar_dispatch; //The train will use this to signal the dispatcher
pthread_cond_t convar_dispatcher_add_to_queue;
pthread_cond_t convar_track_free;
pthread_cond_t convar_input_finished;

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

//Queue heads & tails
Node eb_LP_head;
Node eb_LP_tail;

Node eb_HP_head;
Node eb_HP_tail;

Node wb_LP_head;
Node wb_LP_tail;

Node wb_HP_head;
Node wb_HP_tail;

int main( int argc, char** argv ) {

	/* Initialize mutex and condition variable objects */
	pthread_mutex_init(&mutex_load, NULL);
	pthread_mutex_init(&mutex_increment_loaded_train_count, NULL);
	pthread_mutex_init(&mutex_dispatch, NULL);
	pthread_mutex_init(&mutex_cross, NULL);
	pthread_mutex_init(&mutex_loaded_queue, NULL);
	pthread_mutex_init(&mutex_post_loading_wait, NULL);
	pthread_mutex_init(&mutex_ebHP_queue, NULL);
	pthread_mutex_init(&mutex_wbHP_queue, NULL);
	pthread_mutex_init(&mutex_ebLP_queue, NULL);
	pthread_mutex_init(&mutex_wbLP_queue, NULL);
	pthread_mutex_init(&mutex_train_data, NULL);
	pthread_cond_init(&convar_begin_loading, NULL);
	pthread_cond_init(&convar_dispatch, NULL);
	pthread_cond_init(&convar_track_free, NULL);
	pthread_cond_init(&convar_dispatcher_add_to_queue, NULL);
	pthread_cond_init(&convar_input_finished, NULL);
	pthread_attr_init(&thread_attr);
	pthread_attr_init(&thread_attr_joinable);
	pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_JOINABLE);
	pthread_attr_setdetachstate(&thread_attr_joinable, PTHREAD_CREATE_JOINABLE);
	
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
	readInput(argv[1],&train_data,&arr_size);

	//Create all train threads
	initializeThreads(&train_data,&train_threads);	

	//Create dispatcher to poll loaded trains
	pthread_create( &dispatcher_thread, &thread_attr_joinable, dispatcher, (void*)&train_data );
	
	//INVALUABLE: https://computing.llnl.gov/tutorials/pthreads/samples/dotprod_mutex.c
	//Can get concurrency without having to call DETACH
	//by doing the join down here...wtf
	pthread_attr_destroy(&thread_attr);
	pthread_join( dispatcher_thread, NULL );
	for( i = 0; i < train_count; i++ ) {
		pthread_join( train_threads[i], NULL );
	}

	//printLoadedQueue();

	//AFTER JOINING (killing) CLEAN UP
	pthread_mutex_destroy(&mutex_load);
	pthread_cond_destroy(&convar_begin_loading);
	free(&train_data[0]);
	free(&train_threads[0]);
	train_data = NULL;
	train_threads = NULL;
	pthread_exit(NULL);
}

/*Read in the train file*/
void readInput(char* filename, Train** train_data, int* arr_size) {

	// pthread_mutex_lock(&mutex_train_data);
	char priority;
	int loading_time;
	int crossing_time;
	int i;
	(*train_data) = malloc(sizeof(Train)*(*arr_size)); //array of trains
	if(DEBUG) printf("[DEBUG] Called readInput(), filename input was: \"%s\"\n", filename);
	FILE* in = fopen(filename, "r");
	while( fscanf( in, "%c %d %d *[\n]", &priority, &loading_time, &crossing_time ) != EOF ) {
		(*train_data)[train_count].direction = priority;
		(*train_data)[train_count].loading_time = loading_time;
		(*train_data)[train_count].crossing_time = crossing_time;
		(*train_data)[train_count].train_id = train_count;
		(*train_data)[train_count].live = LIVE;
		(*train_data)[train_count].ready = FALSE;
		(*train_data)[train_count].dispatched = FALSE;
		// pthread_cond_init(&((*train_data)[train_count].convar_eb_ready), NULL);
		// pthread_cond_init(&((*train_data)[train_count].convar_Eb_ready), NULL);
		// pthread_cond_init(&((*train_data)[train_count].convar_Wb_ready), NULL);
		// pthread_cond_init(&((*train_data)[train_count].convar_wb_ready), NULL);
		pthread_cond_init(&((*train_data)[train_count].convar_gone), NULL);
		pthread_cond_init(&((*train_data)[train_count].convar_cross), NULL);
		train_count++;
	}
	// pthread_cond_broadcast(&convar_input_finished);
	// pthread_mutex_unlock(&mutex_train_data);
}

void initializeThreads( Train** train_data, pthread_t** train_threads ) {

	// pthread_mutex_lock(&mutex_train_data);
	// while( num_live_threads < train_count ) {
	// 	pthread_cond_wait(&convar_input_finished,&mutex_train_data);
	// }
	// pthread_mutex_unlock(&mutex_train_data);

	(*train_threads) = malloc( sizeof(pthread_t)*train_count );
	int i;
	for( i = 0; i < train_count; i++ ) {
		pthread_mutex_lock(&mutex_load);
		printf("Train %ld waiting to load, %d livethreads exist\n", (*train_data)[i].train_id, num_live_threads);
		num_live_threads++;
		pthread_create( &(*train_threads)[i], &thread_attr, train_function, (void*)&(*train_data)[i] );
		
		if( num_live_threads == train_count ) {
			pthread_cond_broadcast(&convar_begin_loading);
		}
		pthread_mutex_unlock(&mutex_load);
	}
}

void* train_function( void* arg ) {

	// pthread_mutex_lock(&mutex_train_data);
	// while( num_live_threads < train_count ) {
	// 	pthread_cond_wait(&convar_input_finished,&mutex_train_data);
	// }
	// pthread_mutex_unlock(&mutex_train_data);

	Train *train_data = (Train*)arg;
	pthread_mutex_lock(&mutex_load);
		while( num_live_threads < train_count ) {
			pthread_cond_wait( &convar_begin_loading, &mutex_load );
			
		}
		printf("Train %ld loading...\n", train_data->train_id);
	pthread_mutex_unlock(&mutex_load);
	usleep( train_data->loading_time * 100000 );
	printf("Train %ld finished loading!\n", train_data->train_id);

	//Any loaded train goes into the loaded queue
	//Clearly, loaded trains get sent to their respective queues first
	if( train_data->direction == 'E') {
		pthread_mutex_lock( &mutex_ebHP_queue );
		enqueue( &eb_HP_head,&eb_HP_tail,&(train_data->train_id) );
		//train_data->ready = TRUE;
		//pthread_cond_signal(&(train_data->convar_Eb_ready));
		pthread_mutex_unlock( &mutex_ebHP_queue );

	}
	else if( train_data->direction == 'W') {
		pthread_mutex_lock( &mutex_wbHP_queue );
		enqueue( &wb_HP_head,&wb_HP_tail,&(train_data->train_id) );
		//train_data->ready = TRUE;
		//pthread_cond_signal(&(train_data->convar_Wb_ready));
		pthread_mutex_unlock( &mutex_wbHP_queue );

	}
	else if( train_data->direction == 'e') {
		pthread_mutex_lock( &mutex_ebLP_queue );
		enqueue( &eb_LP_head,&eb_LP_tail,&(train_data->train_id) );
		//train_data->ready = TRUE;
		//pthread_cond_signal(&(train_data->convar_eb_ready));
		pthread_mutex_unlock( &mutex_ebLP_queue );

	}
	else { //its w
		pthread_mutex_lock( &mutex_wbLP_queue );
		enqueue( &wb_LP_head,&wb_LP_tail,&(train_data->train_id) );
		//train_data->ready = TRUE;
		//pthread_cond_signal(&(train_data->convar_wb_ready));
		pthread_mutex_unlock( &mutex_wbLP_queue );
	}


	//WAIT HERE FOR DISPATCHER TO SIGNAL GREEN LIGHT
	pthread_mutex_lock(&mutex_cross);
	while( train_data->ready ) {
		pthread_cond_wait( &(train_data->convar_cross), &mutex_cross );
	}
	//Critical section here
	printf("Train %ld CROSSING!\n", train_data->train_id);
	usleep( train_data->crossing_time * 100000 );
	pthread_mutex_unlock(&mutex_cross);

	// pthread_exit((void*) 0);
}

//Poll loaded trains for dispatch
void* dispatcher( void* arg ) {

	//Smhouldn't do anything until the trains are starting to load
	// pthread_mutex_lock(&mutex_load);
	// while( num_live_threads < train_count ) {
	// 	pthread_cond_wait( &convar_begin_loading, &mutex_load );
		
	// }
	printf("Dispatcher Ready...\n");
	// pthread_mutex_unlock(&mutex_load);

	Train** train_data = (Train**)arg;
	//DISPATCHER SHOULD SIGNAL WHEN THE NEXT TRAIN IS AVAILABLE TO CROSS
	//THE SIGNAL WILL INDICATE WHICH QUEUE HEAD CAN SEND THE NEXT TRAIN
	//AND THE TRAIN WILL LOCK THE TRACK WHEN IT GETS THE SIGNAL
	pthread_mutex_lock(&mutex_cross);
	while( num_dispatched_trains < num_loaded_trains ) {
		pthread_cond_wait(&convar_dispatch,&mutex_cross);
	}
	// Check condition of track here to give the correct train the green light (somehow)
	// Maybe check head of each queue based on simulation rules for priority,
	// Then signal the waiting trains convar that it can cross then update num_dispatched_trains
	if( eb_HP_head ) {
		printf("Tried this EB HP\n");
		pthread_cond_signal(&( (*train_data)[ dequeue(&eb_HP_head,&eb_HP_tail) ].convar_cross ));
		
	}
	else if( wb_HP_head ) {
		printf("Tried this WB HP\n");
		pthread_cond_signal(&( (*train_data)[ dequeue(&wb_HP_head,&wb_HP_tail) ].convar_cross ));
	}
	else if( eb_LP_head ) {
		printf("Tried this EB LP\n");
		pthread_cond_signal(&( (*train_data)[ dequeue(&eb_LP_head,&eb_LP_tail) ].convar_cross ));
	}
	else { //wb_LP_head
		printf("Tried this WB LP\n");

		pthread_cond_signal(&( (*train_data)[ dequeue(&wb_LP_head,&wb_LP_tail) ].convar_cross ));
	}
	num_dispatched_trains++;
	pthread_mutex_unlock(&mutex_cross);
}

void printQueue( Node* tail ) {
	if( (*tail) ) {
		if( DEBUG) printf("[printQueue()]QUEUE: Tail to head: ");
		Node cur = NULL;
		//Cur should be the dereferenced tail otherwise it will modify the pointer
		//to the original tail (very annoying to debug)
		cur = (*tail);
		while( cur != NULL ) {
			if( cur->p_next == NULL ) printf("%ld", cur->data);
			else printf("%ld(next)->", cur->data);
			cur = cur->p_next;
		}
		printf("\n");
	}
	else {
		printf("CANNOT PRINT! Nothing in queue\n");	
	}

	
	
}

void printQueueReverse( Node* head ) {

	if( (*head) ) {
		if( DEBUG) printf("[printQueueReverse()]QUEUE: Head to tail: ");
		Node cur = NULL;
		//Cur should be the dereferenced tail otherwise it will modify the pointer
		//to the original tail (very annoying to debug)
		cur = (*head);
		while( cur != NULL ) {
			if( cur->p_prev == NULL ) printf("%ld", cur->data);
			else printf("%ld(prev)->", cur->data);
			cur = cur->p_prev;
		}
		printf("\n");	
	}
	else {
		printf("CANNOT PRINT! Nothing in queue\n");
	}
	
}

int dequeue( Node* head, Node* tail ) {

	if( (*head) ) {
		if( DEBUG) printf("[deqeue()]Head was: %ld\n", (*head)->data);
		//Set tempHead to head
		int tempHeadData = (*head)->data;
		Node tempHead = (*head);
		//If head and head-prev exists, set new head to head-prev
		if( (*head)->p_prev ) {
			(*head) = (*head)->p_prev;
			(*head)->p_next = NULL;
			if( DEBUG) printf("[deqeue()]Head now: %ld\n", (*head)->data);
		}
		//Else, just the head exists
		else {
			(*head)->p_prev = NULL;
			(*head)->p_next = NULL;
			(*tail)->p_next = NULL;
			(*tail)->p_prev = NULL;
			(*head) = NULL;
			(*tail) = NULL;
			if( DEBUG) printf("[deqeue()]Head AND tail null\n");
		}
		//Deallocate head
		free(tempHead);
		return tempHeadData;	
	}
	//Return -1 if no items to be dequeued
	return QUEUE_EMPTY;
}

/*
*/
void enqueue( Node* head, Node* tail, long* data ) {

	Node temp;
	temp = (Node)malloc( sizeof(struct Queue) );
	temp->data = (*data);
	temp->p_prev = NULL;
	temp->p_next = NULL;

	if( (*head) == NULL ) {
		if( DEBUG) printf("Head was NULL, adding node %ld\n", (*data));
		(*head) = temp;
	}
	else {
		if( (*tail) == NULL ) {
			if( DEBUG) printf("Tail was NULL, adding node %ld\n", (*data));
			temp->p_next = (*head);
			(*tail) = temp;
			(*head)->p_prev = (*tail);
		}
		else {
			if( DEBUG) printf("Tail was NOT null, adding node %ld\n", (*data));
			(*tail)->p_prev = temp;
			temp->p_next = (*tail);
			(*tail) = temp;
		}
	}
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

void mutexTest( long id ) {
	sleptCount++;
	printf("Train: %ld called MUTEX TEST, sleptCount is: %d\n", id, sleptCount);
}

