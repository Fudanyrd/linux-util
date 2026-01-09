
// strace -e openat apt info gdb
// Read
// "/var/lib/apt/lists/security.ubuntu.com_ubuntu_dists_jammy-security_universe_binary-amd64_Packages"

#include <cctype>
#include <stack>
#include <string.h>

#include "aptlist.h"

static inline bool startswith(const char *cstr, const std::string &line) {
  return 0 == strncmp(cstr, line.c_str(), strlen(cstr));
}

static ExprNode *ParseDep(const char *depstr) {
  /* Example: "foo (>= 0.1.0), libbar (>= 0.2.0), baz, libc6"
   * some packages may come without any version.
   * the output should be ["foo", "libbar", "baz", "libc6"]
   */

  /**
   * Scanner spec:
   * simple_token := .+
   * parenthese_token := \($.simple_token*\)
   * and_operator := ,
   * or_operator := |
   * operator_token := ($.and_operator|$.or_operator)
   * token := ($.simple_token | $.parenthese_token | $.operator_token)
   *
   * Parser spec; build an expression tree, contains `ValueNode` and
   * `OperatorNode` Operator precedence |(or) > ,(and) Associativity: left
   */
  std::vector<std::string> tokens;
  while (*depstr) {
    const char ch = *depstr;

    if (isblank(ch)) {
      /* Skip blanks. */
      depstr++;
    } else if (ch == ',') {
      /* Treat as an `and` operator. */
      tokens.emplace_back(",");
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
      tokens.emplace_back(std::string(start, depstr - start));
    }
  }

  auto precedence = [](const std::string &op) -> int {
    if (op == "|") {
      return 2;
    } else if (op == ",") {
      return 1;
    } else {
      dbg.log("Note: it is: %s\n", op.c_str());
      APT_ASSERT(false && "Unknown operator");
    }
  };

  std::stack<ExprNode *> values;
  std::stack<std::string> ops;

  auto pop_value = [&](void) -> ExprNode * {
    APT_ASSERT(!values.empty());
    auto *ret = values.top();
    values.pop();
    APT_ASSERT(ret);
    return ret;
  };

  const size_t nTokens = tokens.size();
  for (size_t i = 0; i < nTokens; i++) {
    const std::string &token = tokens[i];
    if (token == "," || token == "|") {
      const int pred = precedence(token);
      if (ops.empty() || precedence(ops.top()) < pred) {
        ops.push(token);
      } else {
        while (!ops.empty()) {
          if (precedence(ops.top()) < pred) {
            break;
          }
          ExprNode *right = pop_value();
          ExprNode *left = pop_value();
          OperatorNode *op = new OperatorNode(ops.top(), left, right);
          ops.pop();
          values.push(op);
        }
        ops.push(token);
      }
    } else {
      ValueNode *v = new ValueNode(token);
      if (!ops.empty()) {
        OperatorNode *op = new OperatorNode(ops.top(), pop_value(), v);
        ops.pop();
        values.push(op);
      } else
        values.push(v);
    }
  }

  if (values.empty()) {
    return nullptr;
  }
  auto *ret = values.top();
  return ret;
}

std::pair<std::string, PackageDetail> AptParse(std::ifstream &ifile) {
  std::string package;
  PackageDetail detail;
  detail.size_ = 0; /* Mark it as invalid. */

  ExprNode *dep = nullptr;
  ExprNode *predep = nullptr;

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
      dep = ParseDep(line.c_str() + 8);
    } else if (startswith("Filename:", line)) {
      detail.filename_ = line.substr(10, line.size() - 10);
    } else if (startswith("Pre-Depends:", line)) {
      /* FIXME: what is the difference(Pre-Depends:Depends) ? */
      predep = ParseDep(line.c_str() + 12);
    } else if (startswith("MD5sum:", line)) {
      const char *md5str = line.c_str() + 8;
      for (int i = 0; i < 16; i++) {
        char byte_str[3] = {md5str[i * 2], md5str[i * 2 + 1], 0};
        detail.md5sum_[i] =
            static_cast<unsigned char>(strtoul(byte_str, nullptr, 16));
      }
    }
  }

  detail.deps_ = std::shared_ptr<ExprNode>(dep);
  detail.pre_deps_ = std::shared_ptr<ExprNode>(predep);
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

void AptParse(
    std::ifstream &ifile,
    std::unordered_map<std::string, std::vector<PackageDetail>> &table) {
  for (;;) {
    auto ret = AptParse(ifile);
    if (ret.second.size_ == 0) {
      /* Empty record. */
      break;
    }

    table[ret.first].push_back(ret.second);
  }
}

void PackageDetail::print(FILE *ofile) const {
  fprintf(ofile, "Filename: %s\n", this->filename_.c_str());
  fprintf(ofile, "Url: http://archive.ubuntu.com/ubuntu/%s \n",
          this->filename_.c_str());
  fprintf(ofile, "Depends: ");
  if (this->deps_) {
    deps_->print(ofile);
  }
  fprintf(ofile, "\nPre-Depends: ");
  if (this->pre_deps_) {
    pre_deps_->print(ofile);
  }
  fprintf(ofile, "\n");
}
