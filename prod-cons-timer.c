/*
 * FILE: prod-cons-timer.c
 * THMMY, 8th semester, Real Time Embedded Systems: 2nd assignment
 * Implementation of producer - consumer problem using timer
 * Author:
 *   Papadakis Charalampos, 9128, papadakic@ece.auth.gr
 *   Compile with gcc prod-cons-timer.c -o exe -lm -pthread -O3
 *   Used Raspberry Pi zero
 * It will create a queue with QUEUESIZE size and P / C
 */

#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <math.h>


// Defines for the queue and the prod/cons
#define QUEUESIZE 5
#define P 1
#define C 2
#define REPS 10
#define OPTION 1 //OPTION 0: Runs with defined Period , OPTION 1: Combined Version
#define PERIOD 100 //Period in ms


#define duration 3600 //It is setted to 3600 because we want the program to run for an hour to the raspberry


// Thread functions decleration
void * producer(void * args);
void * consumer(void * args);

//This is the function that the producer will put in the queue
void uselessFunc (void *x){
  int k = * (int * ) x;
  int result;
  for (int i=0; i<REPS; i++)
    result+=i*i;
};

typedef struct {
  void * ( * work)(void * );
  void * arg;
}
workFunction;

struct timeval arrTime[QUEUESIZE];

int doneProducers = 0;
struct timeval timeStamp;
double tempTime = 0;
int  misses=0; //how many jobs were lost-error


// The queue
typedef struct {
  workFunction buf[QUEUESIZE];
  long head, tail;
  int full, empty;
  pthread_mutex_t * mut;
  pthread_cond_t * notFull, * notEmpty;
}
queue;

typedef struct{
  queue *Q;
  pthread_mutex_t *argumentMut;
} arguments;


typedef struct{
    int period;
    int tasksToExecute;
    int startDelay;
    void * (*startFnc) (void *);
    void * (*stopFnc)  (void *);
    void * (*timerFnc) (void *);
    void * (*errorFnc) (void *);
    void *userData;
    queue *Q;
    void *(*producer)(void *arg);
    pthread_t tid;
} Timer;

//Metrix structs to calculate for time measurements


queue * queueInit(void);
void queueDelete(queue * q);
void queueAdd(queue * q, workFunction in );
void queueDel(queue * q, workFunction * out);

Timer *timerInit(int period, int tasksToExecute, int startDelay , queue *queue, void *(*producer)(void *arg),void *(*errorFnc)(), void * (*timerFnc) (void *););
void timerStop(Timer *T);
void startat(Timer *T, int year, int month, int day, int hour, int minute, int second);
void *error();

int main() {

//Available timer's period in mseconds

int period = PERIOD ;
int option = OPTION ;
int iterations = 0; //how many iterations in a time space of one hour

if(option==0){
  printf("The program will run for period %d ms\n",period);
  iterations = duration * (int)1e3 / period; //Calculate iterations
}
else if(option==1){
  printf("The program will run for period 1s , 0.1s , 0.01s combined\n");
  iterations = duration * (int)1e3 / 1000 +  duration * (int)1e3 / 100 +  duration * (int)1e3 / 10; //Calculate iterations
}
else{
  printf("Please don't forget to define OPTION 0 and the wanted period , or OPTION 1 if you want the combined version.");
}

  srand(time(NULL));
  queue * fifo;
  pthread_t *p, *c;

  fifo = queueInit(); //Queue Initialization
  if (fifo == NULL) {
    fprintf(stderr, "Main: Queue Initialization Failed.\n");
    exit(1);
  }

  p = (pthread_t *) malloc( P * sizeof(pthread_t) ); //allocating memory for P producer threads
  c = (pthread_t *) malloc( C * sizeof(pthread_t) ); //allocating memory for C consumer threads


  arguments *producerObj, *consumerObj; //will use them to pass needed objects/arguments to the created threads
  producerObj = (arguments *) malloc( P * sizeof(arguments) );
  consumerObj = (arguments *) malloc( C * sizeof(arguments) );

  Timer *T; // the Timer object

  if(p == NULL || c == NULL || producerObj == NULL || consumerObj == NULL ){
    fprintf(stderr, "Error at memory initialization.\n");
    exit(1);
  }

//Creating the consumer threads
  for (int i = 0; i < C; i++) {
    consumerObj[i].Q = fifo;
    consumerObj[i].argumentMut = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init(consumerObj[i].argumentMut, NULL);
    pthread_create( &c[i], NULL, consumer, (void *)(consumerObj + i));
  }

//Creating the producer threads after checking which option we are running
if(option==0){
  T = (Timer *)malloc(sizeof(Timer));
  T = timerInit(period,iterations , 0, fifo , producer, error , uselessFunc );
  pthread_create(&T->tid, NULL, T->producer, T);
}
else if(option == 1){
  T = (Timer *)malloc(3 * sizeof(Timer));

  T[0] = *timerInit(1000,duration * (int)1e3 / 1000 , 0, fifo , producer,error,uselessFunc);
  T[1] = *timerInit(100,duration * (int)1e3 / 100 , 0, fifo , producer,error,uselessFunc);
  T[2] = *timerInit(10,duration * (int)1e3 / 10 , 0, fifo , producer,error,uselessFunc);

  for(int i=0; i<3; i++){
     pthread_create(&(T+i)->tid, NULL, (T+i)->producer, T+i);
  }
}


//Waiting for the threads to join
  for (int i = 0; i < P; i++) {
    pthread_join(p[i], NULL);
  }

  for (int i = 0; i < C; i++) {
    pthread_join(c[i], NULL);
  }

  queueDelete(fifo);
  free(T);

  return 0;
}

