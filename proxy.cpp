#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <fstream>
#include <iomanip>
#include <semaphore.h>
#include <vector>
#include <queue>
#include <iostream>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <csignal>


using namespace std;

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
queue<int> sockets;
const int MAX_THREADS = 30;
sem_t mySemaphore;
char *resBuf = (char*)malloc(1000000);

void *producer(void *arg);
void *consumer(void *arg);

int main(int argc, char *argv[]){
  pthread_t pro;
  int pid, cid;
  sem_init(&mySemaphore, 0, 0);
  vector<pthread_t> threads;
    
  if((pid = pthread_create(&pro,NULL,&producer,(void*)argv[1])))
    printf("producer is broken");
  for(int i = 0; i < MAX_THREADS; i++){
    pthread_t new_thread;
    if((cid = pthread_create(&new_thread,NULL,&consumer,(void*)argv[1])))
      printf("consumer is broken");
    threads.push_back(new_thread);
  }
  if(pthread_join(pid, NULL) || pthread_join(cid, NULL)){
    cout << "Join error" << endl;
    pthread_cancel(pid);
    pthread_cancel(cid);
  }
    
  return 0;
}

void *consumer(void *arg){ //# of consumer threads = MAX_THREADS
  int sockfd;
  int numRead = 0;
  char *reqBuf = (char*)malloc(32);
  int size = 32;
  bool signal = false;
    
  while(1){
    sem_wait(&mySemaphore);
    pthread_mutex_lock(&lock);
    sockfd = sockets.front();
    sockets.pop();
    pthread_mutex_unlock(&lock);
    
    while((numRead = read(sockfd, reqBuf, size)) && size > 0 && !signal){
      reqBuf += numRead;
      size -= numRead;
      if(numRead > 4){
        if(reqBuf[numRead-4]=='\r' && reqBuf[numRead-3]=='\n'&& reqBuf[numRead-2]=='\r' && reqBuf[numRead]=='\r')
          signal = true;
      }
    }
    cout << reqBuf << endl;
  }
  return NULL;
}
void *producer(void *arg){    //One producer thread  - need to include port for getaddrinfo
  char* port = (char*)arg;
  int sockfd, new_fd, rv;
  struct addrinfo hints, *servinfo, *p;
  struct sockaddr_storage their_addr;
  socklen_t sin_size;
  int yes = 1;

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;
  
  if((rv = getaddrinfo(NULL, port, &hints, &servinfo)) != 0){
    cout << "GetAddrInfo failed with error " << rv;
    return NULL;
  }
  for(p = servinfo; p!= NULL; p = p->ai_next){
    if((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == 0){
      perror("server: socket");
      continue;
    }
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
                   sizeof(int)) == -1) {
      perror("setsockopt");
      exit(1);
    }
    if(bind(sockfd, p->ai_addr, p->ai_addrlen) == -1){
      close(sockfd);
      perror("server:bind");
      continue;
    }
    break;
  }
  
  freeaddrinfo(servinfo);

  if(listen(sockfd, 5) == 1){
    perror("listen");
    exit(1);
  }
 
  while(1){
    sin_size = sizeof their_addr;
    new_fd = accept(sockfd,(struct sockaddr *)&their_addr, &sin_size);
    if (new_fd == -1) {
      perror("accept");
      continue;
    }

    pthread_mutex_lock(&lock);
    sockets.push(new_fd);
    pthread_mutex_unlock(&lock); 
    sem_post(&mySemaphore);  
  }
  return NULL;
}


