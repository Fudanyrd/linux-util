FROM ubuntu:24.04
WORKDIR /home/yrd

# needed by building distribution
# starting from python-dev, these are required to build the perf tool.
RUN apt-get -y update && \
	apt-get -y install cpio wget less file xz-utils && \
	apt-get -y install libncurses5-dev pkgconf flex bison kmod && \
	apt-get -y install make binutils gcc gdb qemu-system-x86 gcc-multilib && \
	apt-get -y install bc libelf-dev libssl-dev grub-common xorriso grub-pc  && \
    apt-get -y install python-dev-is-python3 libperl-dev libslang2-dev systemtap-sdt-dev \
    libunwind-dev libdw-dev libtraceevent-dev && apt-get -y update && \
	apt-get clean && rm -rf /var/lib/apt/lists/*

# done
CMD ["sleep", "infinity"]

