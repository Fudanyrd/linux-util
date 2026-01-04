/**
 * SYNOPSIS readgpt [disk image]
 *
 * Example output
 * start, end
 * 64, 1211
 * 1212, 6971
 * ...
 *
 * It also saves the data of each partition into 0.img, 1.img, ...
 */

#include "gpt.h"
#include <fcntl.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static void usage(void) { printf("readgpt [disk image]\n"); }

int main(int argc, char **argv, char **) {
  if (!argv[1]) {
    usage();
    return 0;
  }

  int fd = open(argv[1], O_RDONLY);
  if (fd < 0) {
    perror("open");
    return 1;
  }
  size_t mapLen;
  do {
    struct stat st;
    if (fstat(fd, &st) < 0) {
      perror("fstat");
      return 1;
    }
    mapLen = st.st_size;
    size_t pageSize = getpagesize();
    mapLen += pageSize - 1;
    mapLen &= ~(pageSize - 1);
  } while (0);
  void *diskImage = mmap(NULL, mapLen, PROT_READ, MAP_PRIVATE, fd, 0);

  struct GPTConfig config = {
      .buf = diskImage,
  };
  if (GPTParse(&config) != 0) {
    fprintf(stderr, "GPTParse failed\n");
    return 1;
  }
  printf("start, end\n");
  char nameBuf[64];
  for (size_t i = 0; i < config.numPart; i++) {
    struct PartitionConfig *part = &(config.partitions[i]);
    if (part->startLBA == 0)
      continue;

    printf("%zu, %zu\n", part->startLBA, part->startLBA + part->volume - 1);
    sprintf(nameBuf, "%lu.img", i);
    int ofd = open(nameBuf, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (ofd < 0) {
      perror("open output file");
      return 1;
    }
    write(ofd, diskImage + part->startLBA * GPT_SECTOR_SIZE,
          part->volume * GPT_SECTOR_SIZE);
    fsync(ofd);
    close(ofd);
  }
  return 0;
}
