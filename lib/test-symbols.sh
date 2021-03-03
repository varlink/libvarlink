#!/bin/bash -x

set -e

libvarlink_sym=${1:-lib/libvarlink.sym}
libvarlink_a=${2:-libvarlink.a}

# 77 skip test
which readelf >/dev/null 2>&1 || exit 77
test -e "${libvarlink_sym}" || exit 77
test -e "${libvarlink_a}" || exit 77

rm -f symbols.list symbols.lib

if readelf -s -W "${libvarlink_a}" | grep -q 'FUNC    GLOBAL DEFAULT.*varlink_'; then
	readelf -s -W "${libvarlink_a}" |
		grep 'FUNC    GLOBAL DEFAULT.*varlink_' |
		awk '{ print $8 }' |
		sort >symbols.list
elif readelf -s -W "${libvarlink_a}" | grep -q gnu_lto; then
	if ! readelf -s -W --lto-syms "${libvarlink_a}" &>/dev/null; then
		echo "readelf is too old and does not understand \"--lto-syms\"" >&2
		exit 77
	fi

	readelf -s -W --lto-syms "${libvarlink_a}" 2>/dev/null |
		grep ' DEF.*DEFAULT.*FUNCTION.*varlink_' |
		while read _ _ _ _ _ _ _ f; do
			echo ${f#_}
		done |
		sort >symbols.list
else
	exit 1
fi

grep varlink_ "${libvarlink_sym}" | sed 's/[ ;]//g' | sort >symbols.lib
diff -u symbols.list symbols.lib
r=$?

rm -f symbols.list symbols.lib

exit $r
