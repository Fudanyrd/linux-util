#include "aptlist.h"

#include <cstdio>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <set>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <httpd.h>

#define APT_MAX_RETRIES 3

#define APT_DISALLOW_COPY(classname)                                           \
  classname &operator=(const classname &other) = delete;                       \
  classname(const classname &other) = delete;

struct AptListFile {
  std::unordered_map<std::string, std::vector<PackageDetail>> table;

  std::vector<char> url; /* archive.ubuntu.com */
  std::string base;      /* ubuntu */

  AptListFile &operator=(AptListFile &&other) = default;
  AptListFile(AptListFile &&other) = default;
  APT_DISALLOW_COPY(AptListFile)

  ~AptListFile() = default;
  AptListFile(const char *dirname, const char *file);

  bool match(const char *file) const;
  void parse(const char *dirname, const char *file) {
    std::string absPath = std::string(dirname) + "/" + file;
    std::ifstream ifile(absPath.c_str());
    AptParse(ifile, table);
    ifile.close();
  }
};

bool AptListFile::match(const char *file) const {
  const char *substr1 = strchr(file, '_');
  if (!substr1) {
    return false;
  }
  const char *substr2 = strchr(substr1 + 1, '_');
  if (!substr2) {
    return false;
  }

  std::string file_url(file, substr1);
  if (strncmp(url.data(), file, url.size())) {
    return false;
  }

  std::string file_base(substr1 + 1, substr2);
  file_base = "/" + file_base + "/";
  if (file_base != base) {
    return false;
  }

  return true;
}

AptListFile::AptListFile(const char *dirname, const char *file) {
  const char *substr1 = strchr(file, '_');
  APT_ASSERT(substr1 != nullptr);
  const char *substr2 = strchr(substr1 + 1, '_');
  APT_ASSERT(substr2 != nullptr);

  url = std::vector<char>(file, substr1);
  url.push_back(0);

  base = "/";
  base += std::string(substr1 + 1, substr2);
  base += "/";

  parse(dirname, file);
}

static HttpClient *client;

struct AptClientHandler : public HttpClientHandler {
  int ofd;

public:
  AptClientHandler(const std::vector<char> &host) : HttpClientHandler(host) {}
  AptClientHandler(const char *host) : HttpClientHandler(host) {}
  bool consume(const void *b, size_t length) override {
    if (ofd < 0) {
      http_assert(0 && "invalid file descriptor");
    }
    ssize_t ret = Socket::xwrite(ofd, (const char *)b, length);
    return ret == static_cast<ssize_t>(length);
  }
};

struct Apt {
private:
  AptListFile *searchByFileName(const char *name) {
    for (auto &file : files) {
      if (file.match(name)) {
        return &file;
      }
    }
    return nullptr;
  }

  std::vector<AptListFile> files;

  struct ClientStruct {
    AptClientHandler handler;
    HttpSocket socket;
    HttpClientProvider provider;
    HttpClient client;

    ClientStruct(const std::vector<char> &host)
        : handler(host.data()), socket(), provider(host.data(), 80),
          client(socket, provider, handler) {}
  };

  ClientStruct *clients;

  typedef std::function<int(const AptListFile &, HttpClient &)> callbackType;

public:
  Apt(const char *directory);
  ~Apt() {
    size_t len = files.size();
    for (size_t i = 0; i < len; i++) {
      clients[i].~ClientStruct();
    }
    ::free(clients);
  }

  /**
   * Find a package and perform action on it.
   * 
   * @param callback a return-0-on-success function.
   * @return 0 on success; ENOENT if no such package.
   */
  int find(const std::string &package, callbackType callback);

  APT_DISALLOW_COPY(Apt)
};

int Apt::find(const std::string &package, callbackType callback) {
  auto len = files.size();

  /* iterate over the files and find the package. */
  for (size_t i = 0; i < len; i++) {
    const auto &file = files[i];
    auto iter = file.table.find(package);
    if (iter == file.table.end()) {
      continue;
    } else {
      return callback(file, clients[i].client);
    }
  }

  dbg.log("Package %s be found.\n", package.c_str());
  return ENOENT;
}

