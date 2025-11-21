#include "aptlist.h"

#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <string.h>
#include <unistd.h>

using std::ifstream;
using std::string;
using std::vector;

static bool endswith(const char *s1, const char *end) {
  size_t l = strlen(s1);
  size_t le = strlen(end);
  if (l < le) {
    return false;
  }
  return 0 == strcmp(s1 + (l - le), end);
}

static int debug(int argc, char **argv) {
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
  DIR *dir = opendir("/var/lib/apt/lists/");

  char buf[384];
  strcpy(buf, "/var/lib/apt/lists/");
  char *append = &buf[19 /* = strlen("/var/lib/apt/lists") */];
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

static int download(int argc, char **argv) {
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

static int info(int argc, char **argv) {
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

static int help(int argc, char **argv) {
  printf("Supported operations:\n"
         "  info: list information of a package.\n"
         "  debug: reserved\n"
         "  help: print this help message and exit.\n");
  return 0;
}

static std::unordered_map<std::string, int (*)(int, char **)> ops = {
    {"info", info},
    {"download", download},
    {"debug", debug},
    {"help", help},
};

int main(int argc, char **argv) {
  static short empty_str;
  if (endswith(argv[0], "debug")) {
    dbg.on();
  }
  if (!argv[1]) {
    argv[1] = (char *)&empty_str;
  }
  auto ptr = ops.find(argv[1]);
  if (ptr == ops.end()) {
    help(argc, argv);
    return 1;
  }

  return ptr->second(argc, argv);
}
