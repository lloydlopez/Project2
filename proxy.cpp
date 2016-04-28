// James Baracca and Lloyd Lopez
// proxy.cpp version 1

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
#include <string.h>


using namespace std;

struct Request
{
	char* buffer;
	char* host;
};

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
queue<int> sockets;
const int ERRORSIZE = 20;
const int MAX_THREADS = 30;
const int MAX_REQ_SIZE = 4096;
const int MAX_REC_SIZE = 100000; 
const string METHOD = "GET";
const string HTTPVERSION = "HTTP/1.0";
char* ERROR = (char*)"500 'Internal Error'";
const char* HTTPPORT = (char*)"80";

sem_t mySemaphore;
pthread_t pro;
vector<pthread_t> threads;
int activeThreads = 0;

bool validateRequest(char *buffer);
void setRequest(Request *r, char *buffer);
void *producer(void *arg);
void *consumer(void *arg);
void int_handler(int);

int main(int argc, char *argv[])
{
	signal(SIGINT,int_handler);
	int pid;
	int cid[MAX_THREADS];
	sem_init(&mySemaphore, 0, 0);

	// Create producer thread
	if ((pid = pthread_create(&pro, NULL, &producer, (void*)argv[1])))
		printf("producer is broken");

	// Create consumer threads
	for (int i = 0; i < MAX_THREADS; i++) {
		pthread_t new_thread;
		if ((cid[i] = pthread_create(&new_thread, NULL, &consumer, (void*)argv[1])))
			printf("consumer is broken");
		threads.push_back(new_thread);
	}

	// Wait for all threads to finish
	for (int i = 0; i < MAX_THREADS; i++) {
		if (pthread_join(threads[i], NULL)) {
			cout << "Join error" << endl;
		}
	}

	if (pthread_join(pro, NULL)) {
		cout << "Join error" << endl;
	}
	
	// Cleanup
	sem_destroy(&mySemaphore);
	pthread_mutex_destroy(&lock);
	return 0;
}

// # of consumer threads = MAX_THREADS
void *consumer(void *arg)
{
	int sockfd, proxyfd, rv;
	struct addrinfo hints, *servinfo, *p;
	int numRead = 0;
	int sent = 0;
	int size = MAX_REQ_SIZE;
	bool signal = false;
	int bufSize = MAX_REQ_SIZE;
	struct Request r;
	bool returnThread = false;
	char* ptr = ERROR;
	
	// Receive and Request buffers
	char *recBuf = (char*)malloc(MAX_REC_SIZE);
	char *reqBuf = (char*)malloc(MAX_REQ_SIZE);
	

	while (1) {
		
		// A critical section
		returnThread = false;
		sem_wait(&mySemaphore);
		pthread_mutex_lock(&lock);
		sockfd = sockets.front();
		sockets.pop();
		activeThreads++;
		pthread_mutex_unlock(&lock);
		size = MAX_REQ_SIZE;

		while (!signal && (size > 0) && (numRead = read(sockfd, reqBuf, size))) {
			if(numRead < 0){
				returnThread = true;
				close(sockfd);
				break;
			}
			reqBuf += numRead;
			size -= numRead;
			
			// Find end of request with \r\n\r\n
			if (numRead >= 4) {
				char* temp = reqBuf - numRead;
				string buf(temp);
				if (buf.find("\r\n\r\n") != std::string::npos) {
					numRead = buf.find("\r\n\r\n", 0) + 4;
					reqBuf -= buf.length() - numRead;
					signal = true;
				}
			}
			if (numRead == MAX_REQ_SIZE)
				break;
		}
		
		reqBuf -= (MAX_REQ_SIZE-size);
		
		// Validate client request
		if(validateRequest(reqBuf) && !returnThread)
		{
			setRequest(&r, reqBuf);
			
			// Open socket to web server
			memset(&hints, 0, sizeof hints);
			hints.ai_family = AF_UNSPEC;
			hints.ai_socktype = SOCK_STREAM;
			
			char* newBuf = (char*)malloc(MAX_REQ_SIZE);
			for(int i = 0; i < MAX_REQ_SIZE; i++){
				newBuf = r.buffer;
				newBuf++;
				r.buffer++;
			}
			
			newBuf -= MAX_REQ_SIZE;

			if((rv = getaddrinfo(r.host, HTTPPORT, &hints, &servinfo)) != 0) {
				returnThread = true;
			}
			
			if(!returnThread){
				for(p = servinfo; p != NULL; p = p->ai_next) {
					if ((proxyfd = socket(p->ai_family, p->ai_socktype,
					p->ai_protocol)) == -1) {
						perror("socket");
						continue;
					}

					if (connect(proxyfd, p->ai_addr, p->ai_addrlen) == -1) {
						close(proxyfd);
						perror("connect");
						continue;
					}

					break;
				}
				if (p == NULL) {
					fprintf(stderr, "client: failed to connect\n");
					returnThread = true;
					close(proxyfd);
					close(sockfd);
				}
				if(!returnThread){
					freeaddrinfo(servinfo);
					
					size = MAX_REC_SIZE;
					numRead = 0;
					
					// Send request to server
					while((sent = write(proxyfd, newBuf, bufSize)) && bufSize > 0){
						if(sent < 0)
							perror("Write Failed");
						newBuf += sent;
						bufSize -= sent;
					}

					// Read response from server
					while((numRead = read(proxyfd, recBuf, size)) && size > 0){
						recBuf += numRead;
						size -= numRead;
					}
					
					recBuf -= (MAX_REC_SIZE - size);
					size = (MAX_REC_SIZE - size);
					
					// Send response to client
					while((sent = write(sockfd, recBuf, size)) && size > 0){
						
						if(sent < 0){
							break;
						}
						recBuf += sent;
						size -= sent;
					}
										
					sleep(1);
				}
			}
			
		} else if(!returnThread){
			returnThread = true;
		}
		
		// Write error to client
		if(returnThread) {
			size = ERRORSIZE;
			while((sent = write(sockfd, ptr, size)) && size > 0){
				ptr += sent;
				size -= sent;
			}
			
			ptr -= (ERRORSIZE - size);
		}	
		
		close(proxyfd);
		close(sockfd);
		pthread_mutex_lock(&lock);
		activeThreads--;
		pthread_mutex_unlock(&lock);
	}

	return NULL;
}

