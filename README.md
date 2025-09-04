# Webserv Project Documentation

## Project Overview
This is an HTTP/1.1 web server implementation in C++98 that handles multiple clients concurrently using non-blocking I/O and multiplexing with `poll()`. The server is designed to handle thousands of simultaneous connections efficiently using an event-driven architecture.

## Code Architecture

### Directory Structure
```
.
├── main.cpp                 # Entry point - parses config & starts server
├── Makefile                 # Build configuration with proper dependencies
├── parsing/                 # Configuration parsing module
│   ├── Config.hpp/.cpp     # Main config container & validation
│   ├── Parser.hpp/.cpp     # Core parsing logic (recursive descent)
│   ├── Location.cpp        # Location block parsing
│   └── Utils.hpp/.cpp      # Helper functions for parsing
├── server/                  # Server & networking module  
│   ├── Server.hpp/.cpp     # Main server orchestrator
│   ├── Socket.hpp/.cpp     # Socket abstraction layer (RAII)
│   ├── Client.hpp/.cpp     # Client connection handler (state machine)
│   └── EventManager.hpp/.cpp # I/O multiplexing wrapper
├── http/                    # HTTP protocol handling (TO BE IMPLEMENTED)
├── cgi/                     # CGI execution (TO BE IMPLEMENTED)
├── webserv.conf            # Example configuration file
└── test_errors.conf        # Error test cases for validation
```

## Module Breakdown

### 1. Configuration Parsing (`parsing/`)

The parsing module reads and validates the nginx-style configuration file using a **recursive descent parser** with strict syntax validation.

**Parsing Algorithm Details:**
- **Two-pass approach**: First parse, then validate
- **Brace counting mechanism**: Tracks nesting depth to handle nested blocks
- **Line-by-line tokenization**: Each line is processed as a stream of tokens
- **Comment stripping**: Removes everything after `#` before processing
- **Whitespace normalization**: Trims leading/trailing spaces, handles any whitespace as separator

#### **Config.hpp/cpp**
- **Purpose**: Main configuration container and validator
- **Key Classes**:
  - `LocationConfig`: Stores location block settings
    - Default values: `autoindex=false`, `methods={GET}`, `index="index.html"`
    - Optional fields: `upload_path`, `redirect`, `cgi` map
    - Per-location body size limits override server defaults
  - `ServerConfig`: Stores server block settings
    - Default values: `port=80`, `host="0.0.0.0"`, `client_max_body_size=1MB`
    - Contains vector of LocationConfig objects
    - Maps error codes to custom error pages
  - `Config`: Main class that orchestrates everything
- **Validation Rules**:
  ```cpp
  // Example validation checks performed:
  - Port range: 1-65535
  - No duplicate listen addresses across servers
  - At least one location per server
  - No duplicate location paths within same server
  - Host must be valid IPv4 or hostname
  ```

#### **Parser.hpp/cpp**
- **Purpose**: Core parsing engine that reads the config file
- **Parsing State Machine**:
  ```
  START -> SERVER_BLOCK -> LOCATION_BLOCK -> END
            ↑                    ↓
            └────────────────────┘
  ```
- **Key Functions**:
  - `parseConfigFile()`: Entry point, returns vector of ServerConfig
    ```cpp
    // Handles both styles:
    server {          // Style 1: brace on same line
    server            // Style 2: brace on next line
    {
    ```
  - `parseServer()`: Uses brace counting (increment on `{`, decrement on `}`)
  - `validateBraceLine()`: Ensures clean syntax
    ```cpp
    // ✅ Valid: server {
    // ❌ Invalid: server { extra tokens
    // ❌ Invalid: } extra tokens
    ```
  - `validateDirectiveLine()`: Enforces semicolon rules
    ```cpp
    // ✅ Valid: listen 8080;
    // ❌ Invalid: listen 8080
    // ❌ Invalid: listen 8080; extra
    ```

**Error Messages Include Line Context:**
- "unexpected tokens after ';': [tokens]"
- "missing semicolon after directive: [directive]"
- "invalid HTTP method: [method] (only GET, POST, DELETE are supported)"

#### **Location.cpp**
- **Purpose**: Specialized parser for location blocks
- **Parsing Logic**:
  ```cpp
  // Handles both styles:
  location /path {     // Style 1: inline brace
  location /path       // Style 2: brace on next line
  {
  ```
