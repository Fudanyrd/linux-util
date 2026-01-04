all: a.img myfat.img

headers = $(shell ls *.h )
headers := Makefile

# possible options:
### -m32: build 32bit executable.
CFLAGS = 
LDFLAGS = 
DEFS = 
QEMU_FLAGS = -nographic -no-reboot -action reboot=shutdown
QEMU_FLAGS += -m 2G -smp 4 -gdb tcp::1234

%.o: %.c $(headers)
	@echo "CC $<" && gcc -Wno-overflow -static -O2 -c -g $(CFLAGS) $< -o $@

gpt: gpt.o fat.o mkgpt.o
	@echo "CCLD gpt" && gcc -static -O2 -g $(LDFLAGS) fat.o gpt.o mkgpt.o -o gpt

readgpt: gpt.o readgpt.o
	@echo "CCLD readgpt" && gcc -static -O2 -g $(LDFLAGS) gpt.o readgpt.o -o readgpt

installer: gpt.o installer.o fat.o
	@echo CCLD installer && gcc -static -O2 -g $(LDFLAGS) fat.o gpt.o installer.o -o installer

init.o: init.c $(headers)
	@echo "CC init.o" && gcc -m64 -ffreestanding -nostdlib -static -O2 -c $(CFLAGS) $< -o $@

init: init.o 
	@echo "CCLD init" && ld -static -nostdlib $(LDFLAGS) init.o -o init && strip init

init.elf: init
	@echo COPY init.elf && link init init.elf

mkfat: fat.o $(headers)
	@echo "CCLD mkfat" && gcc -static -O2 -g $(LDFLAGS) fat.o -o mkfat

myfat.img: mkfat
	@echo GEN myfat.img && ./mkfat

mbr.img: 
	@echo "GEN mbr.img" && make -C ../bootloader all DEFS=$(DEFS) && link ../bootloader/mbr.img ./mbr.img

biosmbr.img:
	@echo "GEN biosmbr.img" && make -C ../bootloader biosmbr.img DEFS=$(DEFS) && link ../bootloader/biosmbr.img ./biosmbr.img

fat.img:
	@dd if=/dev/zero of=fat.img bs=1M count=63
	@mkfs.fat -v -F 32 -S 512 -s 8 ./fat.img

a.img: gpt mbr.img init
	@echo GEN a.img && ./gpt 


.PHONY: cfdisk
cfdisk: a.img
	cfdisk a.img --read-only 


.PHONY: parted
parted: a.img 
	parted ./a.img 

.PHONY: clean 
clean:
	-clear
	-rm -f *.img mkfat gpt *.o
	-rm *.out
	-$(MAKE) -C ../bootloader clean

.PHONY: qemu 
qemu: a.img
	@qemu-system-x86_64 -hda a.img $(QEMU_FLAGS)

.PHONY: debug 
debug: a.img 
	@qemu-system-x86_64 -hda a.img -nographic -S -gdb tcp::1234

# connect qemu-gdb
.PHONY: gdb
gdb: qemu.gdb
	@gdb -x qemu.gdb

# display the fat filesystem.
.PHONY: dispfat
dispfat: myfat.img 
	@mount myfat.img /mnt/tmp && stat /mnt/tmp/echo && umount /mnt/tmp
