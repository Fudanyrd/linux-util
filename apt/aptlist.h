#ifndef _APTLIST_H_
#define _APTLIST_H_ 1

#include <fstream>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "debug.h"

struct ExprNode {
public:
  virtual ~ExprNode() = default;

  virtual void print(FILE *file) const = 0;
  virtual ExprNode *children(void) = 0;

  /**
   * @param exists: apt has some `virtual` packages but they
   * cannot be found in lists.
   *
   * @return 0 if can be satisfied.
   */
  virtual int
  satisfy(std::set<std::string> &packages,
          std::function<bool(const std::string &)> exists) const = 0;
};

struct ValueNode : public ExprNode {
public:
  ValueNode(const std::string &package) : package_(package) {}

  void print(FILE *file) const { fprintf(file, "%s", package_.c_str()); }

  ExprNode *children() { /* no children*/
    return nullptr;
  }

  int satisfy(std::set<std::string> &packages,
              std::function<bool(const std::string &)> exists) const override {
    if (!exists(package_)) {
      return 1;
    }
    packages.insert(package_);
    return 0;
  }
  std::string package_;
};

struct OperatorNode : public ExprNode {
public:
  OperatorNode(const std::string &op, ExprNode *left, ExprNode *right)
      : op_(op[0]), left_(left), right_(right) {}

  ~OperatorNode() {
    delete left_;
    delete right_;
  }

  int satisfy(std::set<std::string> &packages,
              std::function<bool(const std::string &)> exists) const override {
    if (op_ == '|') {
      /* OR operator: satisfy either left or right. */
#define check_and_satisfy(node)                                                \
  do {                                                                         \
    std::set<std::string> tempSet;                                             \
    int ret = node->satisfy(tempSet, exists);                                  \
    if (ret == 0) {                                                            \
      packages.insert(tempSet.begin(), tempSet.end());                         \
      return 0;                                                                \
    }                                                                          \
  } while (0)

      check_and_satisfy(left_);
      check_and_satisfy(right_);
      return 1;
#undef check_and_satisfy

    } else if (op_ == ',') {
      /* AND operator: satisfy both left and right. */
      int ret = left_->satisfy(packages, exists);
      ret |= right_->satisfy(packages, exists);
      return ret;
    } else {
      APT_ASSERT(false && "Unknown operator");
    }
  }

  void print(FILE *file) const {
    left_->print(file);
    if (op_ == ',')
      fprintf(file, "%c ", op_);
    else
      fprintf(file, " %c ", op_);
    right_->print(file);
  }

  ExprNode *children() { return left_; }

  int precedence() {
    if (op_ == '|') {
      return 2;
    } else if (op_ == ',') {
      return 1;
    } else {
      APT_ASSERT(false && "Unknown operator");
    }
  }

  char op_;
  ExprNode *left_;
  ExprNode *right_;
};

struct PackageDetail {
  PackageDetail() = default;
  ~PackageDetail() = default;

  /* Dependencies. */
  unsigned char md5sum_[16]{0};
  std::shared_ptr<ExprNode> deps_{nullptr};
  std::shared_ptr<ExprNode> pre_deps_{nullptr};

  /* File name on the distro server. */
  std::string filename_;

  /* File size (can cross-validate with downloader) */
  size_t size_{0};

  void print(FILE *ofile) const;

  /**
   * NOTE: the apt lists provide far more information
   * than listed here. Take a look at the files in `/var/lib/apt/lists`
   * when trying to add new features for this.
   */
};

/**
 * Parse a record from a file stream.
 */
std::pair<std::string, PackageDetail> AptParse(std::ifstream &ifile);

/**
 * Parse an apt list, and append the results to the map.
 */
void AptParse(std::ifstream &ifile,
              std::unordered_map<std::string, PackageDetail> &table);

void AptParse(
    std::ifstream &ifile,
    std::unordered_map<std::string, std::vector<PackageDetail>> &table);

#endif /* _APTLIST_H_ 1 */