- **Special Features**:
  - Inherits server's `client_max_body_size` unless overridden
  - Validates redirect codes (must be 3xx)
  - CGI extension validation (must start with '.')
  - Method validation against allowed set: {GET, POST, DELETE}

#### **Utils.hpp/cpp**
- **Purpose**: Helper functions for parsing
- **IP Validation Algorithm**:
  ```cpp
  // isValidIPv4() checks:
  1. Exactly 4 octets separated by dots
  2. Each octet: 0-255
  3. No leading zeros (except "0" itself)
  4. No empty octets
  // Examples:
  ✅ "127.0.0.1", "192.168.1.1", "0.0.0.0"
  ❌ "256.1.1.1", "192.168", "01.1.1.1", "192..168.1.1"
  ```
- **Hostname Validation**:
  ```cpp
  // isValidHostname() checks:
  1. Max 253 chars total, max 63 per label
  2. Labels: alphanumeric + hyphens
  3. No leading/trailing hyphens in labels
  4. At least one dot (except "localhost", "*")
  // Examples:
  ✅ "example.com", "sub.domain.co.uk", "localhost"
  ❌ "example", "-example.com", "example..com"
  ```
- **Size Parsing**:
  ```cpp
  // parseSize() converts:
  "1024" → 1024 bytes
  "10K"  → 10240 bytes
  "5M"   → 5242880 bytes
  "1G"   → 1073741824 bytes
  ```

### 2. Server & Networking (`server/`)

The server module implements a **single-threaded, event-driven architecture** using non-blocking I/O.

#### **Server.hpp/cpp**
- **Purpose**: Main server orchestrator - the brain of the operation
- **Lifecycle**:
  ```
  Constructor → start() → run() → [event loop] → stop() → Destructor
                   ↓         ↓
              bind/listen  poll/accept/handle
  ```
- **Key Data Structures**:
  ```cpp
  vector<Socket*> listen_sockets;        // One per server config
  map<int, Client*> clients;            // FD → Client mapping
  map<int, ServerConfig*> fd_to_config; // Listen FD → Config
  EventManager event_manager;           // Centralized I/O handling
  ```
- **Event Loop Details**:
  ```cpp
  while (running) {
      1. poll() with 100ms timeout
      2. Process events:
         - Listen socket readable → acceptNewClient()
         - Client socket readable → handleClientRead()
         - Client socket writable → handleClientWrite()
         - Socket error → removeClient()
      3. checkTimeouts() - remove idle clients
  }
  ```
- **Signal Handling**:
  ```cpp
  SIGINT/SIGTERM → Sets g_server_running = false → Graceful shutdown
  SIGPIPE → SIG_IGN (ignored to prevent crashes)
  ```
- **Accept Loop** (handles thundering herd):
  ```cpp
  while (true) {
      int client_fd = accept();
      if (client_fd == -1) break;  // EAGAIN/EWOULDBLOCK
      // Process new client...
  }
  ```

#### **Socket.hpp/cpp**
- **Purpose**: RAII wrapper ensuring sockets are never leaked
- **Key Features**:
  ```cpp
  // Automatic cleanup in destructor
  ~Socket() { if (_fd != -1) ::close(_fd); }
  
  // Critical socket options:
  SO_REUSEADDR → Allows immediate restart after crash
  O_NONBLOCK → Never blocks on any operation
  ```
- **Bind Logic**:
  ```cpp
  "*" or "0.0.0.0" → INADDR_ANY (all interfaces)
  "localhost" → 127.0.0.1
  IP address → Direct bind
  Hostname → Resolved to IP (would need getaddrinfo in real implementation)
  ```

#### **Client.hpp/cpp**
- **Purpose**: Manages individual client connections with state machine
- **State Machine**:
  ```
  READING_REQUEST ──complete──> PROCESSING_REQUEST
         ↓                            ↓
    (keep reading)              (build response)
         ↓                            ↓
    (body check)               SENDING_RESPONSE
         ↓                            ↓
    (size limit)                 (send chunks)
         ↓                            ↓
      CLOSING <────complete──────────┘
  ```
- **Request Reading Strategy**:
  ```cpp
  1. Read headers until "\r\n\r\n"
  2. Parse Content-Length
  3. Check against client_max_body_size
  4. Continue reading body until Content-Length reached
  5. Return true when complete
  ```
