#include "aptlist.h"

#include <fstream>

using std::ifstream;

static int info(int argc, char **argv) {
  /*
   * TODO: scan the directory, and parse all files whose
   * name ends with "Packages".
   */
  ifstream ifile(
      "/var/lib/apt/lists/"
      "archive.ubuntu.com_ubuntu_dists_jammy_main_binary-amd64_Packages");

  std::unordered_map<std::string, PackageDetail> table;
  AptParse(ifile, table);

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
