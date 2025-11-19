#include "aptlist.h"

#include <dirent.h>
#include <string.h>
#include <unistd.h>
#include <fstream>

using std::ifstream;

static bool endswith(const char *s1, const char *end) {
  size_t l = strlen(s1);
  size_t le = strlen(end);
  if (l < le) { return false; }
  return 0 == strcmp(s1 + (l - le), end);
}

static int info(int argc, char **argv) {
  std::unordered_map<std::string, PackageDetail> table;

  DIR *dir = opendir("/var/lib/apt/lists/");
  if (chdir("/var/lib/apt/lists/") != 0) {
    perror("chdir");
    return 1;
  }
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

    ifstream ifile(fname);
    dbg.log("Parsing apt list file: %s\n", fname);
    AptParse(ifile, table);
    ifile.close();
  }
  closedir(dir);

  int ret = 0;

  for (int i = 2; i < argc; i++) {
    const char *package = argv[i];
    APT_ASSERT(package);
    auto ptr = table.find((const char *)package);
    if (ptr == table.end()) {
      printf("Package: %s Not found\n\n", package);
      ret = 1;
    } else {
      printf("Package: %s\n", package);
      ptr->second.print(stdout);
      printf("\n");
    }
  }

  return ret;
}

static int help(int argc, char **argv) {
  printf("Supported operations:\n"
         "  info: list information of a package.\n"
         "  help: print this help message and exit.\n");
  return 0;
}

static std::unordered_map<std::string, int (*)(int, char **)> ops = {
    {"info", info},
    {"help", help},
};

int main(int argc, char **argv) {
  if (endswith(argv[0], "debug")) {
    dbg.on();
  }
  if (!argv[1]) {
    argv[1] = "";
  }
  auto ptr = ops.find(argv[1]);
  if (ptr == ops.end()) {
    help(argc, argv);
    return 1;
  }

  return ptr->second(argc, argv);
}
