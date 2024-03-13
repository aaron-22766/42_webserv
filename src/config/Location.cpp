#include "../../include/webserv.hpp"

Location::Location(){
	name = "";
	root = "";
	autoindex = true;
	autoindexSet = false;
	redirect = "";
}

Location::Location(const Location &location) : name(location.name),
											   root(location.root), allowedMethods(location.allowedMethods),
											   index(location.index), autoindex(location.autoindex), cgi_info(location.cgi_info),
											   autoindexSet(location.autoindexSet), redirect(location.redirect){}

Location &Location::operator=(const Location &location){
	if (&location != this)
	{
		name = location.name;
		root = location.root;
		allowedMethods = location.allowedMethods;
		index = location.index;
		autoindex = location.autoindex;
		cgi_info = location.cgi_info;
		autoindexSet = location.autoindexSet;
		redirect = location.redirect;
	}
	return *this;
}

Location::~Location() {}

void Location::setName(std::string name) {
	if (!this->name.empty())
		throw std::runtime_error("Configuration file error: declared name twice.\n");

    this->name = name;
}

void Location::setRoot(std::string root) {
	if (!this->root.empty())
		throw std::runtime_error("Configuration file error: declared root twice.\n");
	struct stat sb;

	if (stat(root.c_str(), &sb) != 0)
		throw std::runtime_error("Config file error: root directory does not exist.\n");
	this->root = root;
}

void Location::setIndex(std::string index) {
    this->index.push_back(index);
}

void Location::setMethod(std::string method) {
    if (method == "GET") {
        this->allowedMethods.push_back(GET);
    } else if (method == "HEAD") {
        this->allowedMethods.push_back(HEAD);
    } else if (method == "POST") {
        this->allowedMethods.push_back(POST);
    } else if (method == "DELETE") {
        this->allowedMethods.push_back(DELETE);
    } else {
		throw std::runtime_error("Configuration file error: method does not exist.\n");
    }
}

void Location::setAutoIndex(std::string autoindex) {
	if (autoindex == "true"){
		this->autoindex = true;
	}
	else if (autoindex == "false"){
		this->autoindex = false;
	}
	else
		throw std::runtime_error("Config file error: autoindex can only be set to true or false.\n");
	this->autoindexSet = true;
}

void Location::setCgiInfo(std::string cgiInfo){
	this->cgi_info.push_back(cgiInfo);
}

void Location::setRedirect(std::string redirect) {
	if (!this->redirect.empty())
		throw std::runtime_error("Config file error: Can only have one redirection per location.");
	this->redirect = redirect;
}

void Location::changeAutoIndex(bool ai) {
	this->autoindex = ai;
}

void printListTab(std::string index)
{
	std::clog << "\t\t" << index << std::endl;
}
void printListMethods(RequestMethod meth)
{
	std::string method;
	if (meth == GET)
		method = "GET";
	else if (meth == POST)
		method = "POST";
	else
		method = "DELETE";
	std::clog << "\t\t" << method << std::endl;
}

void Location::printLocation(Location &location) {
	std::clog << "Location: " << location.name <<
			  "\n\tRoot: " << location.root << "\n\tIndex:\n";
	std::for_each(location.index.begin(), location.index.end(), printListTab);
	std::clog << "\tCGI Info:\n";
	std::for_each(location.cgi_info.begin(), location.cgi_info.end(), printListTab);
	std::clog << "\tAllowed:\n";
	std::for_each(location.allowedMethods.begin(), location.allowedMethods.end(), printListMethods);
	std::clog << "\tAutoindex: " << location.autoindex << std::endl;
	std::clog << "\tRedirect: " << location.getRedirect() << std::endl;
}

const std::string &Location::getName() const{
	return this->name;
}

const std::vector<RequestMethod> &Location::getAllowedMethods() const{
	return this->allowedMethods;
}

bool Location::isAutoIndex() const{
	return this->autoindex;
}

std::vector <std::string> Location::getCgiInfo() const{
	return this->cgi_info;
}

const std::vector <std::string> &Location::getIndex() const{
	return this->index;
}

const std::string &Location::getRoot() const{
	return this->root;
}

const std::string &Location::getRedirect() const {
	return this->redirect;
}

void Location::autoCompleteFromServer(const Server &server) {
	if (root.empty())
		this->root = server.getRoot();
	if (index.empty())
		this->index = server.getIndex();
	if (!autoindexSet)
		this->autoindex = server.isAutoIndex();
	if (this->cgi_info.empty())
		this->cgi_info = server.getCgiInfo();
    if (this->allowedMethods.empty()) {
        this->allowedMethods.push_back(GET);
        this->allowedMethods.push_back(POST);
        this->allowedMethods.push_back(DELETE);
    }
	if (this->redirect.empty())
		this->redirect = server.getRedirect();
}
