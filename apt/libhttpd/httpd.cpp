#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "httpd.h"

static HttpDaemon::Method methodFromString(const char *s);

extern "C" {

int httpTestResult(void (*testcase)(void)) {
  pid_t ret = fork();
  if (ret < 0) {
    return errno;
  }
  if (ret == 0) {
    /* child */
    testcase();
    _exit(0);
  } else {
    /* parent */
    int status;
    waitpid(ret, &status, 0);
    if (WIFEXITED(status)) {
      return WEXITSTATUS(status);
    } else {
      return -1;
    }
  }
  return 0;
}

__attribute__((weak)) void httpRecordTestSuccess(const char *) {}
__attribute__((weak)) void httpRecordTestFailure(const char *) {}

__attribute__((weak)) void httpTestStart(const char *testname) {
  fprintf(stderr, "\n\033[01;92m[----------]\033[0;m\n");
  fprintf(stderr, "\033[01;92m[ RUN      ]\033[0;m %s\n", testname);
}

__attribute__((weak)) void httpTestSuccess(const char *testname) {
  httpRecordTestSuccess(testname);
  fprintf(stderr, "\033[01;92m[       OK ]\033[0;m\n");
  fprintf(stderr, "\033[01;92m[----------]\033[0;m\n");
}

__attribute__((weak)) void httpTestFail(const char *testname) {
  httpRecordTestFailure(testname);
  fprintf(stderr, "\033[01;31m[     FAIL ]\033[0;m\n");
  fprintf(stderr, "\033[01;31m[----------]\033[0;m\n");
}
}

void HttpDaemon::initialize() {
  /*
   * Set the correct signal handlers.
   *
   * In particular, we reset the signal handler
   * of SIGCHLD to SIG_DFL to make function calls
   * to system(3) or popen(3) work.
   */
  signal(SIGCHLD, SIG_DFL);
  signal(SIGPIPE, SIG_IGN);
  signal(SIGINT, SIG_IGN);

  /* Create worker threads. */
  reqMutex = PTHREAD_MUTEX_INITIALIZER;
  reqCond = PTHREAD_COND_INITIALIZER;
  for (int i = 0; i < numWorkers; ++i) {
    pthread_create(&workers[i], nullptr, workerRoutine, this);
  }

  /* return sock; */
}

void HttpDaemon::masterRoutine() {
  int clientSocket = provider->accept();
  if (clientSocket < 0) {
    fprintf(logger, HTTP_ERROR_STR "accept failed: %s\n", strerror(errno));
  }

  pthread_mutex_lock(&reqMutex);
  pendingRequests.push_back(clientSocket);
  pthread_cond_signal(&reqCond);
  pthread_mutex_unlock(&reqMutex);
}

void HttpDaemon::start() {
  initialize();

  for (;;) {
    this->masterRoutine();
  }
}

void HttpDaemon::run(size_t numRequests) {
  initialize();

  for (size_t i = 1; i <= numRequests; i++) {
    masterRoutine();
  }

  /* join all threads. */
  for (int i = 0; i < numWorkers; i++) {
    pthread_mutex_lock(&reqMutex);
    pendingRequests.push_back(-1);
    pthread_cond_signal(&reqCond);
    pthread_mutex_unlock(&reqMutex);
  }
  for (int i = 0; i < numWorkers; i++) {
    pthread_join(workers[i], nullptr);
  }
}

/**
 * HTTP/1.1 400 Bad Request
 * Content-Length: 0
 * Connection: close
 */
static const char onBadRequest[] = {
    0x48, 0x54, 0x54, 0x50, 0x2f, 0x31, 0x2e, 0x31, 0x20, 0x34, 0x30,
    0x30, 0x20, 0x42, 0x61, 0x64, 0x20, 0x52, 0x65, 0x71, 0x75, 0x65,
    0x73, 0x74, 0x0d, 0x0a, 0x43, 0x6f, 0x6e, 0x74, 0x65, 0x6e, 0x74,
    0x2d, 0x4c, 0x65, 0x6e, 0x67, 0x74, 0x68, 0x3a, 0x20, 0x30, 0x0d,
    0x0a, 0x43, 0x6f, 0x6e, 0x65, 0x63, 0x74, 0x69, 0x6f, 0x6e, 0x3a,
    0x20, 0x63, 0x6c, 0x6f, 0x73, 0x65, 0x0d, 0x0a, 0x0d, 0x0a};

