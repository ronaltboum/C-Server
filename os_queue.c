#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>


typedef struct Node Node;
typedef struct Queue Queue;

struct Node {
    Node *next;
    int num;
};

struct Queue {
    Node *head;
    Node *tail;
    int queue_size;
    pthread_mutex_t Queue_Lock;
    pthread_cond_t isEmpty;  
};

//returns 0 if succeeds,  and error number if failed
int initialize_Queue(Queue *queue_ptr)
{
  
  queue_ptr->queue_size = 0;           
  queue_ptr -> head = NULL;
  queue_ptr -> tail = NULL;
  
  int ret_val = pthread_mutex_init( &(queue_ptr->Queue_Lock), NULL );
  if(ret_val !=0){
      return ret_val;  
  }
  ret_val = pthread_cond_init ( &(queue_ptr->isEmpty) , NULL);
  if(ret_val !=0){
      pthread_mutex_destroy( &(queue_ptr -> Queue_Lock) );
      return ret_val;  
  }
  
  return 0; 
}


//returns 0 on success, and error number on failure
int enqueue (Queue *queue_ptr, int number)
{
    Node *newNode = (Node*)malloc(sizeof(Node));
    if(newNode == NULL){
	return -1;
    }
    newNode -> num = number;
    newNode -> next = NULL;
    
    int lock_val = pthread_mutex_lock( &(queue_ptr->Queue_Lock) );
    if (lock_val !=0 )
    {
      
	free(newNode);
	return lock_val;
    }
    
    
    if(queue_ptr -> queue_size == 0){
      queue_ptr -> head = newNode;
      queue_ptr -> tail = newNode;
    }
    else{ 
      Node *tempNode = queue_ptr -> tail;
      tempNode -> next = newNode;
      queue_ptr -> tail = newNode;
    }
    
    ++(queue_ptr -> queue_size);
    int sig_ret = pthread_cond_signal ( &(queue_ptr -> isEmpty) );
    
    if(sig_ret !=0){
      pthread_mutex_unlock ( &(queue_ptr -> Queue_Lock) );
      return sig_ret;
    }
    int unlock_ret = pthread_mutex_unlock ( &(queue_ptr -> Queue_Lock) );
    if( unlock_ret !=0)
      return unlock_ret;
    
    return 0;
}


//returns 0 in case of success. Returns error number in case of failure
int dequeue (Queue *queue_ptr , int *num_val)
{
  int lock_val = pthread_mutex_lock( &(queue_ptr -> Queue_Lock) );
  if(lock_val !=0 )
    return lock_val;
  //while queue is empty: wait
  int wait_val;
  while(queue_ptr -> queue_size == 0 ){
      wait_val = pthread_cond_wait( &(queue_ptr -> isEmpty) , &(queue_ptr -> Queue_Lock) );
      if(wait_val != 0){
	pthread_mutex_unlock( &(queue_ptr -> Queue_Lock) );  //no need to check return value since we return the error of pthread_cond_wait
	return wait_val;
      }
  }
  
  //queue isn't empty now.
  Node *firstNode = queue_ptr -> head;
  queue_ptr -> head = firstNode -> next;
  if(queue_ptr -> head == NULL)
    queue_ptr -> tail = NULL;
  --(queue_ptr -> queue_size);
  
  int unlock_val = pthread_mutex_unlock( &(queue_ptr -> Queue_Lock) );
   *num_val = firstNode -> num;  //store the number value of the node we want to remove from the queue
  
  free(firstNode);
  return unlock_val;  //in case of success pthread_mutex_unlock returns 0.  Therefore, in case of success this function returns 0. 
  
}


//if the user allocated queue_ptr dynamically,  it's the user's responsiblity to free it
//returns 0 on success,  and error number on failure
int destroy_queue(Queue *queue_ptr)
{
    
    Node *currNode = queue_ptr -> head;
    Node *nextNode = queue_ptr -> head;
 
    while( currNode != NULL)
    {
      
	nextNode = currNode -> next;
	free(currNode);
	currNode = nextNode;
    }
    
    int mutex_val = pthread_mutex_destroy( &(queue_ptr -> Queue_Lock) );
    if(mutex_val != 0){
	return mutex_val;
     }
    
    int cond_ret = pthread_cond_destroy( &(queue_ptr -> isEmpty) );
    if(cond_ret != 0){  
      return cond_ret;
    }
    
    return 0;
    
}

