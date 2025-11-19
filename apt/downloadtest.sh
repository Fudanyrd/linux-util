#!/usr/bin/sh

set -ex

t1=$( mktemp )
t2=$( mktemp )

exitcode=0

stat ./download >/dev/null || make clean default

fail() {
  echo ">> FAILURE!" 1>&2;
  exitcode=1
}

./download /ubuntu/pool/main/c/cairo/cairo_1.16.0.orig.tar.xz >$t1 || fail

# compare the results with wget.
wget https://archive.ubuntu.com/ubuntu/pool/main/c/cairo/cairo_1.16.0.orig.tar.xz \
    -O $t2 || fail

cmp -s $t1 $t2 || fail

rm -f $t1 $t2
exit $exitcode