// One producer thread  
void *producer(void *arg)
{
	
	// Set-up connection
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

	if ((rv = getaddrinfo(NULL, port, &hints, &servinfo)) != 0) {
		cout << "GetAddrInfo failed with error " << rv;
		return NULL;
	}

	for (p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == 0) {
			perror("server: socket");
			continue;
		}
		if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
			sizeof(int)) == -1) {
			perror("setsockopt");
		}
		if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			perror("server:bind");
			continue;
		}
		break;
	}

	freeaddrinfo(servinfo);

	if (listen(sockfd, 5) == 1) {
		perror("listen");
	}

	while (1) {
		sin_size = sizeof their_addr;
		new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
		if (new_fd == -1) {
			perror("accept");
			continue;
		}
		
		pthread_mutex_lock(&lock);
		
		// Only 30 open sockets
		if(sockets.size() > 30){
			pthread_mutex_unlock(&lock);
			sleep(2);
			
		}else
		
			pthread_mutex_unlock(&lock);
		pthread_mutex_lock(&lock);
		sockets.push(new_fd);
		pthread_mutex_unlock(&lock);
		pthread_mutex_lock(&lock);
		
		if(activeThreads < 30){
			pthread_mutex_unlock(&lock);
			sem_post(&mySemaphore);
			
		}else
			pthread_mutex_unlock(&lock);
	}
	return NULL;
}

// Validates client request
bool validateRequest(char *buffer)
{
	string tempBuf(buffer);
	if (buffer == NULL || tempBuf.length() < 1)
	{

		return false;
	}
	
	// Make a duplicate of the buffer
	char *copy = (char*)malloc(MAX_REQ_SIZE);
	strncpy(copy, buffer, MAX_REQ_SIZE);
	string line(copy);
	
	// Retrieve just the request line
	line = line.substr(0, line.find("\r\n"));

	// Separate the request line by tokens using the delimiter 
	//	and add them to a vector
	string delimiter = " ";
	vector<string> list;
	size_t pos = 0;
	
	while ((pos = line.find(delimiter)) != string::npos)
	{
		list.push_back(line.substr(0, pos));
		line.erase(0, pos + delimiter.length());
	}
	
	list.push_back(line.substr(0, pos - 1));

	// If the list has more than 3 elements, method not 'GET',
	//	or HTTP VERSION not '1.0' then request is not valid
	if (list.size() > 3 || list[0] != METHOD || list[2] != HTTPVERSION)
	{

		return false;
	}

	return true;
}

void setRequest(Request *r, char* buffer)
{
	// Make a duplicate of the buffer
	char *copy = (char*)malloc(MAX_REQ_SIZE);
	strcpy(copy, buffer);

	string line(copy);
	// Retrieve just the request line
	line = line.substr(0, line.find("\r\n"));
	
	// Separate the request line by tokens using the delimiter 
	//	and add them to a vector
	string delimiter = " ";
	vector<string> list;
	size_t pos = 0;

	while ((pos = line.find(delimiter)) != string::npos)
	{
		list.push_back(line.substr(0, pos));
		line.erase(0, pos + delimiter.length());
	}

	// Find just the URI
	string host = list[1].substr(list[1].find("/") + 2, list[1].size() - 7);
	if(host.substr(host.length()-1,1) == "/"){
		host = host.substr(0,host.length()-1);
	}
	// If "www." is not part of uri already, then add it 
	if(host.substr(0,4) != "www.")
	{
		host = "www." + host;
	}

	r->host = (char*)malloc(host.length() + 1);
	strcpy(r->host, host.c_str());

	char *serverMsg = (char*)malloc(MAX_REQ_SIZE);
	serverMsg = (char*)((METHOD + " / " + HTTPVERSION + "\r\nHost: " + r->host + "\r\n" + "Connection: close\r\n").c_str());
	string initialLine(serverMsg);

	// Make a duplicate of the buffer to retrieve just the headers
	char *headers = (char*)malloc(MAX_REQ_SIZE);
	strcpy(headers, buffer);
	string newHeaders(headers);

	// Retrieve just the headers
	newHeaders.erase(0, newHeaders.find("\r\n") + 2);

	// Deletes Host and Connection from headers
	newHeaders.erase(newHeaders.find("Host: "), newHeaders.find("\r\n", newHeaders.find("Host: ")) + 2);
	newHeaders.erase(newHeaders.find("Connection: keep-alive"), 24);
		
	string total = initialLine + newHeaders;
	//headers.erase(0, total.find("\r\n"));
	
	char *combined = (char*)malloc(total.length() + 1);
	strcpy(combined, total.c_str());
	
	r->buffer = combined;
}

// Signal handler
void int_handler(int x){
  	for (int i = 0; i < MAX_THREADS; i++) {
		if (pthread_cancel(threads[i])) {
			cout << "Join error" << endl;
		}
	}

	if (pthread_cancel(pro)) {
		cout << "Join error" << endl;
	}
	
	if(sockets.size() > 0){
		for(unsigned int i = 0; i < sockets.size(); i++){
			close(sockets.front());
			sockets.pop();
		}
	}
	sem_destroy(&mySemaphore);
	pthread_mutex_destroy(&lock);
	exit(1);
}
