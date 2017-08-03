#!/bin/bash

set -e

# 77 skip test
which readelf >/dev/null 2>&1 || exit 77
test -e lib/libvarlink.sym || exit 77
test -e libvarlink.a || exit 77

rm -f symbols.list symbols.lib

readelf -s -W libvarlink.a | grep 'FUNC    GLOBAL DEFAULT.*varlink_' | awk '{ print $8 }' | sort > symbols.list
grep varlink_ lib/libvarlink.sym | sed 's/[ ;]//g' | sort > symbols.lib
diff -u symbols.list symbols.lib
r=$?

rm -f symbols.list symbols.lib

exit $r;
