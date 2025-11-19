
// strace -e openat apt info gdb
// Read
// "/var/lib/apt/lists/security.ubuntu.com_ubuntu_dists_jammy-security_universe_binary-amd64_Packages"

#include <cctype>
#include <string.h>

#include "aptlist.h"

static inline bool startswith(const char *cstr, const std::string &line) {
  return 0 == strncmp(cstr, line.c_str(), strlen(cstr));
}

static void ParseDep(const char *depstr, std::vector<std::string> &dst) {
  /* Example: "foo (>= 0.1.0), libbar (>= 0.2.0), baz, libc6"
   * some packages may come without any version.
   * the output should be ["foo", "libbar", "baz", "libc6"]
   */

  /* Scanner spec:
   * simple_token := .+
   * parenthese_token := \($.simple_token*\)
   * token := ($.simple_token | $.parenthese_token)
   *
   * Parser spec; only append $.simple_token
   */

  while (*depstr) {
    const char ch = *depstr;

    if (isblank(ch)) {
      /* Skip blanks. */
      depstr++;
    } else if (ch == ',') {
      /* Separator. */
      depstr++;
    } else if (ch == '(') {
      /* Parenthese token. Skip until ')'. */
      depstr++;
      while (*depstr && *depstr != ')') {
        depstr++;
      }
      if (*depstr == ')') {
        depstr++; /* Skip the closing parenthese. */
      }
    } else {
      /* Simple token. Read until blank or ',' */
      const char *start = depstr;
      while (*depstr && !isblank(*depstr) && *depstr != ',') {
        depstr++;
      }
      dst.emplace_back(std::string(start, depstr - start));
    }
  }
}

std::pair<std::string, PackageDetail> AptParse(std::ifstream &ifile) {
  std::string package;
  PackageDetail detail;
  detail.size_ = 0; /* Mark it as invalid. */

  bool parse = false;
  std::string line;
  while (std::getline(ifile, line)) {
    if (line.empty()) {
      if (parse) {
        /* Apt lists are separated by an empty line. */
        break;
      } else {
        /* Skip empty lines. */
        continue;
      }
    }

    parse = true; /* Parsing started. */
    if (startswith("Package:", line)) {
      package = line.substr(9, line.size() - 9);
    } else if (startswith("Size:", line)) {
      detail.size_ = atol(line.c_str() + 5);
    } else if (startswith("Depends:", line)) {
      /* This is something like "foo (>= 0.1.0), libbar (>= 0.2.0)"*/
      ParseDep(line.c_str() + 8, detail.deps_);
    } else if (startswith("Filename:", line)) {
      detail.filename_ = line.substr(10, line.size() - 10);
    }
  }

  return {package, detail};
}

void AptParse(std::ifstream &ifile,
              std::unordered_map<std::string, PackageDetail> &table) {
  for (;;) {
    auto ret = AptParse(ifile);
    if (ret.second.size_ == 0) {
      /* Empty record. */
      break;
    }

    table[ret.first] = ret.second;
  }
}

void PackageDetail::print(FILE *ofile) const {
  fprintf(ofile, "Filename: %s\n", this->filename_.c_str());
  fprintf(ofile, "Depends:");
  for (const auto &dep : this->deps_) {
    fprintf(ofile, " %s", dep.c_str());
  }
  fprintf(ofile, "\n");
}
