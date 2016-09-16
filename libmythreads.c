/*
 * Kyra Koch
 * 10/20/2015
 * Project 2
 *
 * A library for threading.
 * Very rudimentary, and it is highly suggested to use
 * a more official API.
 */
#include "mythreads.h"
#include <stdlib.h>
#include <sys/queue.h>
#include <stdio.h>
#include <assert.h>

#define TRUE 1
#define FALSE 0

void doFn(thFuncPtr funcPtr, void *argPtr, int id);
static void interruptDisable();
static void interruptEnable();
void cleanQ();

struct threadNode
{
    TAILQ_ENTRY(threadNode) tailq;
    int ID;
    int exited;
    ucontext_t context;
};

struct result
{
    TAILQ_ENTRY(result) tailq;
    int ID;
    void *fnresult;
};

typedef struct lock
{
    int ID;
    int inUse;
    int conditions[CONDITIONS_PER_LOCK];
}Lock_t;

int list_size;                  //count of the num threads made
int dead;                       //count of the num dead threads
struct threadNode *currentThread;//link to ther current thread
TAILQ_HEAD(tailhead, threadNode);
struct tailhead head;           //head of running threads queue
TAILQ_HEAD(resulthead, result);
struct resulthead results;      //head of the results queue
TAILQ_HEAD(deadthread, threadNode);
struct deadthread deadthreadhead;//head of the dead threads queue
Lock_t *locks[NUM_LOCKS];
int interruptsAreDisabled;

//Initialies the threading library
void threadInit()
{
    TAILQ_INIT(&head);          //initializes the thread queue
    TAILQ_INIT(&results);       //initializes the results queue
    TAILQ_INIT(&deadthreadhead);//initializes the dead threads queue

    //sets up the head node to be the main thread
    struct threadNode *mainThread = malloc(sizeof(struct threadNode));
    mainThread->ID = 0;
    mainThread->exited = FALSE;
    
    //sets up the main thread's result (it won't have one)
    struct result *mainRes = malloc(sizeof(struct result));
    mainRes->ID = 0;
    
    //inserts the head in the thread queue and the result in result queue
    TAILQ_INSERT_HEAD(&head, mainThread, tailq);
    TAILQ_INSERT_HEAD(&results, mainRes, tailq);
    
    list_size = 1;          //there is one item in the thread queue
    currentThread = mainThread;
    
    //initilizes the lock array
    for(int i = 0; i<NUM_LOCKS; i++)
    {
        Lock_t *lock = malloc(sizeof(Lock_t));
        lock->ID = i;
        lock->inUse = FALSE;
        for(int i = 0; i < CONDITIONS_PER_LOCK; i++)
            lock->conditions[i] = FALSE;
        locks[i] = lock;
    }
    interruptEnable();
}

//creates a thread and eventually returns the ID of the newly created thread
int threadCreate(thFuncPtr funcPtr, void *argPtr)
{
    interruptDisable();
    //the new node and it's corresponding result node
    struct threadNode *node = malloc(sizeof(struct threadNode));
    struct result *nodeRes = malloc(sizeof(struct result));
    struct threadNode *curr = currentThread;

    //initializes the new node's stack and info
    node->context.uc_stack.ss_sp = malloc(STACK_SIZE);
    node->context.uc_stack.ss_size = STACK_SIZE;
    node->context.uc_stack.ss_flags = 0;
    node->ID = list_size;
    node->exited = FALSE;

    //initializes the new node's result
    nodeRes->ID = list_size;

    list_size++;            //increments the total number of threads

    //inserts the head in the thread queue and the result in result queue
    TAILQ_INSERT_TAIL(&head, node, tailq);
    TAILQ_INSERT_TAIL(&results, nodeRes, tailq);

    currentThread = node;
    getcontext(&node->context);
    makecontext(&node->context, (void(*)(void))doFn, 3, funcPtr, argPtr, node->ID);
    swapcontext(&curr->context, &node->context);

    interruptEnable();
    //when the fn finishes normally, will return the ID
    return node->ID;
}

