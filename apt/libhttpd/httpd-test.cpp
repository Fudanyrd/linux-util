#include "httpd.h"
#include "https-ext.h"

#include <cstdlib>
#include <ctime>

struct StringSocket : public Socket {
  StringSocket(const std::string &s) : data(s), offset(0), closed(false) {}
  virtual ssize_t read(void *buf, size_t len) override {
    if (offset >= data.size()) {
      return 0; // EOF
    }
    size_t toRead = std::min(len, data.size() - offset);
    memcpy(buf, data.data() + offset, toRead);
    offset += toRead;
    return toRead;
  }
  virtual ssize_t write(const void *buf, size_t len) override {
    writtenData.append((const char *)buf, len);
    return len;
  }
  int setConnectFd(int fd, SocketProvider *) override { return 0; }

  void close() override { closed = true; }

  std::string data;
  std::string writtenData;
  size_t offset;
  bool closed;
};

struct CountingClientHandler : public HttpClientHandler {
  size_t received{0};
  CountingClientHandler(const std::vector<char> &myHost)
      : HttpClientHandler(myHost), received(0) {}

  bool consume(const void *b, size_t length) override final {
    received += length;
    return true;
  }
};

struct FakeHttpHeadReader {
  const char **lines;
  int ptr;

public:
  FakeHttpHeadReader(const char **myLines) : lines(myLines), ptr(0) {}

  std::vector<char> nextLine(void) {
    if (lines[ptr] == nullptr) {
      return {};
    }
    const char *line = lines[ptr++];
    std::vector<char> ret(line, line + strlen(line));
    return ret;
  }
};

static int numSuccess = 0;
static int numFailure = 0;
static std::vector<const char *> failedTests;

extern "C" {
void httpRecordTestSuccess(const char *testname) { numSuccess++; }
void httpRecordTestFailure(const char *testname) {
  numFailure++;
  failedTests.push_back(testname);
}
}

#define FOREACH(X)                                                             \
  X(testInit)                                                                  \
  X(testHeadValid)                                                             \
  X(testHeadInvalid)                                                           \
  X(testHeadInvalid2)                                                          \
  X(testHeadParse)                                                             \
  X(testSocketStream)                                                          \
  X(testHttpd)                                                                 \
  X(testCreateSSLProvider)                                                     \
  X(testCreateClientProvider) X(testHttpClient) X(testHttpsClient)

#define DECL(testname) static void testname(void);
#define RUN(testname) HTTP_RUN_TEST(testname);

FOREACH(DECL)

int main() {

  /* Initialization */
  srand((unsigned)time(nullptr));

  FOREACH(RUN)

  fprintf(stderr, "Total %d, Success %d\n", numSuccess + numFailure,
          numSuccess);
  for (const char *testname : failedTests) {
    fprintf(stderr, "  Failed: %s\n", testname);
  }
  return numFailure == 0 ? 0 : 1;
}

static void testInit() { return; }

static bool VecEq(const std::vector<char> &vec, const char *s) {
  const char *buf = vec.data();
  if (!buf) {
    return *s == 0;
  }
  return 0 == strncmp(buf, s, vec.size());
}

static void testHeadValid(void) {
  StringSocket ss(
      "GET / HTTP/1.1\r\nHost: fudanyrd.com\r\nAccept Encoding: */*\r\n\r\n");
  HttpHeadReader reader;
  reader.reset(&ss);

  static const char *lines[] = {"GET / HTTP/1.1", "Host: fudanyrd.com",
                                "Accept Encoding: */*", ""};

  for (int i = 0; i < 4; i++) {
    auto line = reader.nextLine();
    if (!VecEq(line, lines[i])) {
      fprintf(stderr, "VecUneq at %d\n", i);
      abort();
    }
    assert(reader.getLastError() == 0);
  }
}

static void testHeadInvalid(void) {
  StringSocket ss(
      "GET / HTTP/1.1\r\nHost: fudanyrd.com\r\nAccept Encoding: */*\r\n");
  HttpHeadReader reader;
  reader.reset(&ss);

  static const char *lines[] = {"GET / HTTP/1.1", "Host: fudanyrd.com",
                                "Accept Encoding: */*"};

  for (int i = 0; i < 3; i++) {
    auto line = reader.nextLine();
    if (!VecEq(line, lines[i])) {
      fprintf(stderr, "VecUneq at %d\n", i);
      abort();
    }
  }

  auto line = reader.nextLine();
  assert(reader.getLastError() == EINVAL);
}

static void testHeadInvalid2(void) {
  StringSocket ss("GET / HTTP/1.1\r\nHost: fudanyrd.com\r\nAccept Encoding: "
                  "*/*\r\nIncomplete");
  HttpHeadReader reader;
  reader.reset(&ss);

  static const char *lines[] = {"GET / HTTP/1.1", "Host: fudanyrd.com",
                                "Accept Encoding: */*"};

  for (int i = 0; i < 3; i++) {
    auto line = reader.nextLine();
    if (!VecEq(line, lines[i])) {
      fprintf(stderr, "VecUneq at %d\n", i);
      abort();
    }
  }

  auto line = reader.nextLine();
  assert(line.empty());
  assert(reader.getLastError() == EINVAL);
}

