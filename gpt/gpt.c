// create a GUID partition table on a device.
// @see https://uefi.org/specs/UEFI/2.10/05_GUID_Partition_Table_Format.html
//

//
// terminologies
// LBA: Logical Block Address
//

#include "endian.h"
#include "fat.h"
#include "gpt.h"

#include <assert.h>
#include <fcntl.h>
#include <stdbool.h>
#include <errno.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// Credit:
// https://github.com/gcc-mirror/gcc/blob/master/libiberty/crc32.c

static const uint32_t crc32_tab[] = {
      0x00000000L, 0x77073096L, 0xee0e612cL, 0x990951baL, 0x076dc419L,
      0x706af48fL, 0xe963a535L, 0x9e6495a3L, 0x0edb8832L, 0x79dcb8a4L,
      0xe0d5e91eL, 0x97d2d988L, 0x09b64c2bL, 0x7eb17cbdL, 0xe7b82d07L,
      0x90bf1d91L, 0x1db71064L, 0x6ab020f2L, 0xf3b97148L, 0x84be41deL,
      0x1adad47dL, 0x6ddde4ebL, 0xf4d4b551L, 0x83d385c7L, 0x136c9856L,
      0x646ba8c0L, 0xfd62f97aL, 0x8a65c9ecL, 0x14015c4fL, 0x63066cd9L,
      0xfa0f3d63L, 0x8d080df5L, 0x3b6e20c8L, 0x4c69105eL, 0xd56041e4L,
      0xa2677172L, 0x3c03e4d1L, 0x4b04d447L, 0xd20d85fdL, 0xa50ab56bL,
      0x35b5a8faL, 0x42b2986cL, 0xdbbbc9d6L, 0xacbcf940L, 0x32d86ce3L,
      0x45df5c75L, 0xdcd60dcfL, 0xabd13d59L, 0x26d930acL, 0x51de003aL,
      0xc8d75180L, 0xbfd06116L, 0x21b4f4b5L, 0x56b3c423L, 0xcfba9599L,
      0xb8bda50fL, 0x2802b89eL, 0x5f058808L, 0xc60cd9b2L, 0xb10be924L,
      0x2f6f7c87L, 0x58684c11L, 0xc1611dabL, 0xb6662d3dL, 0x76dc4190L,
      0x01db7106L, 0x98d220bcL, 0xefd5102aL, 0x71b18589L, 0x06b6b51fL,
      0x9fbfe4a5L, 0xe8b8d433L, 0x7807c9a2L, 0x0f00f934L, 0x9609a88eL,
      0xe10e9818L, 0x7f6a0dbbL, 0x086d3d2dL, 0x91646c97L, 0xe6635c01L,
      0x6b6b51f4L, 0x1c6c6162L, 0x856530d8L, 0xf262004eL, 0x6c0695edL,
      0x1b01a57bL, 0x8208f4c1L, 0xf50fc457L, 0x65b0d9c6L, 0x12b7e950L,
      0x8bbeb8eaL, 0xfcb9887cL, 0x62dd1ddfL, 0x15da2d49L, 0x8cd37cf3L,
      0xfbd44c65L, 0x4db26158L, 0x3ab551ceL, 0xa3bc0074L, 0xd4bb30e2L,
      0x4adfa541L, 0x3dd895d7L, 0xa4d1c46dL, 0xd3d6f4fbL, 0x4369e96aL,
      0x346ed9fcL, 0xad678846L, 0xda60b8d0L, 0x44042d73L, 0x33031de5L,
      0xaa0a4c5fL, 0xdd0d7cc9L, 0x5005713cL, 0x270241aaL, 0xbe0b1010L,
      0xc90c2086L, 0x5768b525L, 0x206f85b3L, 0xb966d409L, 0xce61e49fL,
      0x5edef90eL, 0x29d9c998L, 0xb0d09822L, 0xc7d7a8b4L, 0x59b33d17L,
      0x2eb40d81L, 0xb7bd5c3bL, 0xc0ba6cadL, 0xedb88320L, 0x9abfb3b6L,
      0x03b6e20cL, 0x74b1d29aL, 0xead54739L, 0x9dd277afL, 0x04db2615L,
      0x73dc1683L, 0xe3630b12L, 0x94643b84L, 0x0d6d6a3eL, 0x7a6a5aa8L,
      0xe40ecf0bL, 0x9309ff9dL, 0x0a00ae27L, 0x7d079eb1L, 0xf00f9344L,
      0x8708a3d2L, 0x1e01f268L, 0x6906c2feL, 0xf762575dL, 0x806567cbL,
      0x196c3671L, 0x6e6b06e7L, 0xfed41b76L, 0x89d32be0L, 0x10da7a5aL,
      0x67dd4accL, 0xf9b9df6fL, 0x8ebeeff9L, 0x17b7be43L, 0x60b08ed5L,
      0xd6d6a3e8L, 0xa1d1937eL, 0x38d8c2c4L, 0x4fdff252L, 0xd1bb67f1L,
      0xa6bc5767L, 0x3fb506ddL, 0x48b2364bL, 0xd80d2bdaL, 0xaf0a1b4cL,
      0x36034af6L, 0x41047a60L, 0xdf60efc3L, 0xa867df55L, 0x316e8eefL,
      0x4669be79L, 0xcb61b38cL, 0xbc66831aL, 0x256fd2a0L, 0x5268e236L,
      0xcc0c7795L, 0xbb0b4703L, 0x220216b9L, 0x5505262fL, 0xc5ba3bbeL,
      0xb2bd0b28L, 0x2bb45a92L, 0x5cb36a04L, 0xc2d7ffa7L, 0xb5d0cf31L,
      0x2cd99e8bL, 0x5bdeae1dL, 0x9b64c2b0L, 0xec63f226L, 0x756aa39cL,
      0x026d930aL, 0x9c0906a9L, 0xeb0e363fL, 0x72076785L, 0x05005713L,
      0x95bf4a82L, 0xe2b87a14L, 0x7bb12baeL, 0x0cb61b38L, 0x92d28e9bL,
      0xe5d5be0dL, 0x7cdcefb7L, 0x0bdbdf21L, 0x86d3d2d4L, 0xf1d4e242L,
      0x68ddb3f8L, 0x1fda836eL, 0x81be16cdL, 0xf6b9265bL, 0x6fb077e1L,
      0x18b74777L, 0x88085ae6L, 0xff0f6a70L, 0x66063bcaL, 0x11010b5cL,
      0x8f659effL, 0xf862ae69L, 0x616bffd3L, 0x166ccf45L, 0xa00ae278L,
      0xd70dd2eeL, 0x4e048354L, 0x3903b3c2L, 0xa7672661L, 0xd06016f7L,
      0x4969474dL, 0x3e6e77dbL, 0xaed16a4aL, 0xd9d65adcL, 0x40df0b66L,
      0x37d83bf0L, 0xa9bcae53L, 0xdebb9ec5L, 0x47b2cf7fL, 0x30b5ffe9L,
      0xbdbdf21cL, 0xcabac28aL, 0x53b39330L, 0x24b4a3a6L, 0xbad03605L,
      0xcdd70693L, 0x54de5729L, 0x23d967bfL, 0xb3667a2eL, 0xc4614ab8L,
      0x5d681b02L, 0x2a6f2b94L, 0xb40bbe37L, 0xc30c8ea1L, 0x5a05df1bL,
      0x2d02ef8dL
   };