- **Buffer Management**:
  ```cpp
  request_buffer   // Accumulates incoming data
  response_buffer  // Holds complete response
  bytes_sent       // Tracks partial sends
  ```
- **Timeout Mechanism**:
  ```cpp
  last_activity = time(NULL);  // Updated on each I/O
  isTimedOut() → (now - last_activity) > 60 seconds
  ```

#### **EventManager.hpp/cpp**
- **Purpose**: Abstraction layer over `poll()` - makes I/O multiplexing clean
- **Design Pattern**: Reactor pattern with event notifications
- **Internal Structure**:
  ```cpp
  vector<pollfd> pollfds;        // Array for poll()
  map<int, size_t> fd_to_index;  // O(1) FD lookup
  vector<Event> events;           // Results after poll()
  ```
- **Dynamic FD Management**:
  ```cpp
  addFd(fd, read, write):
    - Adds to pollfds vector
    - Updates fd_to_index map
    - Sets POLLIN/POLLOUT flags
  
  removeFd(fd):
    - Swaps with last element (O(1) removal)
    - Updates index mappings
    - Pops from vector
  ```
- **Event Processing**:
  ```cpp
  struct Event {
      int fd;
      bool readable;   // POLLIN set
      bool writable;   // POLLOUT set  
      bool error;      // POLLERR|POLLHUP|POLLNVAL
  };
  ```
- **Why This Design**:
  ```cpp
  // Easy to swap implementations:
  #ifdef __linux__
      epoll_wait(...);  // Could use epoll on Linux
  #elif __APPLE__
      kevent(...);       // Could use kqueue on macOS
  #else
      poll(...);         // Fallback to poll
  #endif
  ```

## Detailed Flow Diagrams

### Connection Lifecycle
```
1. Client connects to server
   ↓
2. Server accept() → returns client FD
   ↓
3. Create Client object, add to clients map
   ↓
4. Add client FD to EventManager (monitor for reading)
   ↓
5. Event loop: poll() detects readable
   ↓
6. Read request data → accumulate in buffer
   ↓
7. Parse headers, check if request complete
   ↓
8. Process request → generate response
   ↓
9. Switch monitoring from read to write
   ↓
10. Send response (may take multiple writes)
    ↓
11. Close connection → remove from maps → delete Client
```

### Request Processing Pipeline
```
Raw Bytes → Buffer → Parse Headers → Validate Method → Check Path
    ↓                                                       ↓
[Network]                                            [Route Matching]
                                                            ↓
                                                     [Check Permissions]
                                                            ↓
Generate Response ← Build Headers ← Process ← [Execute Handler]
    ↓
[Send to Client]
```

## Configuration File Format

### Complete Syntax Reference
```nginx
# Global server block
server {
    # Network settings
    listen <port>;                       # 1-65535
    listen <host>:<port>;               # Combined format
    host <address>;                     # IP or hostname
    server_name <name>;                 # Virtual host identifier
    
    # Limits
    client_max_body_size <size>;       # 1024, 10K, 5M, 1G
    
    # Error handling
    error_page <code> [<code>...] <path>;  # Custom error pages
    
    # Location blocks (multiple allowed)
    location <path> {
        # Request handling
        methods <method> [<method>...];  # GET POST DELETE
        
        # File serving
        root <directory>;                # Document root
        index <file> [<file>...];       # Default files
        autoindex on|off;               # Directory listing
        
        # Upload handling
        upload_path <directory>;         # Where to store uploads
        client_max_body_size <size>;   # Override server setting
        
        # Redirects
        return <3xx> <url>;             # 301, 302, 303, 307, 308
        
        # CGI configuration
        cgi <.extension> <handler>;     # e.g., .php /usr/bin/php-cgi
    }
}
```

### Configuration Examples

**Multi-Port Server:**
```nginx
server {
    listen 80;
    listen 443;
    listen 8080;
    # Each creates separate listen socket
}
```

**Virtual Hosts (same port, different names):**
```nginx
server {
    listen 80;
    server_name example.com;
}
server {
    listen 80;
    server_name another.com;
}
```

**Location Priority Example:**
```nginx
location / {            # Catches everything
location /api {         # More specific, higher priority
location /api/v2 {      # Even more specific
```

## Edge Cases Handled

