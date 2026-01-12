#ifndef _HTTP_DEBUG_H
#define _HTTP_DEBUG_H 1

#include <cassert>
#include <stdexcept>

#define http_assert(cond) assert(cond)

#define HTTP_ERROR_STR "\033[01;31mERROR\033[0;m:"
#define HTTP_LOG_STR "\033[01;92mLOG\033[0;m:"

#define HTTP_TEST_API /* Indicate a API is for testing; do not use */

/* indicate that a method's thread safety. */
#define HTTP_THREAD_SAFE(message)

#define HTTP_METHODS_FOREACH(X) X(GET) X(HEAD) X(POST)

#endif /* http_debug.h */

#ifndef _HTTP_TEST_H

extern "C" {
/* Optionally implement these. */
void httpTestStart(const char *testname);
void httpTestSuccess(const char *testname);
void httpTestFail(const char *testname);

/* Implement these. */
void httpRecordTestSuccess(const char *testname);
void httpRecordTestFailure(const char *testname);

/* Returns test result. */
int httpTestResult(void (*testcase)(void));
}

#define HTTP_RUN_TEST(testfn)                                                  \
  do {                                                                         \
    httpTestStart(#testfn);                                                    \
    int ret = httpTestResult(testfn);                                          \
    if (ret == 0) {                                                            \
      httpTestSuccess(#testfn);                                                \
    } else {                                                                   \
      httpTestFail(#testfn);                                                   \
    }                                                                          \
  } while (0)

#endif /* http_test.h */

#ifndef _HTTP_HEAD_READER_H
#define _HTTP_HEAD_READER_H 1

#include <cstdio>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

struct SocketProvider;
struct ClientProvider;

/**
 * An interface of reader/writer.
 */
/* interface */ struct Socket {
  virtual ssize_t read(void *buf, size_t len) = 0;
  virtual ssize_t write(const void *buf, size_t len) = 0;
  virtual int getError() { return errno; }
  virtual ~Socket() {}

  static ssize_t xwrite(int fd, const char *buf, size_t count) {
    size_t written = 0;
    while (written < count) {
      ssize_t len = ::write(fd, buf + written, count - written);
      if (len <= 0) {
        return written == 0 ? len : (ssize_t)written;
      }
      written += (size_t)len;
    }
    return (ssize_t)written;
  }

  virtual void close(void) = 0;

  /* For server sockets */
  virtual int setConnectFd(int connectFd, SocketProvider *provider) = 0;

  /* For client sockets */
  virtual int setConnectFd(int connectFd, ClientProvider *provider) {
    return -1;
  }
};

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

/* interface */ struct SocketProvider {
  SocketProvider() = default;

  /**
   * Accept a new connection, and initialize socketPtr.
   *
   * @return 0 on success; else an errno.
   */
  HTTP_THREAD_SAFE("always") virtual int accept(void) = 0;

  virtual ~SocketProvider() = default;
};

struct InetSocketProvider : public SocketProvider {
  ~InetSocketProvider() { ::close(sockFd); }
  static int createListeningSocket(int port);

  InetSocketProvider(int port) {
    sockFd = createListeningSocket(port);
    if (sockFd < 0)
      _exit(1);
  }

  int accept() override {
    struct sockaddr_in addr;
    socklen_t clientLen = sizeof(addr);
    return ::accept(sockFd, (struct sockaddr *)&addr, &clientLen);
  }

protected:
  int sockFd;
};

struct HttpSocket : public Socket {
  HttpSocket() : fd(-1) {}
  virtual ssize_t read(void *buf, size_t len) override {
    return ::read(fd, buf, len);
  }
  virtual ssize_t write(const void *buf, size_t len) override {
    return Socket::xwrite(fd, (const char *)buf, len);
  }
  virtual ~HttpSocket() override {}
  void close() override {
    ::close(this->fd);
    fd = -1;
  }

  int setConnectFd(int fd, SocketProvider *provider) override {
    this->fd = fd;
    return 0;
  }

  int setConnectFd(int fd, ClientProvider *provider) override {
    this->fd = fd;
    return 0;
  }

  int fd;
};

struct HttpHeadReader {
private:
  typedef Socket _Socket_t;
  /* underlying socket. (write) */
  _Socket_t *socket{nullptr};

  void *buffer{nullptr};
  int size{0};

  /* buffer management */
  int offset{0};
  int end{0};

  /* Get last error. */
  int error{0};

  void fill(void) {
    offset = end = 0;
    ssize_t len = socket->read(buffer, size);
    if (len < 0) {
      error = socket->getError();
      return;
    }
    end = (int)len;
  }

  int getch(void) {
    if (offset >= end) {
      fill();
      if (error != 0) {
        return -1;
      }
      if (offset >= end) {
        /* EOF */
        return -1;
      }
    }
    return ((char *)buffer)[offset++];
  }

public:
  HttpHeadReader(int nPages = 16) {
    size = nPages * getpagesize();
    buffer =
        mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (buffer == MAP_FAILED) {
      perror("mmap");
      _exit(1);
    }
  }
  ~HttpHeadReader(void) { munmap(this->buffer, this->size); }

  void *getBuffer() const { return buffer; }
  size_t getSize(void) const { return size; }

  void reset(_Socket_t *sock) {
    this->socket = sock;
    offset = end = 0;
    error = 0;
  }
  int getError() const { return error; }
  void reset(_Socket_t &sock) { reset(&sock); }

  int getLastError() {
    int ret = this->error;
    this->error = 0;
    return ret;
  }

  /**
   * @return next line read from this buffer;
   * @return empty line and error is 0 to indicate end of a valid http header.
   * @return empty line and error is `EINVAL` to indicate early EOF.
   */
  std::vector<char> nextLine(void) {
    std::vector<char> line;
    for (;;) {
      int ch = getch();
      if (ch < 0) { /* early EOF */
        line.clear();
        this->error = EINVAL;
        return line;
      }

      if (ch == '\n') {
        /* end of this line. */
        break;
      }

      if (ch != '\r') {
        /* push it to the buffer. */
        line.push_back(ch);
      }
    }
    return line;
  }

  size_t getOffset() { return offset; }
  size_t getEnd() { return end; }
};

#include <cstring>
#include <map>

struct HttpStringCompare {
  bool operator()(const char *a, const char *b) const {
    return strcmp(a, b) < 0;
  }
};

template <typename _Reader_t /* has nextLine */> struct HttpHeadParser {
private:
  static void appendNextToken(std::vector<char> &dest,
                              const std::vector<char> &line, size_t &pos) {
    const size_t lineSize = line.size();
    while (pos < lineSize && isspace(line[pos])) {
      pos++;
    }
    while (pos < lineSize && !isspace(line[pos])) {
      dest.push_back(line[pos]);
      pos++;
    }
  }

public:
  typedef std::map<const char *, std::vector<char>, HttpStringCompare> table_t;

public:
  _Reader_t *reader{nullptr};
  table_t &table;
  std::vector<char> method;
  std::vector<char> path;

  /**
   * @param myTable a table that sets the keys but leave the values empty;
   *   the parser will update the key only when it exists in the table.
   */
  HttpHeadParser(_Reader_t *myReader, table_t &myTable)
      : reader(myReader), table(myTable) {}

  bool isValid(void) const { return !method.empty() && !path.empty(); }

  /* Do parse. Normally, after this you should check `getLastError` of reader.
   */
  void parse() {
    std::vector<char> line;
    char buf[64]; /* assume that key is less than 64 bytes. */

    auto printLine = [](const std::vector<char> &line) {
      fprintf(stderr, "< ");
      fwrite(line.data(), 1, line.size(), stderr);
      fprintf(stderr, "\n");
    };

    /* Deal with the request line. */
    do {
      line = reader->nextLine();
      size_t i = 0;
      method.reserve(16);
      path.reserve(64);
      appendNextToken(method, line, i);
      appendNextToken(path, line, i);
    } while (0);

    /* Parse the following kv pairs. */
    for (;;) {
      line = reader->nextLine();
      if (line.empty()) {
        break;
      }
      size_t i;
      const auto lineSize = line.size();
      for (i = 0; i < (sizeof(buf) - 1) && line[i] != ':' && i < lineSize;
           i++) {
        buf[i] = line[i];
      }
      buf[i] = 0;
      for (i++; i < lineSize && line[i] == ' '; i++)
        ;
      auto it = table.find(buf);
      if (it != table.end()) {
        /* key found. */
        table[it->first] = std::vector<char>(line.begin() + i, line.end());
      }
    }
  }
};

#endif /* _HTTP_HEAD_READER_H */

#ifndef _HTTPD_H
#define _HTTPD_H 1

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <list>
#include <pthread.h>
#include <string>
#include <vector>

/* #include "http_head_reader.h" */

struct HttpHandler;

enum class HttpConnectionType {
  CLOSE = 0,
  KEEP_ALIVE,
};

enum class HttpMethodType {
  INVALID = 0,
#define X(method) method,
  HTTP_METHODS_FOREACH(X)
#undef X
};

class HttpDaemon {
  typedef Socket &(*SockObjGetType)(int);

public:
  using Connection = HttpConnectionType;
  using Method = HttpMethodType;

  HttpDaemon()
      : requestHandler(nullptr), logger(stderr), provider(nullptr),
        sockAllocator(nullptr), pendingRequests() {}

  /**
   * Normally, `HttpDaemon` runs infinitely and is never destroyed.
   */
  ~HttpDaemon() = default;

  static Connection connFromString(const char *s) {
    return (*s == 0 /* client didn't specify */
            || strcasecmp(s, "close") == 0)
               ? Connection::CLOSE
               : Connection::KEEP_ALIVE;
  }

  typedef HttpHandler *(*handler_t)(int tid);

  /**
   * Start the server (it never returns.).
   */
  void start() __attribute__((noreturn));

  /**
   * @param numRequests number of requests to process before exiting.
   */
  HTTP_TEST_API void run(size_t numRequests = 3);

  HttpDaemon &setRequestHandler(handler_t handler) {
    if (handler == nullptr) {
    }
    requestHandler = handler;
    return *this;
  }

  HttpDaemon &setLogger(FILE *logger) {
    this->logger = logger;
    return *this;
  }

  HttpDaemon &setProvider(SocketProvider &myProvider) {
    provider = &myProvider;
    return *this;
  }

  HttpDaemon &setSocketAllocator(SockObjGetType alloc) {
    sockAllocator = alloc;
    return *this;
  }

  handler_t getRequestHandler(void) const { return this->requestHandler; }

  static constexpr int numWorkers = 4;

private:
  handler_t requestHandler;
  FILE *logger;
  SocketProvider *provider;
  SockObjGetType sockAllocator;
  std::list<int> pendingRequests;

  static void *workerRoutine(void *arg /* = this */);
  static void handleRequest(HttpHeadReader &reader, Socket &socket,
                            HttpHandler *handler, FILE *logger);
  pthread_cond_t reqCond;
  pthread_mutex_t reqMutex;
  pthread_t workers[numWorkers];
  HttpHeadReader readers[numWorkers];

  void initialize();
  void masterRoutine();
};

struct SocketStream {
private:
  typedef Socket _Socket_t;
  _Socket_t *socket;
  void *buffer;
  size_t offset;
  size_t bufferSize;

public:
  SocketStream(_Socket_t *sock = nullptr, void *buf = nullptr, size_t size = 0)
      : socket(sock), buffer(buf), offset(0), bufferSize(size) {}

  void setBuffer(void *buf, size_t size) {
    buffer = buf;
    bufferSize = size;
  }

  void flush() {
    if (offset) {
      socket->write(buffer, offset);
      offset = 0;
    }
  }

  ~SocketStream() { flush(); }

  void putch(char ch) {
    if (offset == bufferSize) {
    }
  }

  void write(const void *src, size_t length) {
    if (offset + length > bufferSize) {
      flush();
    }
    if (length > bufferSize) {
      socket->write(src, length);
      return;
    }
    memcpy((char *)buffer + offset, src, length);
    offset += length;
  }

  void forward(int fd) {
    flush();
    ssize_t len;
    while ((len = ::read(fd, buffer, bufferSize)) > 0) {
      this->offset = (size_t)len;
      flush();
    }
  }
};

SocketStream &operator<<(SocketStream &stream, const std::vector<char> &vec);
SocketStream &operator<<(SocketStream &stream, const char *str);
SocketStream &operator<<(SocketStream &stream, size_t length);

struct HttpHandler {
protected:
  friend class HttpDaemon;

  std::vector<char> path;
  HttpMethodType method;

private:
  void doConsume(Socket *socket, void *buf, size_t bufSize,
                 size_t contentLength) {
    for (size_t remain = contentLength; remain > 0;) {
      auto ret = socket->read(buf, std::min(remain, bufSize));
      if (ret <= 0) {
        break;
      }
      if (!consume(buf, (size_t)ret)) {
        /* consumer stopped reading. */
        break;
      }
      remain -= (size_t)ret;
    }
  }

  void doProduce(Socket *socket, void *buf, size_t bufSize) {
    SocketStream stream(socket, buf, bufSize);
    produce(stream);
    stream.flush();
  }

public:
  HttpHandler() : path(), method(HttpMethodType::INVALID) {}
  virtual ~HttpHandler() {}

  virtual int getStatusCode(void) = 0;

  /**
   * Consumes the body of request.
   *
   * @return true when it can cosume more data.
   */
  virtual bool consume(const void *buf, size_t length) = 0;

  /**
   * Produces the header and then the body of response.
   *
   * @return the length of actual produced data.
   */
  virtual void produce(SocketStream &stream) = 0;

  virtual void setRequestMethod(HttpMethodType method) {
    this->method = method;
  }

  virtual void setRequestPath(std::vector<char> &myPath) {
    this->path = std::move(myPath);
  }

  /* Set the connection state requested by client. */
  virtual void setConnection(HttpConnectionType conn) {
    /* do nothing by default. */
  }

  /* Handler returns whether it wants to close the connection after `produce.`
   */
  virtual bool connectionClosed(void) const { return true; }
};

/**
 * <h1>File/Directory Utilities<h1>
 *
 * <p>All the methods will write a complete HTTP response
 * (i.e. also the response header) to the stream, and returns
 * its stauts code.</p>
 */
namespace HttpUtil {

int render404(SocketStream &socket);
int render500(SocketStream &socket, int err = 0);
int renderFile(SocketStream &socket, const char *path);
void renderDirectory(SocketStream &socket, const char *path);

} /* namespace HttpUtil */

#endif // _HTTPD_H

#ifndef _HTTP_UTIL_H
#define _HTTP_UTIL_H 1

#endif /* _HTTP_UTIL_H */

#ifndef _HTTP_CLIENT_H
#define _HTTP_CLIENT_H 1

struct HttpClientHandler : public HttpHandler {
private:
  const char *host;
  HttpConnectionType connection;

public:
  HttpClientHandler(const std::vector<char> &myHost)
      : host(myHost.data()), connection(HttpConnectionType::CLOSE) {

    http_assert(myHost.size() && myHost.back() == 0 &&
                "host is not null-terminated");
  }

  HttpClientHandler(const char *myHost)
      : host(myHost), connection(HttpConnectionType::CLOSE) {}

  void setConnection(HttpConnectionType conn) override {
    this->connection = conn;
  }

  int getStatusCode() override { http_assert(0 && "not reachable"); }

  bool connectionClosed() const { http_assert(0 && "do not use this method"); }

  /**
   * This is a simple working produce method. Possible extensions
   * include writing cookies into stream, etc.
   */
  virtual void produce(SocketStream &stream) override {
    /* Method Path Version */
#define X(myMethod)                                                            \
  case (HttpMethodType::myMethod): {                                           \
    stream << #myMethod " ";                                                   \
    break;                                                                     \
  }

    switch (this->method) {
      HTTP_METHODS_FOREACH(X)
    default:
      http_assert(0 && "Invalid method");
    }

    http_assert(path.size() && path.back() == 0 &&
                "path is not null-terminated");
    stream.write(path.data(), path.size() - 1);
    stream << " HTTP/1.1\r\n";

    stream << "Host: ";
    stream << this->host;

    switch (connection) {
    case (HttpConnectionType::CLOSE):
      stream << "\r\nConnection: close\r\n";
      break;
    case (HttpConnectionType::KEEP_ALIVE):
      stream << "\r\nConnection: keep-alive\r\n";
      break;
    }
    stream
        << "User-Agent: curl/7.81.0\r\nAccept: */*\r\n\r\n"; /* end of head */
    stream.flush();

#undef X
  }
};

struct ClientProvider {
  virtual ~ClientProvider() = default;

  /**
   * @return a socket file descriptor for connection;
   * < 0 on failure.
   */
  virtual int connect(void) = 0;
};

struct HttpClientProvider : public ClientProvider {
private:
  struct addrinfo *info;

public:
  HttpClientProvider(const char *host, int port = 80);

  /**
   * Open a new connection with `this->info`.
   */
  int connect(void) override;

  ~HttpClientProvider();
};

using HttpClientSocket = HttpSocket;

class HttpClient {
private:
  HttpHeadReader reader;
  Socket &socket;
  ClientProvider &provider;
  HttpClientHandler &handler;
  bool connected;

  static bool isNullTerminate(const std::vector<char> &vec) {
    return vec.size() && vec.back() == 0;
  }

public:
  HttpClient(Socket &mySocket, ClientProvider &myProvider,
             HttpClientHandler &myHandler)
      : reader(), socket(mySocket), provider(myProvider), handler(myHandler),
        connected(false) {}
  int request(HttpMethodType method, std::vector<char> path, bool isLast);

  ~HttpClient() {
    if (connected) {
      socket.close();
    }
  }

  bool closed(void) const { return !connected; }
};

#endif /* _HTTP_CLIENT_H */
