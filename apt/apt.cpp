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

/**
 * See <strong>sources.list</strong>(5) for the apt source list format.
 *
 * <p>We only implemented parsing for its "one-line style" format.</p>
 */
struct AptListFile {
  std::unordered_map<std::string, std::vector<PackageDetail>> table;

  std::string uri;
  std::string suite;
  std::string component;

  AptListFile &operator=(AptListFile &&other) = default;
  AptListFile(AptListFile &&other) = default;
  APT_DISALLOW_COPY(AptListFile)

  ~AptListFile() = default;
  AptListFile() = default;
  AptListFile(const std::string &u, const std::string s, const std::string &c)
      : uri(u), suite(s), component(c) {}
  static void fromLine(std::vector<AptListFile> &files,
                       const std::string &line);

  bool match(const char *file) const;
  void parse(const char *dirname, const char *file) {
    std::string absPath = std::string(dirname) + "/" + file;
    std::ifstream ifile(absPath.c_str());
    AptParse(ifile, table);
    ifile.close();
  }
};

bool AptListFile::match(const char *file) const {
  size_t i = 0;
  size_t j;
  int tokens = 0;

  /* FIXME: also compare ^0, ^1 against uri. */

  /* security.ubuntu.com_ubuntu_dists_jammy-security_main_bin... */
  /* ^0                  ^1     ^2    ^3             ^4*/
  /**
   * Split file by '_',
   * and check token[3] == suite && token[4] == component
   */
  while (file[i]) {
    j = i;
    while (file[j] && file[j] != '_') {
      j++;
    }
#define thisToken std::string(file + i, file + j)
    if (tokens == 3) {
      if (thisToken != suite) {
        return false;
      }
    } else if (tokens == 4) {
      if (thisToken != component) {
        return false;
      }
    }
    tokens++;
    if (file[j] == 0 || tokens > 4) {
      break;
    }
    i = j + 1;
#undef thisToken
  }

  return true;
}

/**
 * Initialize this object by a line in
 * <strong>sources.list</strong>(5).
 *
 * @param line
 */
void AptListFile::fromLine(std::vector<AptListFile> &files,
                           const std::string &line) {
  const char *ptr = line.c_str();
  if (strncmp(ptr, "deb", 3) != 0) {
    return;
  }
  if (strncmp(ptr, "deb-src", 7) == 0) {
    /* ignored */
    return;
  }

  /*
   * Example of line:
   * deb http://archive.ubuntu.com/ubuntu/ jammy-backports main
   *     ^uri                              ^suite          ^component
   */

  auto appendNextToken = [](const char *str,
                            std::string &dest) -> const char * {
    char ch = *str;
    while (ch && isspace(ch)) {
      str++;
      ch = *str;
    }
    while (ch && !isspace(ch)) {
      dest.push_back(ch), str++;
      ch = *str;
    }
    return str;
  };

  ptr += 4; /* skip "deb " */
  std::string uri, suite, component;
  ptr = appendNextToken(ptr, uri);
  if (uri.front() == '[') {
    /* Possibly skip */
    while (uri.back() != ']') {
      ptr = appendNextToken(ptr, uri);
      if (uri.empty()) {
        break;
      }
    }
    uri.clear();
    ptr = appendNextToken(ptr, uri);
  }
  ptr = appendNextToken(ptr, suite);
  ptr = appendNextToken(ptr, component);

  if (uri.empty() || suite.empty() || component.empty()) {
    fprintf(stderr, "ERROR parsing: \"%s\"\n", line.c_str());
    return;
  }
  files.push_back(AptListFile(uri, suite, component));
  for (;;) {
    component.clear();
    ptr = appendNextToken(ptr, component);
    if (component.empty()) {
      break;
    }
    files.push_back(AptListFile(uri, suite, component));
  }
}

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

  typedef std::function<int(const AptListFile &,
                            const std::vector<PackageDetail> &)>
      callbackType;

public:
  Apt(const char *directory);
  ~Apt() = default;

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
      return callback(file, iter->second);
    }
  }

  dbg.log("Package %s be found.\n", package.c_str());
  return ENOENT;
}

Apt::Apt(const char *dir) {
  files.reserve(4 * 3);

  do {
    const char *file = getenv("APT_SOURCES_LIST");
    if (file == nullptr) {
      file = "/etc/apt/sources.list";
    }

    auto doParse = [](std::ifstream &sources, std::vector<AptListFile> &files) {
      std::string line;
      while (std::getline(sources, line)) {
        if (line.empty()) {
          continue;
        }
        AptListFile::fromLine(files, line);
      }
      sources.close();
    };
    std::ifstream sources(file);
    dbg.log("parsing sources.list ...\n");
    doParse(sources, this->files);

    /* Parse files in APT_SOURCES_LIST_D (default to /etc/apt/sources.list.d) */
    const char *dir = getenv("APT_SOURCES_LIST_D");
    if (!dir) {
      dir = "/etc/apt/sources.list.d";
    }
    DIR *dirp = opendir(dir);
    if (!dirp) {
      /* no such dir */
      break;
    }
    struct dirent *entry;
    while ((entry = readdir(dirp)) != NULL) {
      const char *fname = entry->d_name;
      /* check whether fname ends with .list: */
      size_t len = strlen(fname);
      if (len < 5 || strcmp(fname + len - 5, ".list")) {
        continue;
      }

      std::string filepath = std::string(dir) + "/" + fname;
      std::ifstream ifile(filepath.c_str());
      dbg.log("parsing sources.list.d/%s ...\n", fname);
      doParse(ifile, this->files);
    }
    closedir(dirp);
  } while (0);

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
      dbg.log("Failed to match for file %s\n", fname);
      continue;
    } else {
      aptFile->parse(dir, fname);
    }
  }

  closedir(dirp);
}

