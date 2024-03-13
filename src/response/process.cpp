#include "../../include/webserv.hpp"

void Response::processRequest() {
    if ((_status = _request.getErrorCode()) != 0) {
        return ;
    }

    _location = _server.getLocation(_request.getPath());

    std::vector<RequestMethod> allowedMethods = _location.getAllowedMethods();
    if (std::find(allowedMethods.begin(), allowedMethods.end(), _request.getMethod()) == allowedMethods.end()) {
        throw 405; // request method is not allowed
    }

    if (_location.getRedirect().empty() == false) {
        _headers["location"] = _location.getRedirect();
        throw 301;
    }

    _path = _location.getRoot() + cleanPath(_request.getPath());

    if (_request.getMethod() == POST && _request.getBody().size() > _server.getClientBodySize()) {
        throw 413;
    }

    if ((_isCGI = isCGI()) == true) {
        executeCGI();
        return ;
    }

    if (access(_path.c_str(), F_OK) == 0) {
        if (stat(_path.c_str(), &_pathStat) != 0) {
            throw 500;
        }
    } else if (_request.getMethod() != POST) {
        throw 404;
    }

    switch (_request.getMethod()) {
    case GET: 
        performGET();
        return ;
    case HEAD:
        performGET();
        return ;
    case POST: 
        performPOST();
        return ;
    case DELETE: 
        performDELETE();
        return ;
    default:
        _status = 501;
    }
}

std::string Response::cleanPath(const std::string &path) {
    std::vector<std::string> components;
    std::istringstream stream(path);
    std::string component;

    while (std::getline(stream, component, '/')) {
        if (component == "..") {
            if (components.empty()) {
                throw 403; // directory traversal attempt
            }
            components.pop_back();
        } else if (component != "." && !component.empty()) {
            components.push_back(component);
        }
    }
    std::ostringstream cleanedPath;
    for (size_t i = 0; i < components.size(); ++i) {
        cleanedPath << '/' << components[i];
    }
    std::string result = cleanedPath.str();
    if (path.at(path.size()- 1) == '/') {
        result.push_back('/');
    }
    return (result.empty() ? "/" : result);
}

void Response::performGET() {
    if (access(_path.c_str(), R_OK) != 0) {
        throw 403;
    }
    std::string pathExtension = _path;
    if (S_ISDIR(_pathStat.st_mode) == false) {
        fileToBody(_path);
    } else {
        if (_path.at(_path.size() - 1) != '/') {
            _headers["location"] = _request.getUri() + "/";
            throw 301;
        }
        pathExtension = getIndex();
        if (pathExtension.empty() == false) { // has index file
            fileToBody(pathExtension);
        } else if (_location.isAutoIndex() == true) { // auto-indexing on
            makeDirectoryListing();
            pathExtension = "html";
        } else { // directory without response
            throw 403;
        }
    }
    const char *type = Response::getMimeType(pathExtension.c_str());
    if (type == NULL) {
        _body.clear();
        throw 415;
    }
    _status = 200;
    _headers["content-type"] = type;
    if (pathExtension != "html") { // is auto-index
        _headers["last-modified"] = formatDate(_pathStat.MTIME);
    }
}

void Response::fileToBody(std::string &path) {
    int fd = open(path.c_str(), O_RDONLY);
    if (fd == -1) {
        if (errno == ENOENT) {
            throw 404;
        }
        throw 500;
    }
    if (readToVector(fd, _body) == RETURN_FAILURE) {
        close(fd);
        throw 500;
    }
    close(fd);
}

std::string Response::getIndex() {
    std::vector<std::string> indexes = _location.getIndex();
    for (std::vector<std::string>::iterator it = indexes.begin();
        it != indexes.end(); ++it) {
        std::string index = combinePaths(_path, *it);
        if (access(index.c_str(), R_OK) == 0) {
            return (index); // found valid index path
        }
    }
    return (""); // no index path found
}

void Response::performPOST() {
    const char *extension = Response::getMimeExtenstion(_request.getHeaderValueForKey("content-type").c_str());
    if (extension == NULL) {
        throw 415;
    }
    std::string postPath = _path;
    if (_path[_path.size() - 1] == '/'
        || (access(_path.c_str(), F_OK) == 0 && S_ISDIR(_pathStat.st_mode))) {
        std::string index = "index." + std::string(extension);
        postPath = combinePaths(_path, index);
    }
    if (access(postPath.c_str(), F_OK) == 0) {
        throw 403;
    }
    std::string created; // dir creation
    std::string dir = postPath.substr(0, postPath.rfind('/'));
    if (access(dir.c_str(), F_OK) != 0) {
        // create all directories
        for (size_t ext = postPath.find('/'); ext != std::string::npos;
            ext = postPath.find('/' , ext + 1)) {
            dir = postPath.substr(0, ext);
            if (access(dir.c_str(), F_OK) == -1) {
                if (mkdir(dir.c_str(), 0700) == -1) {
                    throw 500;
                } else if (created.empty()) {
                    created = dir;
                }
            }
        }
    }
    std::ofstream file(postPath.c_str(), std::ofstream::binary);
    if (file.is_open()) {
        file.write(_request.getBody().data(), _request.getBody().size());
        if (file.bad() == false) {
            file.close();
            _status = 201;
            _headers["location"] = postPath.substr(_location.getRoot().size());
            return ;
        }
        file.close();
    }
    if (created.empty() == false) { // dir creation
        deleteDirectory(created);
    } else {
        std::remove(postPath.c_str());
    }
    throw 500;
}

void Response::performDELETE() {
    if (S_ISDIR(_pathStat.st_mode) == false) {
        if (std::remove(_path.c_str()) == -1) {
            throw 500;
        }
    } else {
        if (_request.getQuery() != "force") {
            appendStringToVector(_body, DELETE_CONFIM_MESSAGE);
            if (_path.at(_path.size() - 1) != '/') {
                _headers["location"] = _request.getUri() + "/";
            }
            throw 409;
        }
        if (access(_path.c_str(), W_OK) == -1) {
            throw 403;
        }
        if (deleteDirectory(_path) == RETURN_FAILURE) {
            throw 500;
        }
    }
    _status = 204;
}
 