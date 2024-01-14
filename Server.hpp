#pragma once

#include <iostream>
#include <string>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <exception>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <sstream>
#include <unistd.h>
#include <fstream>
#include "Location.hpp"

#define BUFFER_SIZE 30720

class Server
{
private:
	unsigned short				port;
	in_addr						host;
	std::string					serverName;
	std::string					root;
	std::vector<std::string>	index;
	std::string					ipAddress;
	unsigned long				clientSize;
	std::vector<std::string>	errorPages;
	std::vector<Location>		locations;
	int							socketFd;
	sockaddr_in					socketAddress;
	int 						newSocket;
//	long 						incomingMsg;
	unsigned int				socketLen;
//	uint32_t ipAddress;

public:
	Server();
	Server(const Server& server);
	Server &operator=(const Server& server);
	~Server();

	void startServer();
	void closeServer();
	void startListen();
	void acceptConnection(int &newSocket);

	void setPort(unsigned short port);
	void setHost(std::string host);
	void setName(std::string name);
	void setRoot(std::string root);
	void setIndex(std::string index);
	void setIP();
	void setClientSize(unsigned long clientSize);
	void setErrorPage(std::string errorPage);
	void setLocation(std::string line, std::ifstream &stream);
//	void pushLocation();

	static void printServer(Server &server);
};