Apt::Apt(const char *dir) {
  files.reserve(4);

  DIR *dirp = opendir(dir);
  struct dirent *entry;
  while ((entry = readdir(dirp)) != NULL) {
    const char *fname = entry->d_name;
    size_t len = strlen(fname);
    if (len < 8) {
      continue;
    }
    if (strcmp(fname + len - 8, "Packages") != 0) {
      continue;
    }
    auto *aptFile = searchByFileName(fname);
    dbg.log("Parsing list file %s ...\n", fname);
    if (aptFile == nullptr) {
      this->files.push_back(AptListFile(dir, fname));
    } else {
      aptFile->parse(dir, fname);
    }
  }
  closedir(dirp);

  size_t len = files.size();
  clients = (ClientStruct *)malloc(sizeof(ClientStruct) * len);
  if (clients == nullptr) {
    perror("malloc");
    _exit(124);
  }
  for (size_t i = 0; i < len; i++) {
    const auto &host = files[i].url;
    new (&clients[i]) ClientStruct(host);
  }
}

static AptClientHandler *handler;

static int download_main(int ofd, const char *pathStr, bool isLast) {
  dbg.log("Downloading %s\n", pathStr);
  std::vector<char> path(pathStr, pathStr + strlen(pathStr) + 1);
  int ret = 1;
  for (int i = 0; i < APT_MAX_RETRIES; i++) {
    ret = client->request(HttpMethodType::GET, path, isLast);
    if (ret == 0) {
      break;
    }
  }
  return ret;
}

static void dumpMD5sum(FILE *file, const unsigned char *md5sum) {
  for (int i = 0; i < 16; i++) {
    fprintf(file, "%02x", md5sum[i]);
  }
  fprintf(file, "\n");
}

static bool md5sumMatch(const char *file, const unsigned char *md5sum) {
  char cmd[128 * 2], buf[36];
  sprintf(cmd, "md5sum %s", file);
  FILE *pf = popen(cmd, "r");
  if (!pf) {
    perror("popen md5sum:");
    return false;
  }
  fscanf(pf, "%32s", buf);
  for (int i = 0; i < 16; i++) {
    char byte_str[3] = {buf[i * 2], buf[i * 2 + 1], 0};
    unsigned char byte =
        static_cast<unsigned char>(strtoul(byte_str, nullptr, 16));
    if (byte != md5sum[i]) {
      pclose(pf);
      fprintf(stderr, "Expected: ");
      dumpMD5sum(stderr, md5sum);
      fprintf(stderr, "GOT: %s\n", buf);

      return false;
    }
  }
  pclose(pf);
  return true;
}

static bool sizeMatch(const char *file, size_t size) {
  struct stat st;
  if (stat(file, &st) != 0) {
    perror("stat:");
    return false;
  }
  return st.st_size == static_cast<off_t>(size);
}

static bool fileMatch(const char *file, const PackageDetail &detail) {
  return sizeMatch(file, detail.size_) && md5sumMatch(file, detail.md5sum_);
}

using std::ifstream;
using std::string;
using std::vector;

static const char *AptListDir() {
  const char *env;
  if ((env = getenv("APT_LIST_DIR")) != nullptr) {
    return env;
  }
  return "/var/lib/apt/lists";
}

/**
 * FIXME: Full list:
 * for d in jammy-backports jammy-proposed jammy-security jammy-updates jammy;
 * for f in main multiverse restricted universe;
 */
static const char update_script[] = " \n\
set -e \n\
for d in jammy; do \n\
   for f in main multiverse restricted universe; do \n\
     wget http://archive.ubuntu.com/ubuntu/ubuntu/dists/$d/$f/binary-amd64/Packages.gz -O \"$d-$f.gz\"; \n\
     gzip -d \"$d-$f.gz\"; \n\
   done \n\
done";

static int update(int argc, char **argv, char **envp) {
  /* Why not use your own wget? (I'm lazy 😭) */
  /* The `wget` client class is not suitable - it links with -lssl
   * unnecessary for apt;
   */
  pid_t id = fork();

  static const char *update_args[] = {"/bin/busybox", "sh", "-c", update_script,
                                      0};

  if (id < 0) {
    perror("fork");
    return 1;
  }

  if (id == 0) {
    /* child */
    execve(update_args[0], (char *const *)update_args, envp);
    perror("apt child: execve:");
    _exit(1);
  }

  int wstatus;
  if (waitpid(-1, &wstatus, 0) == -1) {
    perror("wait:");
    return 1;
  }

  return WEXITSTATUS(wstatus);
}

