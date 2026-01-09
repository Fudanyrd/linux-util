#ifndef _APTLIST_H_
#define _APTLIST_H_ 1

#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <set>
#include <vector>

#include "debug.h"

struct ExprNode {
public:
  virtual ~ExprNode() = default;

  virtual void print(FILE *file) const = 0;
  virtual ExprNode *children(void) = 0;
  virtual void satisfy(std::set<std::string> &packages) const = 0;
};

struct ValueNode : public ExprNode {
public:
  ValueNode(const std::string &package) : package_(package) {}

  void print(FILE *file) const { fprintf(file, "%s", package_.c_str()); }

  ExprNode *children() { /* no children*/
    return nullptr;
  }

  void satisfy(std::set<std::string> &packages) const override {
    packages.insert(package_);
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

  void satisfy(std::set<std::string> &packages) const override {
    if (op_ == '|') {
      /* OR operator: satisfy either left or right. */
      std::set<std::string> left_set;
      left_->satisfy(left_set);
      
      std::set<std::string> right_set;
      right_->satisfy(right_set);

      if (left_set.size() <= right_set.size()) {
        packages.insert(left_set.begin(), left_set.end());
      } else {
        packages.insert(right_set.begin(), right_set.end());
      }
      
    } else if (op_ == ',') {
      /* AND operator: satisfy both left and right. */
      left_->satisfy(packages);
      right_->satisfy(packages);
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
