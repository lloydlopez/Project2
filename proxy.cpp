// proxy.cpp

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
const int MAX_REQ_SIZE = 2048;
const int MAX_REC_SIZE = 100000; 
const string METHOD = "GET";
const string HTTPVERSION = "HTTP/1.0";
char* ERROR = (char*)"500 'Internal Error'";
const char* HTTPPORT = (char*)"80";
sem_t mySemaphore;

bool validateRequest(char *buffer);
void setRequest(Request *r, char *buffer);
void *producer(void *arg);
void *consumer(void *arg);
void *hello(void *arg);

int main(int argc, char *argv[])
{
	pthread_t pro;
	int pid;
	int cid[MAX_THREADS];
	sem_init(&mySemaphore, 0, 0);
	vector<pthread_t> threads;

	if ((pid = pthread_create(&pro, NULL, &producer, (void*)argv[1])))
		printf("producer is broken");

	for (int i = 0; i < MAX_THREADS; i++) {
		pthread_t new_thread;
		if ((cid[i] = pthread_create(&new_thread, NULL, &consumer, (void*)argv[1])))
			printf("consumer is broken");
		threads.push_back(new_thread);
	}

	for (int i = 0; i < MAX_THREADS; i++) {
		if (pthread_join(threads[i], NULL)) {
			cout << "Join error" << endl;
			pthread_cancel(cid[i]);
		}
	}

	if (pthread_join(pro, NULL)) {
		cout << "Join error" << endl;
		pthread_cancel(pid);
	}

	return 0;
}

// # of consumer threads = MAX_THREADS
void *consumer(void *arg)
{
	int sockfd, proxyfd, rv;
	struct addrinfo hints, *servinfo, *p;
 	char *recBuf = (char*)malloc(MAX_REC_SIZE);
	int numRead = 0;
	int sent = 0;
	char *reqBuf = (char*)malloc(MAX_REQ_SIZE);
	int size = MAX_REQ_SIZE;
	bool signal = false;
	int bufSize = MAX_REQ_SIZE;
	struct Request r;
	bool returnThread = false;

	while (1) {
		returnThread = false;
		sem_wait(&mySemaphore);
		pthread_mutex_lock(&lock);
		sockfd = sockets.front();
		sockets.pop();
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

		reqBuf -= numRead;
		cout << reqBuf << endl;
		if(validateRequest(reqBuf) && !returnThread)
		{
			
			setRequest(&r, reqBuf);
			
			//open socket to web server
			memset(&hints, 0, sizeof hints);
			hints.ai_family = AF_UNSPEC;
			hints.ai_socktype = SOCK_STREAM;
			cout << r.host << endl;
			if((rv = getaddrinfo(r.host, HTTPPORT, &hints, &servinfo)) != 0) {
				perror("host resolve issue");
				returnThread = true;
			}
			if(!returnThread){
				for(p = servinfo; p != NULL; p = p->ai_next) {
					if ((proxyfd = socket(p->ai_family, p->ai_socktype,
					p->ai_protocol)) == -1) {
						perror("client: socket");
						continue;
					}

					if (connect(proxyfd, p->ai_addr, p->ai_addrlen) == -1) {
						close(proxyfd);
						perror("client: connect");
						continue;
					}

					break;
				}

				if (p == NULL) {
					fprintf(stderr, "client: failed to connect\n");
					return NULL;
				}

				freeaddrinfo(servinfo);
				
				size = MAX_REC_SIZE;
				numRead = 0;
				
				cout << "Created new socket!" << endl;
				
				while((sent = write(proxyfd, r.buffer, bufSize)) && bufSize > 0){
					if(sent < 0)
						perror("Write Failed");
					r.buffer += sent;
					bufSize -= sent;
				}
				
				cout << "Completed first write!" << endl;
				
				while((numRead = read(proxyfd, recBuf, size)) && size > 0){
					recBuf += numRead;
					size -= numRead;
					/*
					if(size == 0){
						recBuf -= MAX_REC_SIZE;
						size += MAX_REC_SIZE;
						while((sent = write(sockfd, recBuf, size)) && size > 0){
							recBuf += sent;
							size -= sent;
						}
						recBuf -= MAX_REC_SIZE;
						size += MAX_REC_SIZE;
					}
					*/
				}
				cout << "Completed read/write!" << endl;
				recBuf -= (MAX_REC_SIZE - size);
				size = MAX_REC_SIZE;
				while((sent = write(sockfd, recBuf, size)) && size > 0){
					recBuf += sent;
					size -= sent;
				}
				cout << "done!" << endl;
				cout << endl;
				cout << endl;
				cout << endl;
			}
		} else if(!returnThread){
			cout << "validateRequest() failed!" << endl;
		}
		if(returnThread) {
			size = ERRORSIZE;
			char* ptr = ERROR;
			while((sent = write(sockfd, ptr, size)) && size > 0){
				ptr += sent;
				size -= sent;
			}
			cout << "Error: request not valid";
			cout << endl;
			cout << endl;
			cout << endl;
		}	
		//delete [] &r.host;
		close(proxyfd);
		close(sockfd);
	}

	return NULL;
}

// One producer thread  - need to include port for getaddrinfo
void *producer(void *arg)
{
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
			exit(1);
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
		exit(1);
	}

	while (1) {
		sin_size = sizeof their_addr;
		new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
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

bool validateRequest(char *buffer)
{
	if (buffer == NULL || strlen(buffer) < 1)
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
	
	// WHAT IF THERE'S MORE THAN 3 ELEMENTS? HOW TO SEE?
	while ((pos = line.find(delimiter)) != string::npos)
	{
		list.push_back(line.substr(0, pos));
		line.erase(0, pos + delimiter.length());
	}
	
	list.push_back(line.substr(0, pos - 1));

	// If the list has more than 3 elements, method not 'GET',
	//	or HTTP VERSION not '1.0' then request is not valid
	cout << '"' << list.size() << endl << list[0] << endl << list[2] << '"' << endl;
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

	r->host = new char[host.length() + 1];
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
	
	
	char *combined = new char[total.length() + 1];
	strcpy(combined, total.c_str());
	
	r->buffer = combined;
}

// TEST: google.com www.google.com/ /www.google.com