static bool endswith(const char *s1, const char *end) {
  size_t l = strlen(s1);
  size_t le = strlen(end);
  if (l < le) {
    return false;
  }
  return 0 == strcmp(s1 + (l - le), end);
}

static int debug(int argc, char **argv, char **envp) {
  const char *fname = argv[2];
  if (!fname) {
    printf("missing input file\n");
    return 1;
  }

  std::unordered_map<string, PackageDetail> table;
  ifstream ifile(fname);
  dbg.log("Parsing apt list file: %s\n", fname);
  AptParse(ifile, table);
  ifile.close();

  for (const auto &p : table) {
    printf("Package: %s\n", p.first.c_str());
    p.second.print(stdout);
  }

  /* Ok. */
  return 0;
}

static int init_list(std::unordered_map<string, vector<PackageDetail>> &table) {
  const char *list_dir = AptListDir();
  DIR *dir = opendir(list_dir);

  char buf[384];
  strcpy(buf, list_dir);
  char *append = &buf[strlen(list_dir)];
  *append = '/';
  append++;
  if (!dir) {
    perror("opendir");
    return 1;
  }
  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    const char *fname = entry->d_name;
    size_t len = strlen(fname);
    if (len < 8) {
      continue;
    }
    if (strcmp(fname + len - 8, "Packages") != 0) {
      continue;
    }

    strcpy(append, fname);
    ifstream ifile((const char *)buf);
    dbg.log("Parsing apt list file: %s\n", fname);
    AptParse(ifile, table);
    ifile.close();
  }
  closedir(dir);
  return 0;
}

static int
doDownload(const char *package,
           const std::unordered_map<string, vector<PackageDetail>> &table,
           bool isLast) {
  int ret = 0;

  const char *args[3];
  args[0] = "apt-download";
  args[1] = args[2] = nullptr;

  APT_ASSERT(package);
  auto ptr = table.find((const char *)package);
  if (ptr == table.end()) {
    fprintf(stderr, "Package: %s Not found\n\n", package);
    ret = 1;
    return ret;
  }

  const auto &detail = ptr->second[0];
  std::string path = "/ubuntu/" + detail.filename_;
  std::string ofile = package + std::string(".deb");
  do {
    /* user may have downloaded the deb. Check it. */
    struct stat st;
    int fd = open(ofile.c_str(), O_RDONLY);
    if (fd >= 0 && fstat(fd, &st) == 0) {
      /* check md5sum and size. */
      if (fileMatch(ofile.c_str(), detail)) {
        /* skip download */
        fprintf(stderr, "Package %s already downloaded.\n", package);
        close(fd);
        return 0;
      }
    }
    close(fd);
  } while (0);

  int ofd = open(ofile.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0666);
  if (ofd < 0) {
    perror("open output file");
    ret = 1;
    return ret;
  }
  handler->ofd = ofd;
  if ((ret = download_main(ofd, (const char *)path.c_str(), isLast)) != 0) {
    fprintf(stderr, "Package %s failed download. Stop.\n", package);
    close(ofd);
    (void)unlink(ofile.c_str());
    return ret;
  }

  (void)fsync(ofd);
  close(ofd);
  handler->ofd = -1;
  fprintf(stderr, "Checking MD5 sum...\n");
  if (!fileMatch(ofile.c_str(), detail)) {
    fprintf(stderr, "MD5 sum mismatch! Delete file and stop.\n");
    (void)unlink(ofile.c_str());
    ret = 1;
  }

  return ret;
}

static int download(int argc, char **argv, char **envp) {
  std::unordered_map<string, vector<PackageDetail>> table;
  int ret;
  if ((ret = init_list(table)) != 0) {
    return ret;
  }

  for (int i = 3; i < argc; i++) {
    const char *package = argv[i];
    ret |= doDownload(package, table, false);
  }
  if (argc >= 3) {
    const char *package = argv[2];
    ret |= doDownload(package, table, true);
  }

  return ret;
}

