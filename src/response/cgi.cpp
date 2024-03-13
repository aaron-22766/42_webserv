#include "../../include/webserv.hpp"

/* ************************************************************************** */
/*                                   CHECK                                    */
/* ************************************************************************** */

bool Response::isCGI() {
    if ((_request.getMethod() & CGI_METHODS) == 0) {
        return (false);
    }

    if (S_ISDIR(_pathStat.st_mode) == true) {
        return (hasCGIIndex());
    }

    return (isValidCGI(_path));
}

bool Response::isValidCGI(std::string &path) {
    for (size_t ext = path.find('.'); ext != std::string::npos; ext = path.find('.', ext + 1)) {
        size_t end = path.find('/', ext);
        if (end == std::string::npos) {
            end = path.size();
        }
        std::string testPath = path.substr(0, end);
        struct stat dirStat;
        if (stat(testPath.c_str(), &dirStat) != 0) {
            return (false);
        }
        if (S_ISDIR(dirStat.st_mode) == false) {
            std::string extension = path.substr(ext + 1, end - ext - 1);
            if (Response::isSupportedCGI(extension) == false) {
                return (false);
            }
            if (access(testPath.c_str(), R_OK) != 0) {
                return (false);
            }
            _cgiPath = testPath;
            _cgiExtension = extension;
            _cgiPathInfo = path.substr(end, path.size() - end);
            return (true);
        }
    }
    return (false);
}

bool Response::isSupportedCGI(const std::string &extension) {
    return (_supportedCGI.find("," + extension + ",") != std::string::npos);
}

bool Response::hasCGIIndex() {
    std::vector<std::string> indexes = _location.getIndex();
    for (std::vector<std::string>::iterator it = indexes.begin();
        it != indexes.end(); ++it) {
        std::string index = combinePaths(_path, *it);
        if (isValidCGI(index) == true) {
            return (true);
        }
    }
    return (false);
}

/* ************************************************************************** */
/*                                  EXECUTE                                   */
/* ************************************************************************** */

void Response::executeCGI() {
    checkCGI();
    int cgiOutput[2];
    int cgiInput[2];
    setupPipes(cgiInput, cgiOutput);
    pid_t cgi = fork();
    if (cgi == -1) {
        if (_request.getMethod() == POST) {
            close(cgiInput[PIPE_READ]);
        }
        close(cgiOutput[PIPE_READ]);
        close(cgiOutput[PIPE_WRITE]);
        throw 500;
    } else if (cgi == 0) {
        close(cgiOutput[PIPE_READ]);
        cgiProcess(cgiInput[PIPE_READ], cgiOutput[PIPE_WRITE]);
    }
    if (_request.getMethod() == POST) {
        close(cgiInput[PIPE_READ]);
    }
    close(cgiOutput[PIPE_WRITE]);
    if ((_status = waitForCGI(cgi)) == 0) {
        if (readToVector(cgiOutput[PIPE_READ], _response) == RETURN_FAILURE) {
            _status = 500;
        }
    }
    close(cgiOutput[PIPE_READ]);
    if (_status != 0) {
        throw _status;
    }
    parseCGIOutput();
}

void Response::checkCGI() {
    std::vector<std::string> cgiInfo = _location.getCgiInfo();
    if (std::find(cgiInfo.begin(), cgiInfo.end(), _cgiExtension) == cgiInfo.end()) {
        throw 403; // file extension not allowed
    }

    std::ifstream file(_cgiPath.c_str());
    if (file.is_open() == false) {
        throw 500; // failed opening file
    }
    std::string line;
    std::getline(file, line);
    file.close();

    if (line.compare(0, 2, SHEBANG) != 0) {
        throw 500; // no shebang
    }
    line.erase(0, 2);
    _cgiArgv = splitString(line, " \t");
    if (access(_cgiArgv.front().c_str(), X_OK) != 0) {
        throw 500; // interpreter invalid or not installed
    }

    _cgiArgv.push_back(_cgiPath.substr(_cgiPath.rfind("/") + 1));
}

void Response::setupPipes(int cgiInput[2], int cgiOutput[2]) {
    if (pipe(cgiOutput) == -1) {
        throw 500;
    }
    if (_request.getMethod() == POST) {
        if (pipe(cgiInput) == -1) {
            close(cgiOutput[PIPE_READ]);
            close(cgiOutput[PIPE_WRITE]);
            throw 500;
        }
        ssize_t written = write(cgiInput[PIPE_WRITE], _request.getBody().data(), _request.getBody().size());
        close(cgiInput[PIPE_WRITE]);
        if (written != static_cast<ssize_t>(_request.getBody().size())) {
            close(cgiInput[PIPE_READ]);
            close(cgiOutput[PIPE_READ]);
            close(cgiOutput[PIPE_WRITE]);
            throw 500;
        }
    }
}