/* Enhancement: handle request, but reuse the connection. */
void HttpDaemon::handleRequest(HttpHeadReader &reader, Socket &socket,
                               HttpHandler *handler, FILE *logger) {
  std::vector<char> path;
  HttpDaemon::Method method;

  void *buffer = reader.getBuffer();
  const size_t bufferSize = reader.getSize();

  HttpHeadParser<HttpHeadReader>::table_t table;
  table["Content-Length"] = std::vector<char>();
  table["Connection"] = std::vector<char>();

  for (;;) {
    table["Content-Length"].clear();
    table["Connection"].clear();
    reader.reset(socket);

    HttpHeadParser<HttpHeadReader> parser(&reader, table);
    parser.parse();
    if (!parser.isValid() || reader.getLastError()) {
      (void)socket.write(onBadRequest, sizeof(onBadRequest));
      fprintf(logger, HTTP_LOG_STR " ?? ?? 400\n");
      break;
    }

    parser.method.push_back(0);
    const char *methodStr = parser.method.data();
    method = methodFromString(methodStr);
    if (method == Method::INVALID) {
      (void)socket.write(onBadRequest, sizeof(onBadRequest));
      fprintf(logger, HTTP_LOG_STR " ?? ?? 400\n");
      break;
    }

    path = std::move(parser.path);
    path.push_back(0);

    std::vector<char> &connString = table["Connection"];
    connString.push_back(0);
    HttpDaemon::Connection connection =
        HttpDaemon::connFromString(connString.data());

    size_t contentLength = 0;
    std::vector<char> &contentLengthStr = table["Content-Length"];
    if (!contentLengthStr.empty()) {
      contentLengthStr.push_back(0);
      contentLength = atol(contentLengthStr.data());
    }

    fprintf(logger, HTTP_LOG_STR " %s %s ", methodStr,
            (const char *)path.data());
    handler->setRequestMethod(method);
    handler->setRequestPath(path);
    handler->setConnection(connection);
    handler->doConsume(&socket, buffer, bufferSize, contentLength);
    handler->doProduce(&socket, buffer, bufferSize);
    int statusCode = handler->getStatusCode();
    fprintf(logger, "%d\n", statusCode);
    if (handler->connectionClosed()) {
      break;
    }
  }

  socket.close();
}

static HttpDaemon::Method methodFromString(const char *s) {
  static const char *strs[] = {nullptr,
#define TOSTR(method) #method
#define X(method) TOSTR(method),
                               HTTP_METHODS_FOREACH(X)
#undef X
#undef TOSTR
  };

  static HttpDaemon::Method methods[] = {HttpDaemon::Method::INVALID,
#define X(method) HttpDaemon::Method::method,
                                         HTTP_METHODS_FOREACH(X)
#undef X
  };

  for (size_t i = 1; i < (sizeof(strs) / sizeof(strs[0])); i++) {
    if (strcmp(s, strs[i]) == 0) {
      return methods[i];
    }
  }
  return HttpDaemon::Method::INVALID;
}

void *HttpDaemon::workerRoutine(void *arg) {
  /* `arg` always equals to this. */
  HttpDaemon *httpd = reinterpret_cast<HttpDaemon *>(arg);
  pthread_mutex_t *mutexPtr = &(httpd->reqMutex);
  handler_t requestHandler = httpd->requestHandler;

  /* Allocate a buffered reader. */
  int tid = -1;
  do {
    const auto self = pthread_self();
    for (int i = 0; i < HttpDaemon::numWorkers; i++) {
      if (pthread_equal(self, httpd->workers[i]) != 0) {
        tid = i;
        break;
      }
    }
  } while (0);
  http_assert(tid >= 0);
  HttpHeadReader &reader = httpd->readers[tid];
  Socket &clientSocketWrapper = httpd->sockAllocator(tid);

  /* Set timeouts to prevent hanging on slow or dead connections */
  struct timeval timeout;
  timeout.tv_sec = 30; /* FIXME: make this configurable */
  timeout.tv_usec = 0;

  for (;;) {
    /* Get an accepted socket from queue. */
    pthread_mutex_lock(mutexPtr);
    while (httpd->pendingRequests.empty()) {
      pthread_cond_wait(&(httpd->reqCond), mutexPtr);
    }
    int clientSock = httpd->pendingRequests.front();
    httpd->pendingRequests.pop_front();
    pthread_mutex_unlock(mutexPtr);
    if (clientSock < 0) {
      pthread_exit(nullptr);
    }

    if (clientSocketWrapper.setConnectFd(clientSock, httpd->provider) != 0) {
      /* error */
      clientSocketWrapper.close();
      continue;
    }

    /* Do rest initialization of socket. */
    reader.reset(clientSocketWrapper);
    setsockopt(clientSock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout,
               sizeof(timeout));
    setsockopt(clientSock, SOL_SOCKET, SO_SNDTIMEO, (const char *)&timeout,
               sizeof(timeout));

    auto *handler = requestHandler(tid);
    HttpDaemon::handleRequest(reader, clientSocketWrapper, handler,
                              httpd->logger);
    delete handler;

    /* on Finish. */
    fflush(httpd->logger);
    // close(clientSock);
  }
  return httpd;
}