static int downloadDeps(int argc, char **argv, char **envp) {

  std::unordered_map<string, vector<PackageDetail>> table;
  std::function<bool(const std::string &)> exists =
      [&](const std::string &package) {
        return table.find(package) != table.end();
      };
  int ret = 0;
  if ((ret = init_list(table)) != 0) {
    return ret;
  }

  if (argc == 2) {
    fprintf(stderr, "No package specified.\n");
    return 1;
  }

  std::set<std::string> packagesRequired;
  auto doSatisfy = [&](const std::string &package) {
    packagesRequired.insert(package);
    auto iter = table.find(package);
    if (iter == table.end()) {
      fprintf(stderr, "Package: %s Not found\n\n", package.c_str());
      return;
    }
    PackageDetail &detail = iter->second[0];
    auto exprTree = detail.deps_;
    if (exprTree) {
      exprTree->satisfy(packagesRequired, exists);
    }
    exprTree = detail.pre_deps_;
    if (exprTree) {
      exprTree->satisfy(packagesRequired, exists);
    }
  };

  for (int i = 2; i < argc; i++) {
    APT_ASSERT(argv[i]);
    std::string package = argv[i];
    doSatisfy(package);
  }

  size_t numPackages = packagesRequired.size();
  for (;;) {
    size_t oldValue = numPackages;
    for (const auto &package : packagesRequired) {
      doSatisfy(package);
    }
    numPackages = packagesRequired.size();
    if (oldValue == numPackages) {
      break;
    }
  }

  {
    auto it = packagesRequired.begin();
    for (it++; it != packagesRequired.end(); it++) {
      const auto &package = *it;
      ret |= doDownload(package.c_str(), table, false);
    }
  }
  {
    auto it = packagesRequired.begin();
    ret |= doDownload((*it).c_str(), table, true);
  }
  return ret;
}

static int info(int argc, char **argv, char **envp) {
  std::unordered_map<string, vector<PackageDetail>> table;
  if (init_list(table)) {
    return 1;
  }

  int ret = 0;

  for (int i = 2; i < argc; i++) {
    const char *package = argv[i];
    APT_ASSERT(package);
    auto ptr = table.find((const char *)package);
    if (ptr == table.end()) {
      printf("Package: %s Not found\n\n", package);
      ret = 1;
    } else {
      const auto &records = ptr->second;
      APT_ASSERT(records.size());

      if (records.size() > 1) {
        printf("Package: %s (%ld records)\n", package, records.size());
      } else {
        printf("Package: %s\n", package);
      }
      for (const auto &record : records) {
        record.print(stdout);
      }
      printf("\n");
    }
  }

  return ret;
}

static int help(int argc, char **argv, char **envp) {
  printf("Supported operations:\n"
         "  info: list information of a package.\n"
         "  debug: reserved\n"
         "  download: Download the binary package into the current directory\n"
         "  download-dep: Download a package and its dependencies\n"
         "  update: update list of available packages\n"
         "  help: print this help message and exit.\n");
  return 0;
}

static std::unordered_map<std::string, int (*)(int, char **, char **)> ops = {
    {"info", info},   {"download", download}, {"download-dep", downloadDeps},
    {"debug", debug}, {"update", update},     {"help", help},
};

int main(int argc, char **argv, char **envp) {
  static short empty_str;
  if (endswith(argv[0], "debug")) {
    dbg.on();
  }
  if (!argv[1]) {
    argv[1] = (char *)&empty_str;
  }

  Apt *apt = new Apt(AptListDir());
  auto infoCallback = [](const AptListFile &file, HttpClient &) -> int {
    auto iter = file.table.find("firefox");
    assert (iter != file.table.end());
    const auto &info = iter->second[0];
    info.print(stdout);
    return 0;
  };
  // apt->find("firefox", infoCallback);
  auto downloadCallback = [](const AptListFile &file, HttpClient &client) -> int {
    auto iter = file.table.find("firefox");
    assert (iter != file.table.end());

    const auto &info = iter->second[0];
    info.print(stdout);
    std::string pathStr = file.base + info.filename_;
    std::vector<char> path(pathStr.begin(), pathStr.end());
    path.push_back(0);
    auto *handler = dynamic_cast<AptClientHandler *>(client.getHandler());
    handler->ofd = ::open("a.deb", O_CREAT | O_WRONLY | O_TRUNC, 0666);
    int ret = client.request(HttpMethodType::GET, path, true);
    ::close(handler->ofd);
    handler->ofd = -1;
    if (!fileMatch("a.deb", info)) {
      fprintf(stderr, "broken a.deb\n");
      return 1;
    }
    return ret;
  };
  int ret = apt->find("firefox", downloadCallback);
  delete apt;
  return ret;
}
