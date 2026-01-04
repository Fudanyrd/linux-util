#ifndef _GPT_H
#define _GPT_H

#include <stddef.h>
#include <stdint.h>

/**< size of a logical sector in bytes */
#define GPT_SECTOR_SIZE (512u)

//
// GUID specification
// @see https://uefi.org/specs/UEFI/2.10/Apx_A_GUID_and_Time_Formats.html?highlight=guid#guid-and-time-formats
//
struct GUID {
  uint32_t timeLow;
  uint16_t timeMid;
  uint16_t timeHigh;
  uint8_t clockSeqHigh;
  uint8_t clockSeqLow;
  uint8_t node[6];
} __attribute__((packed));

#include <stdio.h>
extern void printGUID(FILE *file, const struct GUID *guid);

struct PartitionConfig {
  struct GUID partType;
  struct GUID partId;

  const char *name; /**< null-terminated name. */
  size_t volume;    /**< volume in sectors */

  /**< write start LBA to this attr. */
  size_t startLBA;
};

struct GPTConfig {
  uint8_t *buf;  /**< a buffer that holds `volume` sectors */
  size_t volume; /**< volume of disk in sectors. */

  size_t numPart; /**< number of partitions */
  /**< configuration for each partition. */
  struct PartitionConfig *partitions;

  struct GUID diskId;
};

/**
 * @return number of partitions actually created.
 */
extern size_t GPTGenerate(const struct GPTConfig *config);

/**
 * @param config: a struct with only `buf` set.
 * @return config[out]: configuration with correctly set fields.
 * @return 0 on success; EINVAL if format is not valid.
 */
extern int GPTParse(struct GPTConfig *config);

#endif // _GPT_H