//Producer thread function
void * producer(void * q) {

  queue * fifo;
  int i ,tid ;
  Timer *T = (Timer *)q;
  fifo = T->Q ;
  int  argument;
  double totalDrift = 0;
  struct timeval  timeValue  , producerBegin , producerEnd ;
  double addition , additionNext ;

  sleep(T->startDelay);
  gettimeofday(&timeValue, NULL);
  addition = 1e6* timeValue.tv_sec + timeValue.tv_usec;
  additionNext = addition; //we have no drift at first


  for (i = 0; i < T->tasksToExecute; i++) {
    pthread_mutex_lock(fifo -> mut);
    gettimeofday(&producerBegin, NULL);

    while (fifo -> full) {
      printf("The queue is full \n");
      //Error function execution
      T->errorFnc;
      pthread_cond_wait(fifo -> notFull, fifo -> mut);
    }
    gettimeofday(&timeValue, NULL);
    addition = 1e6*timeValue.tv_sec + timeValue.tv_usec;

    //Workfunction object to add at the queue
    workFunction wf;

    argument = rand() % 2;
    wf.arg  = &argument;
    wf.work = T->timerFnc;



    //Getting the arrival time at the queue
    gettimeofday(&(arrTime[(fifo->tail)]),NULL);
    gettimeofday(&producerEnd, NULL);

    queueAdd(fifo, wf);

    //Calculate the time taken for a producer to add a job to the queue
    int producerTime = (producerEnd.tv_sec-producerBegin.tv_sec)*(int)1e6 + producerEnd.tv_usec-producerBegin.tv_usec;
    printf("Time it took for producer to add job to the queue : %d  \n " , producerTime);
    pthread_mutex_unlock(fifo -> mut);
    pthread_cond_signal(fifo -> notEmpty);


    double drift = addition - additionNext;//calculating the drift time
    printf("Drift time : %d \n " , (int)drift);
    double sleepDur = T->period - (drift/(int)1e3); //calculating needed sleep time
    if(sleepDur > 0){
      usleep(sleepDur*(int)1e3); //forcing producer to sleep for the calculated time space
    }

    //Update time value
    gettimeofday(&timeValue, NULL);
    additionNext = 1e6*timeValue.tv_sec + timeValue.tv_usec;
  }


  doneProducers++;
  //Terminating if the consumers are done
  if (doneProducers == P) {
    pthread_cond_broadcast(fifo -> notEmpty);
  }

  return (NULL);
}