void Response::cgiProcess(int cgiInputREAD, int cgiOutputWRITE) {
    dup2(cgiOutputWRITE, STDOUT_FILENO);
    if (_request.getMethod() == POST) {
        dup2(cgiInputREAD, STDIN_FILENO);
        close(cgiInputREAD);
    }
    close(cgiOutputWRITE);
    if (chdir(_cgiPath.substr(0, _cgiPath.rfind("/")).c_str()) == -1) {
        std::exit(EXIT_FAILURE);
    }
    char **argv = vectorToArray(_cgiArgv);
    if (argv == NULL) {
        std::exit(EXIT_FAILURE);
    }
    char **env = getCGIEnvironment();
    if (env != NULL) {
        execve(argv[0], argv, env);
        free_2d_array((void **)(env));
    }
    free_2d_array((void **)(argv));
    std::exit(EXIT_FAILURE);
}

char **Response::getCGIEnvironment() {
    std::vector<std::string> env;

    env.push_back("GATEWAY_INTERFACE=" + std::string(CGI_VERSION));
    env.push_back("SERVER_SOFTWARE=" + std::string(SERVER_VERSION));
    env.push_back("SERVER_NAME=" + _server.getServerName());
    env.push_back("SERVER_PROTOCOL=" + _request.getHttpVer());
    env.push_back("SERVER_PORT=" + SSTR(_server.getPort()));
    env.push_back("REQUEST_METHOD=" + _request.getMethodStr());
    env.push_back("REQUEST_URI=" + _request.getUri());
    env.push_back("SCRIPT_NAME=" + _cgiPath);
    env.push_back("PATH_INFO=" + _cgiPathInfo); // or _cgiPath ?? -> then _cgiPathInfo not needed at all
    env.push_back("PATH_TRANSLATED=" + _cgiPath);
    env.push_back("DOCUMENT_ROOT=" + _location.getRoot());
    env.push_back("QUERY_STRING=" + _request.getQuery());
    env.push_back("CONTENT_TYPE=" + _request.getHeaderValueForKey("content-type"));
    env.push_back("CONTENT_LENGTH=" + SSTR(_request.getBody().size()));
    env.push_back("REMOTE_ADDR=" + _server.getIpAddr());
    env.push_back("HTTP_FROM=" + _request.getHeaderValueForKey("from"));
    env.push_back("HTTP_ACCEPT=" + _request.getHeaderValueForKey("accept"));
    env.push_back("HTTP_USER_AGENT=" + _request.getHeaderValueForKey("user-agent"));
    env.push_back("HTTP_REFERER=" + _request.getHeaderValueForKey("referer"));

    return (vectorToArray(env));
}

int Response::waitForCGI(pid_t cgi) {
    time_t start = std::time(NULL);
    int waitStatus;

    while (waitpid(cgi, &waitStatus, WNOHANG) == 0) {
        if (std::difftime(std::time(NULL), start) >= CGI_TIMEOUT) {
            kill(cgi, SIGTERM);
            return (503);
        }
    }
    if (WIFEXITED(waitStatus) && WEXITSTATUS(waitStatus) == EXIT_SUCCESS) {
        return (0);
    }
    return (500);
}

/* ************************************************************************** */
/*                                   PARSE                                    */
/* ************************************************************************** */

void Response::parseCGIOutput() {
    if (_response.empty()) {
        throw 204;
    }
    int len = 2;
    std::vector<char>::iterator headerEnd = findSubstring(_response.begin(), _response.end(), "\n\n");
    if (headerEnd == _response.end()) {
        len = 4;
        headerEnd = findSubstring(_response.begin(), _response.end(), CRLFCRLF);
        if (headerEnd == _response.end()) {
            throw 500;
        }
    }
    _body.insert(_body.begin(), headerEnd + len, _response.end());
    _response.erase(headerEnd + len / 2, _response.end()); // empty line and body removed
    parseCGIHeaders();
    interpretCGIHeaders();
}

