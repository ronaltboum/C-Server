#include "os_queue.c"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <fcntl.h> // for open flags
#include <time.h> 
#include <assert.h>
#include <signal.h>

// GLOBAL VARIABLES:
Queue *queue;
pthread_t *threadArr;  //Array of threads
int numThreads;  //number of threads
int listen_fd;
int sigint_flag = 0;  
//char *keyFile;


//UTILITY FUNCTIONS for  worker threads:

//xors the bits in order to encrypt
void perform_xor(char* input_buffer, char* key_buffer, char* result_buffer, int num_of_bytes)
{
  int j;
  for (j=0; j<num_of_bytes; j++)
    result_buffer[j] = input_buffer[j] ^ key_buffer[j];
}

//reads from key file
int read_key_file(int num_of_bytes_to_read , int key_file_handle, char* key_buffer )
{
  int num_of_bytes_read;
  int num_of_bytes_left;
  int bytes_cnt, lseek_val;

  num_of_bytes_read = 0;
  num_of_bytes_left = num_of_bytes_to_read;

  while (num_of_bytes_left > 0) {

    bytes_cnt = read(key_file_handle, key_buffer + num_of_bytes_read, num_of_bytes_left);

    if (bytes_cnt == -1){
	printf( "Error: read failed %s\n", strerror( errno ) );
	exit(-1);
    }
    if (bytes_cnt == 0) {
      lseek_val = lseek(key_file_handle, 0, SEEK_SET);
      if(lseek_val < 0){
	printf( "Error: lseek failed %s\n", strerror( errno ) );
	exit(-1);
      }
      continue;
    }

    num_of_bytes_read = num_of_bytes_read + bytes_cnt;
    num_of_bytes_left = num_of_bytes_left - bytes_cnt;
  } //closes while loop

  return 0;

}


//worker thread's routine:
void *work_routine (void *keypath)
{
    char input_from_client[1024];
    char key_buffer[1024];
    char result_buffer[1024];
    int socket_number_ptr;
    
    int key_file_handle = open( (char*)keypath, O_RDONLY );
    if (key_file_handle < 0){
	printf( "Error Opening Key File: %s\n", strerror( errno ) );
	exit(-1);
    }
    
    while(1)
    {
      
      int deq_val = dequeue(queue, &socket_number_ptr);
      if(deq_val != 0){
	printf( "Error: dequeue failed. Return code from dequeue is %d\n", deq_val);
	exit(-1);
      }
      
      int client_socket_fd = socket_number_ptr;
      //printf("client sockect_fd = %d\n", client_socket_fd);   
      if(client_socket_fd == -17){
	  //printf("exiting thread\n");  
	  close(key_file_handle);
	  return NULL;
      }
      
      //printf("The ID of this thread is: %u\n", (unsigned int)pthread_self());  
      memset(input_from_client, '0', 1024);  
      memset(key_buffer, '0', 1024);
      memset(result_buffer, '0', 1024);
      int total_read = 0;
      int read_bytes = 0;
      int is_client_open = 1;
      //lseek keyfile to beginning:
      int lseek_val = lseek(key_file_handle, 0, SEEK_SET);
      if(lseek_val < 0){
	printf( "Error: lseek failed %s\n", strerror( errno ) );
	exit(-1);
      }
      
      while(is_client_open)
      {
	read_bytes = recv(client_socket_fd , input_from_client + total_read, 1023 - total_read, 0);
	//printf("\nread_bytes = %d\n", read_bytes);
	if(read_bytes < 0)
	{
	    if(errno == ECONNRESET)  //A connection was forcibly closed by a peer
	      printf( "%s\n", strerror( errno ) );
	    else{
	      printf( "Error: recv funcion failed to read from client: %s\n", strerror( errno ) );
	    }
	    break;
	}
	
	else if(read_bytes > 0)
	{
	  input_from_client[total_read + read_bytes] = 0;
	  //read from key file:
	  if (read_key_file(read_bytes, key_file_handle, key_buffer) < 0) {
	    printf( "Error reading from key file: %s\n", strerror( errno ) );
	    exit(-1);
	  }

	  //xor the bits in order to encrypt and send back to client :
	  perform_xor(input_from_client + total_read, key_buffer, result_buffer, read_bytes);
	  //send encrypted text back to client:
	  int totalsent = 0;
	  int nsent = 0;
	  int notwritten = read_bytes;

	  while (notwritten > 0)
	  {
	    nsent = send(client_socket_fd, result_buffer + totalsent, notwritten, MSG_NOSIGNAL);  //MSG_NOSIGNAL flag is set to prevent SIGPIPE
	    if(nsent < 0)
	    {
		if(errno == ECONNRESET || errno == EPIPE ){
		  printf( "%s\n", strerror( errno ) );
		}
		else{
		printf( "Error in sending message to client: %s\n", strerror( errno ) );
		}
		is_client_open = 0;
		break;
	    }
	    
	    //printf("wrote %d bytes\n", nsent);
	    totalsent  += nsent;
	    notwritten -= nsent;
	  } //end of while send loop
	  
	 
	  total_read = total_read + read_bytes;
	}
	else if(read_bytes == 0){
	    //printf("finished reading from client\n");       
	    break;
	}
	 
	//printf("\ntotal_read = %d\n", total_read);
	if(total_read >= 1023){
	    total_read = 0;
	    memset(input_from_client, '0', sizeof(input_from_client));   
	}
      
      }//closes inner while loop
      
      close(client_socket_fd);
      
    
    } //closes outer while loop
    
}



