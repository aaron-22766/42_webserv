<h1 align="center">
	<p>
	webserv
	</p>
	<img src="https://github.com/aaron-22766/aaron-22766/blob/main/42-badges/webservm.png">
</h1>

<p align="center">
	<b><i>This is when you finally understand why a URL starts
with HTTP</i></b><br><br>
</p>

<p align="center">
	<img alt="GitHub code size in bytes" src="https://img.shields.io/github/languages/code-size/aaron-22766/42_webserv?color=lightblue" />
	<img alt="Code language count" src="https://img.shields.io/github/languages/count/aaron-22766/42_webserv?color=yellow" />
	<img alt="GitHub top language" src="https://img.shields.io/github/languages/top/aaron-22766/42_webserv?color=blue" />
	<img alt="GitHub last commit" src="https://img.shields.io/github/last-commit/aaron-22766/42_webserv?color=green" />
</p>

---

## 🗣 About

This project is about writing your own **HTTP server**.
You will be able to test it with an actual browser.
*HTTP* is one of the most used protocols on the internet.
Knowing its arcane will be useful, even if you won’t be working on a website.

I teamed up with [Triin](https://github.com/triimar) and [María José](https://github.com/mtravez) for this one.

## 🛠 Usage

```shell
make
./webserv <optional config file>
```

## 💬 Description

This program takes a *nginx*-like configuration file as argument, in which you can specify rules for each server. The program listens on the server **sockets** for incoming clients and accepts the connection. It reads the **HTTP request** from the client socket, parses it and generates an applicable **HTTP response** according to the server's config. If one of the supported **CGI script** file types is requested, the server executes the script and generates a response from it's output. We utilize the *poll()* function to achieve **I/O Multiplexing**, which means the program can handle multiple clients almost concurrently, even though it's only a single-threaded program.

What's supported:
* HTTP version: **HTTP/1.1**
* Request methods: **GET**, **HEAD**, **POST** & **DELETE**
* CGI scripts: **shell** (`.sh`), **python** (`.py`) & **perl** (`.pl`)

### Config file

* Define a server using a `server { ... }` block
* In the server, define a location/route with it's own rules in a `location [path] { ... }` block
* Each rule in the must be delimited by a `;`
* **Server-only rules**:
    - `host [ip address]`
    - `listen [port]`
    - `server_name [names]`
    - `client_size [number]`
    - `client_body_size: [number]` (in bytes)
    - `error_page [status code] [local path]`
* **Server or location rules**:
    - `root [local path]` where the server or location is mounted (required for server)
    - `index [file names]` for default file on directory request
    - `autoindex [true/false]` for generating directory listing (on by default)
    - `cgi_info [list sh/py/pl]` for allowed CGI script extensions
    - `redirect [URL or URI]`
* **Location-only rules**:
    - `allow [request methods]` (allows everything if not specified)
* A rule not specified in location, defaults to rule in server

**Example:**
```nginx
server {
    listen  8080;
    host    127.0.0.1;
    server_name webserv.default;
    root    ./www;
    error_page  404 ./www/error-pages/404.html;
    client_size 500;
    client_body_size 10240;
    index index.html index.htm;

    location /html {
        allow GET POST DELETE;
        index index.html index.htm;
        autoindex true;
    }

    location /cgi-bin {
        allow GET POST;
        index index.py;
        autoindex false;
        cgi_info py pl sh;
    }

    location /redir {
        redirect https://www.google.com;
        allow GET;
    }
}
```

### Program

* The config file is parsed into `std::vector<Server>`
* The servers are started
    - **socket()** creates socket
    - **setsockopt()** configures the socket
    - **bind()** binds the socket to the ip address
    - **listen()** specifies a willingness to accept incoming connections and a queue limit for incoming connections
    - **fcntl()** sets the socket to non-blocking for multiplexing
* Since we support virtual servers (servers on same ip and port but different server_name), we organize them using their socked file descriptor: `std::map<int, std::vector<Server> >`
* Socket file descriptors are also added to a `std::vector<pollfd>` where the servers are in front and possible client sockets afterwards
* The main loop is started
* **poll()** checks to open sockets for events and returns a count
* If the event is on a server socket and `POLLIN`, new connections are accepted:
    - **accept()** takes the server's socket and other info and returns a socket file descriptor for the client
    - Client object is created and added to a `std::map<int, Client>`
* If the event is on a client socket and `POLLIN`
    - **read()** on the client socket 1024 bytes at a time
    - each Client object has a request attribute
    - `Request::process()` parses the buffer
    - this is done until `Request::requestComplete()`, which depends on various factors
* If the event is on a client socket and `POLLOUT`
    - `Response()` constctor takes the request and the client's server and constructs the response
    - response content is saved in a `std::vector<char>` in the client object and is retrieved from `Response::getResponse()`
    - **send()** tries to send the entire response, if it fails to do that, the successful sent portion is removed from the vector and the rest is left to be sent again until the entire response was sent
* **I/O Multiplexing** using **poll()** makes it possible that the program handles other events while waiting for clients and therefore drastically increases the amount of load the program can handle at the same time
* Info about connected clients is printed in realtime on the terminal

### Request

* Needs to be **HTTP/1.1** request
* Each line must end in **CRLF** (\r\n)
* First line specifies request method, URI and HTTP version
* Following lines specify headers `key: value`
* Empty line marks the end of the headers
* Optional body follows after

**Impactful headers:**
* `Host` specifies the server name the request should be handles by, especially helpful when having virtual servers (same port)
* `Connection` `close` closes client immediately, defaults to `keep-alive` for 60 secs
* `Content-Type` is a **MIME-Type** (i.e. text/plain) that specifies how the body is to be interpreted (required for **POST**)
* `Content-Length` specifies how much of the body to accept
* `Tranfer-Encoding: chunked` means body is in chunks, hex number on a line followed by that amount of bytes, empty chunk marks the end
    - either one of the last two is required for **POST**

**Example requests:**
```http
GET /uri HTTP/1.1
Connection: close
Host: webserv.default

```
```http
POST /test.txt HTTP/1.1
Content-Type: text/plain
Content-Length: 11

Hello World
```
```http
DELETE /test.txt HTTP/1.1

```

### Response

#### Processing

* Whenever there is some error, the status is thrown as an exception
* If the request parsing encountered errors, this status code is returned
* Checks if request method is not allowed in location -> `405`
* If the locations specifies redirect -> set `Location` header & `301`
* Builds local path from locations's root and request path (from uri)
* Checks if request body is bigger than server's client_body_size -> `413`
* If the path matches a CGI script, it is processed differently
* If the path does not exist (and not `POST`) -> `404`

#### GET

* No read permissions -> `403`
* If the path is a file, it is read into the body `std::vector<char>`
* If it's a directory
    - and does not end in a slash -> set `Location` to path with slash & `301`
    - and index file is in the location config, and it exists with read permissions, the file is read
    - if autoindex is on, a directory listing is generated
* Converts file extension to **MIME-Type** -> fails `415`
* Was processed successfully, sets `Content-Type` and `Last-Modified` header -> status `200`

#### HEAD

* Gets processed just like **GET** but does not send body

#### POST

* Converts `Content-Type` header to file extension -> fails `415`
* If the path is a directory, it creates an "index." file
* If a file already exists in the path -> `403`
* All non-existent directories are created -> fail `500`
* File is created and written to -> fail `500`
* Was processed successfully, sets `Location` header -> status `201`

#### DELETE

* If path is a file, tries to delete it -> fail `500`
* If it's a directory, query string must be "force" -> `409`
* No write permissions on path -> `403`
* Deletes entire contents of directory -> fail `500`
* Was processed sucessfully -> status `204`

#### CGI

* Is a valid CGI if:
    - request method is **GET** or **POST**
    - if path is a directory, checks if listed index are valid CGI
    - has one of the supported extensions: `.sh`, `.py` or `.pl`
* If any of these is false, it's handled as a normal request
* If `cgi_info` does not allow file extension -> `403`
* If file does not have or has an invalid shebang and interpreter -> fail `500`
* Pipes for redirecting output and input
* For **POST**, request body is written to pipe that gets redirected to child process' stdin
* Child process is created
    - Redirections set up
    - Directory is changed for relative file access
    - Arguments are allocated from shebang interpreter and arguments, plus the cgi path
    - Environment array is allocated with a bunch of different variables the script might need
    - **execve()** is called with arguments and environment
* Parent process waits for child, timeouts after 3 sec -> `503`
* Pipe from stdout is read and output is interpreted
    - if empty -> `204`
    - cgi output has either '\n' or '\r\n' at the end of each line
    - cgi headers are parsed -> fail `500`
* CGI headers that have a function
    - `Status` sets the response status
    - `Location` specifies redirection - absolute URL is redirect `302`; relative URI gets processed again through the response (keeps a history and max 5 redirections)
    - `Content-Type` is required
    - `Transfer-Encoding: chunked` is unchunked
    - `Content-Length` if set, body is trimmed to that length, otherwise header is set to length of body


#### Constructing

* If the status is an error (above 400), attempts to read `error_page` file to body
* Each line ends in **CRLF** (\r\n)
* First line specifies HTTP version, status code and status message
* Headers `Server`, `Date` (in GMT) and `Content-Length` are always set
* Headers are inserted with format `key: value` 
* After headers and empty line, follows optional body and **CRLFCRLF**

Example responses to the requests above:
```http
HTTP/1.1 404 Not Found
Server: webserv.default
Date: Wed, 13 Mar 2024 12:10:08 GMT
Content-Length: 417

[body has 404 error page html]
```
```http
HTTP/1.1 201 Created
Server: webserv.default
Date: Wed, 13 Mar 2024 12:11:46 GMT
Content-Length: 0
Location: /test.txt

```
```http
HTTP/1.1 204 No Content
Server: webserv.default
Date: Wed, 13 Mar 2024 12:13:08 GMT
Content-Length: 0

```

### Testing

We used these programs to test our webserver:

* **telnet**
* **Postman**
* **Firefox**
* **Google Chrome**
* **siege** (Availability is above 99.7%)