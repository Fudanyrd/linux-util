# Yet Another APT

The most interesting feature of my `apt` is probably
downloading all dependencies of a given list of package.

For example, to run bare linux and `systemd`, one can
do the following to download all dependencies of it:

```sh
# This have been tested by x86_64 linux kernel,
# on both QEMU and Virtual Box platforms.
./apt-debug download-dep\
    systemd\
    bash\
    nano\
    init\
    dash\
    coreutils\
    login\
    strace\
    libpam-systemd\
    gawk\
    sudo
```

And then you can extract all downloaded packages into a disk
image, and mount it inside Linux OS.