// Consumer thread function
void * consumer(void * q) {

  queue * fifo;
  int i, d ;


  int waitingTime ;
  struct timeval  workBegin, workEnd;

  arguments *consumerObj;
  consumerObj = (arguments *)q ;
  fifo = consumerObj->Q;


  while (1) {


    pthread_mutex_lock(fifo -> mut);

    while (fifo -> empty == 1 && doneProducers != P) {

      //printf("The queue is empty \n");
      pthread_cond_wait(fifo -> notEmpty, fifo -> mut);
    }
    //Termination flag for the consumers
    if (doneProducers == P && fifo -> empty == 1) {
      pthread_mutex_unlock(fifo -> mut);
      pthread_cond_broadcast(fifo -> notEmpty);
      break;
    }

    workFunction wf;
    struct timeval removeT;
    gettimeofday(&removeT,NULL);
    //Calculating the waiting time at the queue
    waitingTime= (removeT.tv_sec -(arrTime[fifo->head]).tv_sec) *1e6 + (removeT.tv_usec-(arrTime[fifo->head]).tv_usec) ;
    printf("The waiting time is : %d  \n " , waitingTime);
    queueDel(fifo, &wf);

    pthread_mutex_unlock(fifo -> mut);
    pthread_cond_signal(fifo -> notFull);
    //Executing the work function outside the mutexes
    gettimeofday(&workBegin,NULL);
    wf.work(wf.arg);
    gettimeofday(&workEnd,NULL);

    pthread_mutex_lock(consumerObj->argumentMut);
    int jobDur = (workEnd.tv_sec-workBegin.tv_sec)*(int)1e6 + workEnd.tv_usec-workBegin.tv_usec;
    printf("Execution time is  : %d  \n " , jobDur);
    pthread_mutex_unlock(consumerObj->argumentMut);


  }
//  pthread_cond_signal (fifo->notEmpty);
  return (NULL);
}

//Queue function implementations
queue * queueInit(void) {
  queue * q;

  q = (queue * ) malloc(sizeof(queue));
  if (q == NULL) return (NULL);

  q -> empty = 1;
  q -> full = 0;
  q -> head = 0;
  q -> tail = 0;
  q -> mut = (pthread_mutex_t * ) malloc(sizeof(pthread_mutex_t));
  pthread_mutex_init(q -> mut, NULL);
  q -> notFull = (pthread_cond_t * ) malloc(sizeof(pthread_cond_t));
  pthread_cond_init(q -> notFull, NULL);
  q -> notEmpty = (pthread_cond_t * ) malloc(sizeof(pthread_cond_t));
  pthread_cond_init(q -> notEmpty, NULL);

  return (q);
}

void queueDelete(queue * q) {
  pthread_mutex_destroy(q -> mut);
  free(q -> mut);
  pthread_cond_destroy(q -> notFull);
  free(q -> notFull);
  pthread_cond_destroy(q -> notEmpty);
  free(q -> notEmpty);
  free(q);
}

void queueAdd(queue * q, workFunction in ) {
  q -> buf[q -> tail] = in ;
  q -> tail++;
  if (q -> tail == QUEUESIZE)
    q -> tail = 0;
  if (q -> tail == q -> head)
    q -> full = 1;
  q -> empty = 0;

  return;
}

void queueDel(queue * q, workFunction * out) {
  * out = q -> buf[q -> head];

  q -> head++;
  if (q -> head == QUEUESIZE)
    q -> head = 0;
  if (q -> head == q -> tail)
    q -> empty = 1;
  q -> full = 0;

  return;
}

//initializing timer
Timer *timerInit(int period, int tasksToExecute, int startDelay , queue *queue, void *(*producer)(void *arg),void *(*errorFnc)(), void * (*timerFnc) (void *)){
    // printf("Initializing Timer\n");
    Timer *T = (Timer *) malloc( sizeof(Timer) );
    T->period = period;
    T->tasksToExecute = tasksToExecute;
    T->startDelay = startDelay;
    T->Q = queue;
    T->producer = producer;
    T->errorFnc = errorFnc;
    T->timerFnc = timerFnc;
    T->startFnc = NULL;
    T->userData = NULL;

    return T;
}

//creating threads at specific moment
void startat(Timer *T, int year, int month, int day, int hour, int minute, int second){
    int delay = 0;
    time_t currTime = time(NULL);
    struct tm currMoment;
    currMoment.tm_year = year;
    currMoment.tm_mon = month;
    currMoment.tm_mday = day;
    currMoment.tm_hour = hour;
    currMoment.tm_min = minute;
    currMoment.tm_sec = second;

    delay = (int) difftime(currTime, mktime(&currMoment));

    if (delay<0) {
      delay=0;
    }
    T->startDelay = delay;
    pthread_create(&T->tid, NULL, T->producer, T);
}

void *error() {
    misses++;
}