static uint32_t
__efi_crc32(const void *buf, unsigned long len, uint32_t seed)
{
  unsigned long i;
  register uint32_t crc32val;
  const unsigned char *s = buf;

  crc32val = seed;
  for (i = 0;  i < len;  i ++)
    {
      crc32val =
	crc32_tab[(crc32val ^ s[i]) & 0xff] ^
	  (crc32val >> 8);
    }
  return crc32val;
}

static uint32_t 
crc32(const void *buf, unsigned long len) {
  return (__efi_crc32 (buf, len, ~0L) ^ ~0L);
}

//
// a partition record,
// see https://uefi.org/specs/UEFI/2.10/05_GUID_Partition_Table_Format.html#legacy-master-boot-record-MBR
// table 5.2
// 
struct PartitionRecord {
  // 0x80 indicates that this is the bootable legacy partition. 
  // Other values indicate that this is not a bootable 
  // legacy partition. This field shall not be used 
  // by UEFI firmware.
  uint8_t bootIndicator;

  // Start of partition in CHS address format. 
  // This field shall not be used by UEFI firmware.
  uint8_t startCHS[3];

  uint8_t osType;

  // End of partition in CHS address format. 
  // This field shall not be used by UEFI firmware.
  uint8_t endCHS[3];

  // Starting LBA of the partition on the disk. 
  // This field is used by UEFI firmware to 
  // determine the start of the partition.
  uint32_t startLBA;

  
  // Size of the partition in LBA units of logical blocks. 
  // This field is used by UEFI firmware to determine 
  // the size of the partition.
  uint32_t sizeInLBA;
} __attribute__((packed));