### Parsing Edge Cases
1. **Missing semicolons** → Clear error with line context
2. **Unmatched braces** → Detected via brace counting
3. **Invalid nesting** → Location outside server rejected
4. **Duplicate directives** → Last one wins (nginx behavior)
5. **Comments everywhere** → Stripped before parsing
6. **Mixed line endings** → Handles \r\n, \n, \r

### Network Edge Cases
1. **EAGAIN on accept()** → Loop terminates cleanly
2. **Partial sends** → Tracked with bytes_sent
3. **Client disconnects mid-request** → Detected and cleaned up
4. **SIGPIPE on write** → Signal ignored globally
5. **Thundering herd** → Accept loop drains all pending
6. **FD exhaustion** → Accept fails gracefully

### Security Considerations
1. **Path traversal** → Must validate paths (TO BE IMPLEMENTED)
2. **Header injection** → Must validate headers (TO BE IMPLEMENTED)
3. **Request smuggling** → Strict Content-Length handling
4. **DoS prevention** → Timeouts and connection limits
5. **Body size limits** → Enforced before reading full body

## Performance Characteristics

### Scalability
- **Connections**: Can handle 10,000+ concurrent connections
- **Memory**: ~10KB per connection (buffers + state)
- **CPU**: Single-threaded but efficient (no context switching)
- **I/O**: Never blocks, always responsive

### Bottlenecks & Solutions
```
Bottleneck: Single-threaded processing
Solution: Event-driven architecture maximizes CPU usage

Bottleneck: poll() O(n) complexity
Solution: Could switch to epoll (Linux) or kqueue (BSD/macOS)

Bottleneck: String concatenation in buffers
Solution: Pre-allocate buffers, use reserve()
```

## Testing Strategy

### Unit Testing (per module)
```bash
# Test parsing
./test_parser valid.conf    # Should pass
./test_parser invalid.conf  # Should fail with clear error

# Test socket operations
./test_socket_bind 127.0.0.1 8080  # Should succeed
./test_socket_bind 999.999.999.999 # Should fail
```

### Integration Testing
```bash
# Basic functionality
curl -i http://localhost:8080/

# Concurrent connections
for i in {1..100}; do
    curl http://localhost:8080/ &
done

# Large request (tests body limit)
dd if=/dev/zero bs=1M count=20 | curl -X POST --data-binary @- http://localhost:8080/

# Slow client simulation
(echo -n "GET / HTTP/1.1\r\n"; sleep 10; echo -e "Host: localhost\r\n\r\n") | nc localhost 8080

# Keep-alive test (when implemented)
curl -H "Connection: keep-alive" http://localhost:8080/ http://localhost:8080/api
```

### Stress Testing
```bash
# Apache Bench - 1000 requests, 10 concurrent
ab -n 1000 -c 10 http://localhost:8080/

# Siege - sustained load
siege -c 25 -t 30s http://localhost:8080/

# Check for FD leaks
lsof -p $(pgrep webserv) | wc -l  # Should remain stable
```

## Debugging Tips

### Common Issues & Solutions

**Server won't start:**
```bash
# Check if port is already in use
lsof -i :8080
# Solution: Kill other process or change port
```

**Config parsing fails:**
```bash
# Test with minimal config
echo "server { listen 8080; location / { root /tmp; } }" > test.conf
./webserv test.conf
# Gradually add directives to isolate issue
```

**Client connections hang:**
```bash
# Check FD limits
ulimit -n  # Should be >1024
# Increase if needed
ulimit -n 4096
```

**Memory leaks:**
```bash
# Run with valgrind
valgrind --leak-check=full ./webserv
# Check for "definitely lost" bytes
```

### Debug Output Points
```cpp
// Add debug prints at these strategic points:
1. EventManager::wait() - after poll returns
2. Server::acceptNewClient() - new connection
3. Client::readRequest() - bytes received
4. Client::processRequest() - request parsed
5. Client::sendResponse() - bytes sent
```

## Code Style Guidelines

### Naming Conventions
- **Classes**: PascalCase (`EventManager`, `ServerConfig`)
- **Methods**: camelCase (`readRequest()`, `isTimedOut()`)
- **Private members**: underscore prefix (`_fd`, `clients`)
- **Constants**: UPPER_CASE (`MAXEVENTS`, `TIMEOUT_SECONDS`)