static Apt *apt;

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

static int doDownload(const std::string &package, bool isLast) {
  int ret = 0;

  APT_ASSERT(apt);

  const std::string &packageStr = package;
  auto downloadCallback =
      [&packageStr, isLast](const AptListFile &file,
                            const std::vector<PackageDetail> &details) -> int {
    const auto &detail = details[0];
    std::string ofile = packageStr + ".deb";

    int ret;
    do {
      /* user may have downloaded the deb. Check it. */
      struct stat st;
      int fd = open(ofile.c_str(), O_RDONLY);
      if (fd >= 0 && fstat(fd, &st) == 0) {
        /* check md5sum and size. */
        if (fileMatch(ofile.c_str(), detail)) {
          /* skip download */
          dbg.log("Package %s already downloaded.\n", packageStr.c_str());
          close(fd);
          return 0;
        }
      }
      close(fd);
    } while (0);

    dbg.log("Downloading package %s\n", packageStr.c_str());
    if (fork() == 0) {
      std::string uri = file.uri;
      if (uri.back() != '/') {
        uri += "/";
      }
      uri += detail.filename_;
      /* both GNU wget and busybox wget supports -q. */
      execlp("wget", "wget", uri.c_str(), "-q", "-O", ofile.c_str(), nullptr);
      perror("execlp");
      _exit(12);
    } else {
      int wstatus;
      if (waitpid(-1, &wstatus, 0) == -1) {
        perror("wait:");
        return 1;
      }
      ret = WEXITSTATUS(wstatus);
      if (ret != 0) {
        fprintf(stderr, "Failed to download package %s\n", packageStr.c_str());
        return ret;
      }
    }

    dbg.log("Checking MD5 sum...\n");
    if (!fileMatch(ofile.c_str(), detail)) {
      fprintf(stderr, "MD5 sum mismatch! Delete file and stop.\n");
      (void)unlink(ofile.c_str());
      ret = 1;
    }

    return ret;
  };

  ret = apt->find(packageStr, downloadCallback);
  if (ret == ENOENT) {
    fprintf(stderr, "Package %s not found.\n", package.c_str());
  }
  return ret;
}

static int download(int argc, char **argv, char **envp) {
  int ret = 0;

  for (int i = 3; i < argc; i++) {
    const char *package = argv[i];
    ret |= doDownload(package, false);
  }
  if (argc >= 3) {
    const char *package = argv[2];
    ret |= doDownload(package, true);
  }

  return ret;
}

static bool packageExists(const std::string &package) {
  auto checkExistCallback = [](const AptListFile &file,
                               const std::vector<PackageDetail> &) -> int {
    return 0;
  };

  return apt->find(package, checkExistCallback) == 0;
}

static int downloadDeps(int argc, char **argv, char **envp) {
  std::function<bool(const std::string &)> exists = packageExists;

  int ret = 0;
  if (argc == 2) {
    fprintf(stderr, "No package specified.\n");
    return 1;
  }

  std::set<std::string> packagesRequired;
  auto addDepsCallback = [&packagesRequired,
                          &exists](const AptListFile &file,
                                   const std::vector<PackageDetail> &details) {
    const auto &detail = details[0];
    auto exprTree = detail.deps_;
    if (exprTree) {
      exprTree->satisfy(packagesRequired, exists);
    }
    exprTree = detail.pre_deps_;
    if (exprTree) {
      exprTree->satisfy(packagesRequired, exists);
    }
    return 0;
  };
  auto doSatisfy = [&](const std::string &package) {
    packagesRequired.insert(package);
    apt->find(package, addDepsCallback);
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
      ret |= doDownload((*it), false);
    }
  }
  {
    auto it = packagesRequired.begin();
    ret |= doDownload((*it), true);
  }
  return ret;
}

static int info(int argc, char **argv, char **envp) {

  int ret = 0;
  for (int i = 2; i < argc; i++) {
    const char *arg = argv[i];
    auto infoCallback =
        [arg](const AptListFile &file,
              const std::vector<PackageDetail> &details) -> int {
      const auto size = details.size();
      if (size > 1) {
        fprintf(stdout, "Package %s has %zu records:\n", arg, size);
      } else {
      }
      for (const auto &detail : details) {
        fprintf(stdout, "Package %s:\n", arg);
        detail.print(stdout);
        fprintf(stdout, "\n");
      }
      return 0;
    };

    int r = apt->find(arg, infoCallback);
    if (r == ENOENT) {
      fprintf(stderr, "Package %s not found.\n", arg);
    }
    ret |= r;
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

  auto ptr = ops.find(argv[1]);
  if (ptr == ops.end()) {
    help(argc, argv, envp);
    return 1;
  }

  apt = new Apt(AptListDir());
  int ret = ptr->second(argc, argv, envp);
  delete apt;
  apt = nullptr;
  return ret;
}
