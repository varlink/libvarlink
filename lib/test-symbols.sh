#!/bin/bash -x

set -e

libvarlink_sym=${1:-lib/libvarlink.sym}
libvarlink_a=${2:-libvarlink.a}

# 77 skip test
which readelf >/dev/null 2>&1 || exit 77
test -e "${libvarlink_sym}" || exit 77
test -e "${libvarlink_a}" || exit 77

rm -f symbols.list symbols.lib

readelf -s -W "${libvarlink_a}" | grep 'FUNC    GLOBAL DEFAULT.*varlink_' | awk '{ print $8 }' | sort > symbols.list
grep varlink_ "${libvarlink_sym}" | sed 's/[ ;]//g' | sort > symbols.lib
diff -u symbols.list symbols.lib
r=$?

rm -f symbols.list symbols.lib

exit $r;