int InetSocketProvider::createListeningSocket(int port) {
  int sockFd = ::socket(AF_INET, SOCK_STREAM, 0);

  if (sockFd < 0) {
    perror("socket");
    return sockFd;
  }

  int opt = 1;
  if (setsockopt(sockFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
    perror("setsockopt");
    ::close(sockFd);
    return -1;
  }

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  port = port & 0xffff;
  addr.sin_port = htons((uint16_t)port);
  if (bind(sockFd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("bind");
    ::close(sockFd);
    return -1;
  }

  if (listen(sockFd, 16) < 0) {
    perror("listen");
    ::close(sockFd);
    return -1;
  }
  return sockFd;
}

namespace HttpUtil {

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

int render404(SocketStream &socket) {
  /* head */
  socket << "HTTP/1.1 404 Not Found\r\n"
            "Connection: close\r\n"
            "Content-Length: 18\r\n"
            "Content-Type: text/html\r\n\r\n" /* body */
            "<h1>Not Found</h1>";
  return 404;
}

int render500(SocketStream &socket, int err) {
  const char *body = "<h1>Internal Server Error</h1>";
  const char *message = err ? strerror(err) : "";

  /* head */
  socket << "HTTP/1.1 500 Internal Server Error\r\n"
            "Connection: close\r\n"
            "Content-Length: ";
  socket << strlen(body) + strlen(message) + strlen("<h2></h2>");
  socket << "\r\n"
            "Content-Type: text/html\r\n\r\n";

  /* body */
  socket << body << "<h2>" << message << "</h2>";

  return 500;
}

int renderFile(SocketStream &socket, const char *path) {
  if (*path == 0) {
    /* bad */
    return render500(socket, EINVAL);
  }

  /* skip leading / */
  int fd = open(path + 1, O_RDONLY);
  if (fd < 0) {
    int err = errno;
    if (err == ENOENT) {
      return render404(socket);
    } else {
      return render500(socket, err);
    }
  }

  size_t contentLength = 0;
  do {
    struct stat st;
    if (fstat(fd, &st) < 0) {
      render500(socket, errno);
      close(fd);
      return 500;
    }
    if ((st.st_mode & S_IFMT) == S_IFDIR) {
      render500(socket, EISDIR);
      close(fd);
      return 500;
    }
    contentLength = st.st_size;
  } while (0);

  socket << "HTTP/1.1 200 OK\r\n"
            "Connection: close\r\n"
            "Content-Length: "
         << contentLength << "\r\n\r\n";
  socket.forward(fd);
  close(fd);
  return 200;
}

void renderDirectory(SocketStream &socket, const char *path);

} /* namespace HttpUtil */

std::string checkSocket(void) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    return std::string("socket:") + std::string(strerror(errno));
  }
  if (listen(fd, 1) < 0) {
    int err = errno;
    close(fd);
    return std::string(strerror(err));
  }
  close(fd);
  return "OK";
}

SocketStream &operator<<(SocketStream &stream, const std::vector<char> &vec) {
  stream.write(vec.data(), vec.size());
  return stream;
}

SocketStream &operator<<(SocketStream &stream, const char *str) {
  stream.write(str, strlen(str));
  return stream;
}