#define BLOCK_SIZE GPT_SECTOR_SIZE
#define GPT_SIGNATURE ((uint16_t)0xAA55)

//
// master boot record,
// see https://uefi.org/specs/UEFI/2.10/05_GUID_Partition_Table_Format.html#legacy-master-boot-record-MBR
//
struct MBR {
  uint8_t bootCode[440];
  // offset 440
  uint32_t rDiskSignature;
  uint16_t unknown;
  
  // one partition is `struct PartitionRecord`,
  // the rest 3 are zero.
  struct PartitionRecord records[4];
  uint16_t signature; 
  uint8_t reserved[0];
} __attribute__((packed));

// The Signature must be 0xaa55
// A Partition Record that contains an OSType value of zero or a 
// SizeInLBA value of zero may be ignored.

static inline void *getSector(void *buf, size_t sector) {
  return buf + sector * BLOCK_SIZE;
}

//
// create a protective gpt.
// see https://uefi.org/specs/UEFI/2.10/05_GUID_Partition_Table_Format.html#protective-mbr-partition-record-protecting-the-entire-disk
//
static void createProtectGPT(struct MBR *mbr, size_t blockSize, size_t nBlock) {
  memset(mbr, 0, sizeof(*mbr));
  mbr->signature = GPT_SIGNATURE;
  mbr->rDiskSignature = 0;
  mbr->unknown = 0;

  struct PartitionRecord *record = &(mbr->records[0]);
  record->bootIndicator = 0x80;

  uint32_t start = 0x000200;
  memcpy(&record->startCHS, &start, sizeof(record->startCHS));

  record->osType = 0xEE;

  uint32_t end = blockSize * nBlock;
  if (end > 0xFFFFFF) {
    end = 0xFFFFFF;
  }
  memcpy(&record->endCHS, &end, sizeof(record->endCHS));

  record->startLBA = 1;
  record->sizeInLBA = nBlock >= UINT32_MAX ? UINT32_MAX : nBlock - 1;
}

struct GPTHeader {
  // must be string "EFI PART"
  uint8_t signature[8];
  
  // correct value is 0x00010000
  uint32_t revision;

  // size in bytes of GPT header
  uint32_t headerSize;

  // the CRC32 of the header(when set to 0)
  uint32_t headerCRC32;

  // must be zero
  uint32_t reserved;

  // The LBA that contains this data structure.
  uint64_t thisLBA;

  // LBA address of the alternate GPT Header.
  uint64_t alternateLBA;

  // The first usable logical block 
  // that may be used by a partition described 
  // by a GUID Partition Entry.
  uint64_t firstUsableLBA;

  // The last usable logical block 
  // that may be used by a partition described 
  // by a GUID Partition Entry.
  uint64_t lastUsableLBA;

  // GUID that can be used to uniquely identify the disk.
  uint8_t diskID[16];

  // The starting LBA of the GUID Partition Entry array.
  uint64_t startEntryArray;

  // The number of Partition Entries in the GUID Partition Entry array.
  uint32_t numEntries;

  // The size, in bytes, of each the GUID Partition Entry 
  // structures in the GUID Partition Entry array. 
  // the value shall be (128,256,512,...)
  uint32_t sizeEntryArray;

  // The CRC32 of the GUID Partition Entry array.
  uint32_t crc32EntryArray;

  // must set to 0
  // uint8_t reservedArr[0];
} __attribute__((packed));

// C12A7328-F81F-11D2-BA4B-00A0C93EC93B
static const struct GUID EFI_SYSTEM_PARTITION = {
  0xc12a7328,
  0xf81f,
  0x11d2,
  0xba, 0x4b,
  {0x00, 0xa0, 0xc9, 0x3e, 0xc9, 0x3b},
};