static void testHeadParse() {

  const char *lines[] = {"GET /static/next.js HTTP/1.1",
                         "Host: fudanyrd.com",
                         "Accept: */*",
                         "Connection: close",
                         "Content-Length: 0",
                         nullptr};
  const char *keys[] = {
      "Content-Length",
      "Connection",
  };
  HttpHeadParser<FakeHttpHeadReader>::table_t table;
  for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
    table[keys[i]] = {};
  }

  HttpHeadParser<FakeHttpHeadReader> parser(new FakeHttpHeadReader(lines),
                                            table);

  parser.parse();
  assert(parser.isValid());
  assert(VecEq(parser.method, "GET"));
  assert(VecEq(parser.path, "/static/next.js"));
  assert(VecEq(table["Connection"], "close"));
  assert(VecEq(table["Content-Length"], "0"));

  delete parser.reader;
}

static void testSocketStream() {
  StringSocket socket("ok");
  char buf[32];
  SocketStream stream(&socket, buf, sizeof(buf));
  const size_t magic = 12345678ul;
  stream << "foo " << magic;
  stream.write("bar\n", 4);
  stream.flush();
  assert(socket.writtenData == "foo 12345678bar\n");
}

static HttpSocket httpSockets[HttpDaemon::numWorkers];
static Socket &httpSocketAllocator(int tid) {
  assert(tid < HttpDaemon::numWorkers);
  return httpSockets[tid];
}

/* This is a slow regression test. */
static void testHttpd(void) {
  struct HelloHandler : public HttpHandler {
  public:
    int getStatusCode() override { return 200; }
    bool consume(const void *buf, size_t length) override { return false; }
    void produce(SocketStream &stream) override {
      stream << "HTTP/1.1 200 OK\r\n"
                "Connection: close\r\n"
                "Content-Type: text/plain\r\n"
                "Content-Length: 12\r\n\r\n"
                "Hello world\n";
    }
  };

  int port = 10000 + (rand() % 50000);
  auto makeHandler = [](int tid) -> HttpHandler * {
    return new HelloHandler();
  };

  do {
    int fd = InetSocketProvider::createListeningSocket(port);
    assert(fd >= 0);
    close(fd);
  } while (0);

  do {
    InetSocketProvider provider(port);
    (HttpDaemon())
        .setRequestHandler(makeHandler)
        .setSocketAllocator(httpSocketAllocator)
        .setProvider(provider)
        .run(0);
  } while (0);

  InetSocketProvider *provider = new InetSocketProvider(port);
  if (fork() == 0) {
    delete provider;
    char buf[256];
    sprintf(buf, "http://localhost:%d/", port);
    execlp("curl", "curl", "-v", buf, nullptr);
    _exit(1);
  }

  do {
    (HttpDaemon())
        .setRequestHandler(makeHandler)
        .setSocketAllocator(httpSocketAllocator)
        .setProvider(*provider)
        .run(1);
    delete provider;
  } while (0);
}

void testCreateSSLProvider() {
  SSL_CTX *ctx = SSLSocketProvider::createSSLContext("cert.pem", "key.pem");
  assert(ctx != nullptr);
  SSL_CTX_free(ctx);

  ctx = SSLClientProvider::createClientContext();
  assert(ctx != nullptr);
  SSL_CTX_free(ctx);

  /* Checking wheter it is safef to free nullptr. */
  SSL_CTX_free(nullptr);
  SSL_free(nullptr);
}

static void testCreateClientProvider() {
  do {
    HttpClientProvider provider("archive.ubuntu.com");
    int connFd = provider.connect();
    assert(connFd >= 0);
    close(connFd);
  } while (0);

  do {
    SSLClientProvider provider("bing.com");
    int connFd = provider.connect();
    assert(connFd >= 0);
    close(connFd);
  } while (0);
}

static void testClientTmpl(HttpClient &client, CountingClientHandler &handler,
                           const char *hostStr, const char *cmd) {

  size_t expectedSize = 0;
  FILE *fp = popen(cmd, "r");
  assert(fp != nullptr);
  fscanf(fp, "%zu", &expectedSize);
  pclose(fp);

  const char *pathStr = "/";
  std::vector<char> path(pathStr, pathStr + 1 + strlen(pathStr));
  size_t fileSize;
  do {
    assert(client.request(HttpMethodType::GET, path, false) == 0);
    assert(!client.closed());
    fileSize = handler.received;
    assert(fileSize == expectedSize);
  } while (0);

  do {
    /* check that we are reusing previous connection. */
    assert(client.request(HttpMethodType::GET, path, true) == 0);
    assert(client.closed());
  } while (0);
  assert(handler.received == fileSize * 2);
}

static void testHttpClient() {
  HttpSocket socket;
  const char *hostStr = "archive.ubuntu.com";
  std::vector<char> host(hostStr, hostStr + strlen(hostStr) + 1);

  HttpClientProvider provider(hostStr);
  CountingClientHandler handler(host);
  HttpClient client(socket, provider, handler);

  testClientTmpl(client, handler, hostStr,
                 "curl -s http://archive.ubuntu.com/ | wc -c");
}

static void testHttpsClient() {
  SSLSocket socket;
  const char *hostStr = "jyywiki.cn";
  std::vector<char> host(hostStr, hostStr + strlen(hostStr) + 1);

  SSLClientProvider provider(hostStr);
  CountingClientHandler handler(host);
  HttpClient client(socket, provider, handler);

  testClientTmpl(client, handler, hostStr,
                 "curl -s https://jyywiki.cn/ | wc -c");
}
