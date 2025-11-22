#include "aptlist.h"

#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

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

static int download(int argc, char **argv, char **envp) {
  std::unordered_map<string, vector<PackageDetail>> table;
  int ret;
  if ((ret = init_list(table)) != 0) {
    return ret;
  }

  const char *args[3];
  args[0] = "apt-download";
  args[1] = args[2] = nullptr;

  for (int i = 2; i < argc; i++) {
    const char *package = argv[i];
    APT_ASSERT(package);
    auto ptr = table.find((const char *)package);
    if (ptr == table.end()) {
      fprintf(stderr, "Package: %s Not found\n\n", package);
      ret = 1;
      continue;
    }

    const auto &detail = ptr->second[0];
    std::string path = "/ubuntu/" + detail.filename_;
    std::string ofile = package + std::string(".deb");
    int ofd = open(ofile.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0666);
    if (ofd < 0) {
      perror("open output file");
      ret = 1;
      continue;
    }
    args[1] = path.c_str();
    if ((ret = download_main(2, ofd, (char **)args)) != 0) {
      fprintf(stderr, "Package %s failed download. Stop.\n", package);
      close(ofd);
      (void)unlink(ofile.c_str());
      break;
    }

    close(ofd);
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
         "  update: update list of available packages\n"
         "  help: print this help message and exit.\n");
  return 0;
}

static std::unordered_map<std::string, int (*)(int, char **, char **)> ops = {
    {"info", info},
    {"download", download},
    {"debug", debug},
    {"update", update},
    {"help", help},
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