// 024DEE41-33E7-11D3-9D69-0008C781F39F
static const struct GUID PARTITION_WITH_LEGACY_MBR = {
  0x024dee41,
  0x33e7,
  0x11d3,
  0x9d, 0x69,
  {0x00, 0x08, 0xc7, 0x81, 0xf3, 0x9f}
};

struct PartitionEntry {
  // Unique ID that defines the purpose and type of this Partition. 
  // A value of zero defines that this partition entry is not being used.
  uint8_t partType[16];

  // GUID that is unique for every partition entry.
  uint8_t partId[16];

  // Starting LBA of the partition defined by this entry.
  uint64_t startLBA;

  // Ending LBA of the partition defined by this entry.
  uint64_t endLBA;

  // attributes bits,
  // bit 0: is required?
  // bit 1: no block IO protocol
  // bit 2: legacy bios bootable
  uint64_t attrs;

  // null-terminated string indicate the name
  char partitionName[72];

  // must be 0
  uint8_t reserved[0];
} __attribute__((packed));

static void writePartitionName(struct PartitionEntry *entry, const char *name) {
  int idx = 0;
  for (const char *pt = name; *pt; pt++) {
    entry->partitionName[idx++] = *pt;
    entry->partitionName[idx++] = (char)0;
  }
}

// 9E8D8D40-E4D1-3547-AD9E-86523F5E2F4A
static const struct GUID myDiskID = {
  0x9e8d8d40,
  0xe4d1,
  0x3547,
  0xad, 0x9e,
  {0x86, 0x52, 0x3f, 0x5e, 0x2f, 0x4a}
};

size_t GPTGenerate(const struct GPTConfig *config) {
  assert(sizeof(uint64_t) == 8);
  assert(sizeof(struct GUID) == 16);
  assert(sizeof(struct PartitionRecord) == 16);
  assert(sizeof(struct MBR) == 512);
  assert(BLOCK_SIZE >= 512);
  assert(sizeof(struct GPTHeader) == 92);
  assert(sizeof(struct GPTHeader) <= BLOCK_SIZE);
  assert(sizeof(struct PartitionEntry) == 128);
  assert(BLOCK_SIZE % sizeof(struct PartitionEntry) == 0);

  uint8_t *buf = config->buf;
  const size_t nBlock = config->volume;
  size_t nPartition = config->numPart;
  const size_t nEntryPerSector = BLOCK_SIZE / sizeof(struct PartitionEntry);
  const size_t nSectorForArray = (nEntryPerSector - 1 + nPartition) / nEntryPerSector;
  const size_t nSectorsForFS = nBlock 
    - 4 /** GPT headers, MBR, 1 reserved sector */ - nSectorForArray;
  const size_t reservedLBA = nBlock - 2;
  const size_t lastLBA = nBlock - 1;

  if (nBlock < 4 + nSectorForArray || nSectorForArray == 0) {
    // too small, or no partition!
    return 0;
  }

  // LBA 0 (i.e., the first logical block) contains a protective MBR
  createProtectGPT((struct MBR *)buf, BLOCK_SIZE, nBlock);

  // try to allocate blocks for each partition.
  size_t ret = 0;
  size_t start = 2 /** MBR + one header */ + nSectorForArray;
  size_t remains = nSectorsForFS;
  for (; ret < config->numPart; ret++) {
    struct PartitionConfig *cp = & config->partitions[ret];
    if (remains < cp->volume) {
      // cannot allocate for this partition. stop.
      break;
    } else {
      remains -= cp->volume;
      cp->startLBA = start;
      start += cp->volume;
    }
  }
  //// now ret holds number of partitions.
  nPartition = ret; 

  // write entry array.
  uint32_t entryArrayCRC; 
  do {
    struct PartitionEntry *entryArrayBase = getSector(buf, 2);
    struct PartitionEntry *entryArray = entryArrayBase;
    struct PartitionConfig *cp = config->partitions;
    for (size_t i = 0; i < nPartition; i++) {
      memset(entryArray, 0, nSectorForArray * BLOCK_SIZE);
      _generic_store_le(entryArray->startLBA, cp->startLBA);
      _generic_store_le(entryArray->endLBA, cp->startLBA + cp->volume - 1);
      _generic_store_le(entryArray->attrs, (1 << 2) | (1 << 0));
      memcpy(entryArray->partId, &(cp->partId), sizeof(entryArray->partId));
      memcpy(entryArray->partType, &(cp->partType), sizeof(struct GUID));
      const char *name = cp->name;
      writePartitionName(entryArray, name);
      //// advance
      entryArray++;
      cp++;
    } 
    entryArrayCRC = crc32(entryArrayBase, sizeof(*entryArrayBase) * nPartition);
  } while (0);

  // primary and backup GPT header.
  struct GPTHeader *lba1 = getSector(buf, 1);
  struct GPTHeader *last = getSector(buf, nBlock - 1);
  memset(lba1, 0, sizeof(*lba1));
  memset(last, 0, sizeof(*last));
  //// primary gpt header
  memcpy(&(lba1->signature), "EFI PART", sizeof(lba1->signature));
  _generic_store_le(lba1->revision, 0x00010000);
  _generic_store_le(lba1->headerSize, sizeof(struct GPTHeader));
  _generic_store_le(lba1->reserved, 0);
  _generic_store_le(lba1->thisLBA, 1);
  _generic_store_le(lba1->alternateLBA, nBlock - 1);
  _generic_store_le(lba1->firstUsableLBA, start);
  _generic_store_le(lba1->lastUsableLBA, nBlock - 2);
  memcpy(lba1->diskID, &(config->diskId), sizeof(lba1->diskID));
  _generic_store_le(lba1->startEntryArray, 2);
  _generic_store_le(lba1->numEntries, nPartition);
  _generic_store_le(lba1->sizeEntryArray, sizeof(struct PartitionEntry));
  _generic_store_le(lba1->crc32EntryArray, entryArrayCRC);
  //// backup gpt header
  memcpy(last, lba1, sizeof(*last));
  _generic_store_le(last->thisLBA, nBlock - 1);
  _generic_store_le(last->alternateLBA, 1);
  //// at this time, compute CRC32 for headers.
  _generic_store_le(lba1->headerCRC32, crc32(lba1, lba1->headerSize));
  _generic_store_le(last->headerCRC32, crc32(last, lba1->headerSize));

  return ret;
}