//SIGINT handler:
static void handle_sigint (int sigNum)
{
 //setting a handler to ignore future SIGINT signals:
  struct sigaction future_sigint;
  memset(&future_sigint, '\0', sizeof(future_sigint));
  future_sigint.sa_handler = SIG_IGN;
  if(sigaction(SIGINT, &future_sigint, NULL) == -1){
    printf("Error : sigaction Failed. %s \n", strerror(errno));
    exit(-1);
  }
  
  close(listen_fd);  //stop listening for more clients
  
  sigint_flag = -17;
  int i, enq_val;
  for(i=0; i < numThreads; i++)
  {
    enq_val = enqueue(queue, -17);
    if(enq_val != 0){
	printf("Error : enqueue failed. Return code from enqueue is %d\n", enq_val);
	exit(-1);
    }
  }
  
  //wait for all threads to finish handling clients:
  int join_val;
  for(i=0; i < numThreads; i++)
  {
    join_val = pthread_join(threadArr[i], NULL);
    if (join_val != 0){
	printf("ERROR; return code from pthread_join() is %d\n", join_val);
	exit(-1);
    }
  }
  
  free(threadArr);
  //destroy queue:
  int destroyQ = destroy_queue(queue);
  if(destroyQ !=0){
    printf("Error : destroy_queue failed. Return code from destroy_queue is %d\n", destroyQ);
    exit(-1);
  }
  
  free(queue);
  exit(1);  //successful exit upon pressing ctrl-c
}


int main (int argc, char *argv[])
{
  
  struct sockaddr_in serv_addr, my_addr, peer_addr;
  int connfd , enq_val = 0;
  //args:  numThreads, port#, path to key
  if(argc != 4){
      printf("ERROR:  there should be 3 arguments\n");
      return -1;
  }
  
  char *keyFile = argv[3];
  int access_val;
  access_val = access (keyFile, F_OK);
  if (access_val != 0){ 
  	printf( "Error accessing Key File: %s\n", strerror( errno ) );
	return -1;
  }
 
 //check read access
 access_val = access (keyFile, R_OK);
 if (access_val != 0){ 
  	printf( "Error accessing Key File: %s\n", strerror( errno ) );
	return -1;
  }
 
 
  
  queue = (Queue*)malloc(sizeof(Queue));
  if(queue == NULL){
      printf("ERROR: malloc failed: %s\n", strerror( errno ) );
      return -1;
  }
  
  numThreads = atoi(argv[1]);
  int portNum = atoi(argv[2]);
  
  
  int initVal = initialize_Queue(queue);
  if(initVal != 0){
      printf("ERROR: initialize_Queue failed. Return code from initialize_Queue is %d\n", initVal);
      return -1;  
  }
  
  threadArr = (pthread_t*) malloc( numThreads * sizeof(pthread_t) );
  if(threadArr == NULL){
    printf("ERROR: malloc failed: %s\n", strerror( errno ) );
    return -1;
  }
  
  long i;
  int create_val;
  for(i=0; i < numThreads; i++)
  {
      create_val = pthread_create( &threadArr[i], NULL, work_routine, (void*)keyFile ); 
      if(create_val != 0){
	  printf("ERROR: return code from pthread_create is %d\n", create_val);
	  exit(-1);
      }
  }
  
  listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  if(listen_fd == -1){
      printf( "ERROR: socket failed: %s\n", strerror( errno ) );
      exit(-1);
  }
  memset(&serv_addr, '0', sizeof(serv_addr));
  
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = htonl(INADDR_ANY); // INADDR_ANY = any local machine address
  serv_addr.sin_port = htons(portNum);
  
  if(bind(listen_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr))){
       printf("\nError : Bind Failed. %s\n", strerror(errno));
       return 1; 
  }
  if(listen(listen_fd, 10)){
       printf("\nError : Listen Failed. %s\n", strerror(errno));
       return 1; 
  }
  
  
  //setting a handle for SIGINT:
  struct sigaction sigint_act;
  memset(&sigint_act, '\0', sizeof(sigint_act));
  sigint_act.sa_sigaction = (void*) &handle_sigint;
  sigint_act.sa_flags = SA_SIGINFO;
  if(sigaction(SIGINT,  &sigint_act, NULL) == -1){
    printf("Error : sigaction Failed. %s \n", strerror(errno));
    return 1;
  }
  
  
  while(1)
  {
      connfd = accept(listen_fd, NULL, NULL);
      if(connfd<0){
           printf("\nError : Accept Failed. %s\n", strerror(errno));
           return 1; 
      }
      
      enq_val = enqueue(queue, connfd);
      //sleep(1);
      
      if(enq_val != 0){
	printf("Error : enqueue failed. Return code from enqueue is %d\n", enq_val);
	exit(-1);
      }
  }
  
  
  return 1;
  
}