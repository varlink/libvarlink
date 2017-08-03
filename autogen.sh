#!/bin/bash
set -e

oldpwd=$(pwd)
topdir=$(dirname $0)

cd $topdir
mkdir -p ./build/m4/
autoreconf --force --install --symlink
cd $oldpwd

if [[ "$1" == "c" ]]; then
        $topdir/configure CFLAGS="-ggdb3 -Og" --prefix=/usr --libdir=$(cd "/usr/lib/$(gcc -print-multi-os-directory)"; pwd)
        make clean
else
        echo
        echo "----------------------------------------------------------------"
        echo "Initialized build system. For a common configuration please run:"
        echo "----------------------------------------------------------------"
        echo
        echo "$topdir/configure"
        echo
fi