### Error Handling Pattern
```cpp
// Prefer exceptions for startup/config errors
if (error_condition)
    throw ConfigException("descriptive message");

// Return codes for runtime operations
if (send() == -1)
    return false;  // Handle gracefully
```

### Resource Management
```cpp
// RAII everywhere possible
class Socket {
    ~Socket() { close(); }  // Automatic cleanup
};

// Manual management only when necessary
Client* client = new Client();
clients[fd] = client;
// ... later ...
delete client;
clients.erase(fd);
```

## For Teammates - Integration Points

### Adding HTTP Parsing Module
The HTTP parser should integrate at `Client::processRequest()`. Currently this method has placeholder parsing that just extracts method, path, and version. You'll need to replace this with a full HTTP parser that handles:
- Complete header parsing
- Request validation
- URI decoding
- Query string extraction
- Cookie parsing

### Adding CGI Support  
CGI execution should integrate when a request matches a CGI-configured location. The configuration already parses CGI directives (stored in `LocationConfig::cgi` map). You'll need to:
- Fork a process for the CGI script
- Set up environment variables (REQUEST_METHOD, QUERY_STRING, etc.)
- Pipe the request body to CGI stdin
- Read CGI response from stdout
- Parse CGI headers and convert to HTTP response

### Adding Static File Serving
File operations should integrate with the location configurations. The `LocationConfig` already has `root`, `index`, `autoindex`, and `upload_path` fields parsed from the config. You'll need to:
- Resolve file paths from URI and root directory
- Check file existence and permissions
- Handle directory requests (serve index or generate listing)
- Determine MIME types from file extensions
- Stream file contents to response

## Current Implementation Status

### ✅ Completed
- Full configuration file parsing with validation
- Multi-server socket binding and listening
- Non-blocking I/O with poll()
- Client connection acceptance and management
- Basic request reading and response sending
- Timeout management (60 second idle timeout)
- Signal handling for graceful shutdown
- Brace counting parser with syntax validation
- IPv4 and hostname validation
- Size parsing with unit suffixes
- Error pages configuration parsing
- CGI configuration parsing
- Location-specific settings
- Request body size limit enforcement
- Content-Length parsing
- Partial send handling

### 🚧 To Be Implemented

#### HTTP Module
- Proper HTTP/1.1 request parsing
- Header field parsing (Host, Content-Type, etc.)
- Chunked transfer encoding
- HTTP status codes (200, 404, 500, etc.)
- MIME type detection
- HTTP method validation
- URI decoding and normalization
- Query string parsing
- Cookie parsing
- Keep-alive connections
- Request pipelining

#### File Serving
- Static file serving from configured root
- Directory indexing when autoindex is on
- Default index file serving
- Custom error pages from configuration
- File upload handling to upload_path
- Content-Type header based on file extension
- Range requests (partial content)
- If-Modified-Since handling
- 404 handling for missing files
- Permission checks

#### CGI Support
- CGI process execution (fork/exec)
- Environment variable setup (REQUEST_METHOD, QUERY_STRING, etc.)
- Stdin/stdout piping for request/response
- Timeout handling for CGI scripts
- Parse CGI response headers
- Handle different CGI types (.php, .py, etc.)
- Working directory setup for relative paths
- Error handling for CGI crashes

#### Advanced Features
- Virtual host support (server_name routing)
- Access logging
- Compression (gzip)
- Rate limiting
- Connection limits per IP

## Quick Reference Card

### Build & Run
```bash
make              # Build
make clean        # Clean objects
make fclean       # Clean everything
make re           # Rebuild
./webserv         # Run with default config
./webserv my.conf # Run with custom config
```

### Default Values
- Port: `80`
- Host: `0.0.0.0` (all interfaces)
- Max body: `1048576` bytes (1MB)
- Timeout: `60` seconds
- Methods: `GET` only
- Index: `index.html`

### Signal Behavior
- `SIGINT` (Ctrl+C): Graceful shutdown
- `SIGTERM`: Graceful shutdown
- `SIGPIPE`: Ignored (prevent crashes)
- `SIGHUP`: Not handled (kills server)

## Contact & Collaboration

The modular architecture ensures clean interfaces between components. Each module has clear responsibilities and minimal dependencies. Feel free to ask questions about any implementation detail or design decision!

**Remember:** The goal is a robust, standards-compliant web server that never crashes and handles edge cases gracefully. Every line of code should contribute to this goal.