int GPTParse(struct GPTConfig *config) {
  uint8_t *buf = config->buf, *bootSector = getSector(buf, 0);
  struct MBR *mbr = (struct MBR *)bootSector;
  uint16_t signature;
  _generic_load_le(signature, mbr->signature);
  if (signature != GPT_SIGNATURE) {
    fprintf(stderr, "Last 2 byte of mbr is not 55aa\n");
    return EINVAL;
  }

  struct GPTHeader *gptHeader = getSector(buf, 1);
  if (memcmp(gptHeader->signature, "EFI PART", sizeof(gptHeader->signature)) != 0) {
    fprintf(stderr, "GPT header signature is not 'EFI PART'\n");
    return EINVAL;
  }
  uint32_t numEntries;
  _generic_load_le(numEntries, gptHeader->numEntries);
  uint64_t startEntryArray;
  _generic_load_le(startEntryArray, gptHeader->startEntryArray);

  config->numPart = numEntries;
  config->partitions = malloc(sizeof(struct PartitionConfig) * numEntries); 
  if (!config->partitions) {
    fprintf(stderr, "Cannot allocate memory for partition config\n");
    return ENOMEM;
  }
  struct PartitionEntry *entryArray = getSector(buf, startEntryArray);
  for (uint32_t i = 0; i < numEntries; i++) {
    struct PartitionEntry *entry = &entryArray[i];
    struct PartitionConfig *cp = &config->partitions[i];

    memcpy(&(cp->partType), entry->partType, sizeof(cp->partType));
    memcpy(&(cp->partId), entry->partId, sizeof(cp->partId));

    uint64_t startLBA, endLBA;
    _generic_load_le(startLBA, entry->startLBA);
    _generic_load_le(endLBA, entry->endLBA);
    cp->startLBA = startLBA;
    cp->volume = endLBA - startLBA + 1;

    // read partition name
    char nameBuf[73];
    int idx = 0;
    for (int j = 0; j < 72; j += 2) {
      char ch = entry->partitionName[j];
      if (ch == 0) {
        break;
      }
      nameBuf[idx++] = ch;
    }
    nameBuf[idx] = 0;
    cp->name = strdup(nameBuf);
  }
  return 0;
}