void threadYield()
{
    interruptDisable();
    struct threadNode *newContext = TAILQ_FIRST(&head);
    struct threadNode *oldContext = currentThread;
    TAILQ_INSERT_TAIL(&head, oldContext, tailq);
    
    //tries to find a thread that is not exited
    while(newContext->exited)
    {
        struct threadNode *dead = newContext;
        newContext = TAILQ_NEXT(newContext, tailq);
        TAILQ_REMOVE(&head, dead, tailq);
        TAILQ_INSERT_HEAD(&deadthreadhead, dead, tailq);
        dead++;
    }

    //if there is another node to swap to
    if (newContext != NULL && newContext->ID != oldContext->ID) {
        TAILQ_REMOVE(&head, newContext, tailq);
        TAILQ_INSERT_TAIL(&head, newContext, tailq);
        currentThread = newContext;
        swapcontext(&oldContext->context, &newContext->context);
    }

    if(dead != 0)
        cleanQ();
    interruptEnable();
}

//waits for a specified thread to end
//then stores the result in passed in param
void threadJoin(int thread_id, void **result)
{
    interruptDisable();
    //checks if it's a valid thread ID
    if(thread_id >= list_size)
        return;
    
    //finds the thread with the corresponding ID
    struct threadNode *curr = TAILQ_FIRST(&deadthreadhead);
    while(curr == NULL)
    {
        threadYield();
        curr = TAILQ_FIRST(&deadthreadhead);
    }
    while(curr != NULL && thread_id != curr->ID)
    {
        curr = TAILQ_NEXT(curr, tailq);
        if(curr == NULL)
        {
            threadYield();
            curr = TAILQ_FIRST(&deadthreadhead);
        }
    }

    //else if exited, we update the results
    struct result *res = TAILQ_FIRST(&results);
    while(thread_id != res->ID)
        res = TAILQ_NEXT(res, tailq);
    *result = res->fnresult;

    interruptEnable();
    return;
}

//causes current thread to exit
void threadExit(void *result)
{
    interruptDisable();
    cleanQ();
    //current thread is exited and 
    //whatever result is given to us is the new result for that thread
    currentThread->exited = TRUE;
    struct result *res = TAILQ_FIRST(&results);
    while(currentThread->ID != res->ID)
        res = TAILQ_NEXT(res, tailq);
    res->fnresult = result;
   
    interruptEnable();
    //call threadyield to go to a new thread
    threadYield();
}

//atomically locks a specified lock
void threadLock(int lockNum)
{
    interruptDisable();
    //waits until a specific lock is not in use
    while(locks[lockNum]->inUse)
        threadYield();
    //sets the specified lock to inUse by current thread
    if(!locks[lockNum]->inUse)
        locks[lockNum]->inUse = TRUE;
    interruptEnable();
}

//atomically unlocks a specified lock
void threadUnlock(int lockNum)
{
    interruptDisable();
    //if it's already unlocked, exit
    if(!locks[lockNum]->inUse)
        return;
    else
        locks[lockNum]->inUse = FALSE;
    interruptEnable();
}

//causes the current thread of wait for a specfic condition
void threadWait(int lockNum, int conditionNum)
{
    interruptDisable();
    if(!locks[lockNum]->inUse)
    {
        perror("Lock is already unlocked!");
        return;
    }
    else
    {
        //thread waits for a specific condition
        while(!locks[lockNum]->conditions[conditionNum])
            threadYield();
    }
    interruptEnable();
}

//signals a specified condition
void threadSignal(int lockNum, int conditionNum)
{
    locks[lockNum]->conditions[conditionNum] = TRUE;
}

//a wrapper function to execute a user provided function
void doFn(thFuncPtr funcPtr, void *argPtr, int id)
{
    //stores the function result is the function actually finishes
    struct result *res = TAILQ_FIRST(&results);
    while(id != res->ID)
        res = TAILQ_NEXT(res, tailq);
    interruptEnable();
    
    res->fnresult = funcPtr(argPtr);
    interruptDisable();
    
    currentThread->exited = TRUE;
    
    //calls threadyield to go to a new thread
    threadYield();
    return;
}

//clears the memory of exited threads
void cleanQ()
{
    //printf("in clean q\n");
    struct threadNode *temp = TAILQ_FIRST(&deadthreadhead);
    TAILQ_FOREACH(temp, &deadthreadhead, tailq) {
        TAILQ_REMOVE(&deadthreadhead, temp, tailq);
        free(temp->context.uc_stack.ss_sp);
    }
    dead = 0;
}

static void interruptDisable()
{
    interruptsAreDisabled = TRUE;
}

static void interruptEnable()
{
    interruptsAreDisabled = FALSE;
}
