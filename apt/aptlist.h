#ifndef _APTLIST_H_
#define _APTLIST_H_ 1

#include <fstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

struct PackageDetail {
  PackageDetail() = default;
  /* Dependencies. */
  std::vector<std::string> deps_;

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

#endif /* _APTLIST_H_ 1 */
