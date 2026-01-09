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

#define EMBEDDED
#include "download.cpp"

static int
doDownload(const char *package,
           const std::unordered_map<string, vector<PackageDetail>> &table) {
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
  args[1] = path.c_str();
  if ((ret = download_main(2, ofd, (char **)args)) != 0) {
    fprintf(stderr, "Package %s failed download. Stop.\n", package);
    close(ofd);
    (void)unlink(ofile.c_str());
    return ret;
  }

  (void)fsync(ofd);
  close(ofd);
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

  for (int i = 2; i < argc; i++) {
    const char *package = argv[i];
    ret |= doDownload(package, table);
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

  for (const auto &package : packagesRequired) {
    ret |= doDownload(package.c_str(), table);
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
  auto ptr = ops.find(argv[1]);
  if (ptr == ops.end()) {
    help(argc, argv, envp);
    return 1;
  }

  return ptr->second(argc, argv, envp);
}