SocketStream &operator<<(SocketStream &stream, size_t length) {
  char buf[32];
  int len = snprintf(buf, sizeof(buf), "%zu", length);
  stream.write(buf, (size_t)len);
  return stream;
}

HttpClientProvider::~HttpClientProvider() { ::freeaddrinfo(info); }

HttpClientProvider::HttpClientProvider(const char *host, int port)
    : ClientProvider() {
  struct addrinfo hints;
  char buf[24];
  sprintf(buf, "%d", port);

  ::memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;     /* Allow IPv4 or IPv6 */
  hints.ai_socktype = SOCK_STREAM; /* Stream socket */
  hints.ai_flags = 0;
  hints.ai_protocol = 0; /* Any protocol */
  this->info = nullptr;
  int ret = getaddrinfo(host, buf, &hints, &this->info);
  if (ret != 0) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(ret));
    _exit(1);
  }
}

int HttpClientProvider::connect(void) {
  for (struct addrinfo *it = this->info; it != nullptr; it = it->ai_next) {
    int fd = ::socket(it->ai_family, it->ai_socktype, it->ai_protocol);
    if (fd < 0) {
      continue;
    }
    if (::connect(fd, it->ai_addr, it->ai_addrlen) == 0) {
      return fd;
    }
  }

  return -1 /* failure */;
}

/* returns 0 on success. */
int HttpClient::request(HttpMethodType method, std::vector<char> path,
                        bool isLast) {
  void *buffer = reader.getBuffer();
  const auto size = reader.getSize();
  SocketStream stream(&socket, buffer, size);
  HttpHeadParser<HttpHeadReader>::table_t table;
  table["Content-Length"] = std::vector<char>();
  table["Connection"] = std::vector<char>();

  HttpConnectionType con =
      isLast ? HttpConnectionType::CLOSE : HttpConnectionType::KEEP_ALIVE;
  handler.setRequestMethod(method);
  handler.setRequestPath(path);
  handler.setConnection(con);

  if (!connected) {
    /* re-create a connection. */
    int fd = provider.connect();
    if (fd < 0) {
      return 1;
    }
    if (socket.setConnectFd(fd, &provider) != 0) {
      return 1;
    }
    connected = true;
  }

#define doClose                                                                \
  do {                                                                         \
    socket.close();                                                            \
    connected = false;                                                         \
  } while (0)

  /* Send the entire request.  */
  handler.produce(stream);

  /* Parse the response header */
  reader.reset(socket);
  HttpHeadParser<HttpHeadReader> parser(&reader, table);
  parser.parse();
  if (!parser.isValid()) {
    fprintf(stderr, HTTP_ERROR_STR " failed to parser header: %s\n", 
            strerror(reader.getError()));
    doClose;
    return 1;
  }
  /* method -> http version, path -> statusCode */
  parser.path.push_back(0);
  int statusCode = atoi(parser.path.data());

  /* Consume the response (if status OK). */
  bool success = (statusCode == 200);
  auto &lenStr = table["Content-Length"];
  size_t contentLength = 0;
  if (!lenStr.empty()) {
    lenStr.push_back(0);
    contentLength = atol(lenStr.data());
  }
  if (success && contentLength) {
    size_t nr;
    size_t offset = reader.getOffset();
    nr = reader.getEnd();
    http_assert(nr >= offset);
    nr -= offset;
    nr = std::min(nr, contentLength);
    /* handles bytes in the buffer. */
    if (handler.consume(buffer + offset, nr)) {
      for (; nr < contentLength;) {
        auto res = socket.read(buffer, std::min(size, contentLength - nr));
        if (res <= 0) {
          break;
        }
        size_t inc = (size_t)res;
        if (!handler.consume(buffer, inc)) {
          break;
        }
        nr += inc;
      }
    }

    success = (nr == contentLength);
  }

  auto &connectionStr = table["Connection"];
  if (!connectionStr.empty()) {
    connectionStr.push_back(0);
    con = HttpDaemon::connFromString(connectionStr.data());
  } else { /* con remains unchanged. */
  }

  if (con == HttpConnectionType::CLOSE || !success) {
    doClose;
  } else {
    /* reuse the connection. */
  }

  return success ? 0 : 1;
#undef doClose
}
