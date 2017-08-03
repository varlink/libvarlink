#!/bin/bash

set -e

rm -f symbols.list symbols.lib

nm --dynamic --defined-only libvarlink.so.0 | awk '{ print $3 }' | sort > symbols.list
grep -i VARLINK lib/libvarlink.sym | sed 's/ *;*{*//g' | sort > symbols.lib
diff -u symbols.list symbols.lib
r=$?

rm -f symbols.list symbols.lib

exit $r;