void Response::parseCGIHeaders() {
    while (_response.empty() == false) {
        std::vector<char>::iterator fieldEnd = std::find(_response.begin(), _response.end(), '\n');
        if (fieldEnd == _response.end()) {
            throw 500;
        }
        std::string test = std::string(_response.begin(), fieldEnd);
        std::istringstream field(test);
        _response.erase(_response.begin(), fieldEnd + 1);
        if ((!field.eof() && (field.peek() == ' ' || field.peek() == '\t'))) {
            throw 500;
        }
        std::string line, key, value;
        if (!std::getline(field, line, '\n')) {
            throw 500;
        }
        if (line.at(line.size() - 1) == '\r') {
            line.erase(line.size() - 1);
        }
        std::istringstream iss(line);
        if (!std::getline(iss, key, ':') || std::isspace(key.at(key.size() - 1))
            || !std::getline(iss, value) || containsControlChar(key)
            || containsControlChar(value)) {
            throw 500;
        }
        trimString(key);
        strToLower(key);
        trimString(value);
        if (!field || _headers.find(key) != _headers.end()) {
            throw 500;
        }
        _headers[key] = value;
    }
}

void Response::interpretCGIHeaders() {
    std::map<std::string, std::string>::iterator searchIt;

    // Status header 
    _status = 200;
    searchIt = _headers.find("status");
    if (searchIt != _headers.end()) {
        std::istringstream(searchIt->second) >> _status;
        if (_server.getStatusMessage(_status) == NULL) {
            throw 500;
        }
        _headers.erase(searchIt);
    }

    // Location header 
    searchIt = _headers.find("location");
    if (searchIt != _headers.end()) {
        if (searchIt->second.find("://") != std::string::npos) {
            // absolute URI 
            if (_headers.size() == 1) {
                throw 302; // -> client redirect
            } else if (_body.empty() && _status != 302) {
                throw 500; // -> client redirect with body didn't set Status
            }
        } else if (_headers.size() == 1) {
            _redirectHistory.push_back(_request.getUri());
            if (_redirectHistory.size() < REDIRECTION_LIMIT && 
                std::find(_redirectHistory.begin(), _redirectHistory.end(), searchIt->second) == _redirectHistory.end()) {
                // relative URI -> local redirect
                std::istringstream iss(" " + searchIt->second);
                _request.parseURI(iss);
                _headers.clear();
                _body.clear();
                _response.clear();
                processRequest();
            } else {
                throw 500;
            }
        }
    }

    // Content-Type header 
    if (_body.empty() == false && _headers.find("content-type") == _headers.end()) {
        throw 500;
    }

    // Transfer-Encoding: chunked -> unchunk
    searchIt = _headers.find("transfer-encoding");
    if (searchIt != _headers.end() && searchIt->second == "chunked"
        && unchunkCGIBody() == RETURN_FAILURE) {
        throw 500;
    }

    // Content-Length header 
    if (_body.empty() == false) {
        searchIt = _headers.find("content-length");
        if (searchIt == _headers.end()) {
            _headers["content-length"] = SSTR(_body.size());
        } else {
            size_t len;
            std::istringstream(searchIt->second) >> len;
            if (len < _body.size()) {
                _body.erase(_body.begin() + len, _body.end());
            }
        }
    }
}

Return Response::unchunkCGIBody() {
    std::vector<char> temp(_body);
    _body.clear();
    while (true) {
        std::vector<char>::iterator nl = findSubstring(temp.begin(), temp.end(), CRLF);
        if (nl == temp.end()) {
            return (RETURN_FAILURE);
        }
        std::string length(temp.begin(), nl);
        size_t len;
        std::istringstream iss(length);
        iss >> std::hex >> len;
        if (iss.eof() == false || iss.fail() == true) {
            return (RETURN_FAILURE);
        }
        temp.erase(temp.begin(), nl + 2);
        if (temp.empty()) {
            return (RETURN_FAILURE);
        }
        nl = findSubstring(temp.begin(), temp.end(), CRLF);
        if (nl == temp.end()) {
            return (RETURN_FAILURE);
        } else if (len == 0 && nl == temp.begin()) {
            return (RETURN_SUCCESS); // found 0 chunk
        }
        std::string chunk(temp.begin(), nl);
        if (chunk.size() + 2 != len) {
            return (RETURN_FAILURE);
        }
        appendStringToVector(_body, chunk);
        temp.erase(temp.begin(), nl + 2);
        if (temp.empty()) {
            return (RETURN_FAILURE);
        }
    }
}
