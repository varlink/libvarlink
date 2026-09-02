#!/bin/bash -x

set -e

libvarlink_sym=${1:-lib/libvarlink.sym}
libvarlink_a=${2:-libvarlink.a}

# 77 skip test
which readelf >/dev/null 2>&1 || exit 77
test -e "${libvarlink_sym}" || exit 77
test -e "${libvarlink_a}" || exit 77

rm -f symbols.list symbols.lib

if readelf -s -W "${libvarlink_a}" | grep -E 'FUNC\s+(GLOBAL|WEAK)\s+DEFAULT\s+.*varlink_' >/dev/null 2>&1; then
	readelf -s -W "${libvarlink_a}" |
		awk '/FUNC/ && /DEFAULT/ && /varlink_/ { print $NF }' |
		sort >symbols.list
elif readelf -s -W "${libvarlink_a}" | grep -q gnu_lto; then
	if ! readelf -s -W --lto-syms "${libvarlink_a}" &>/dev/null; then
		echo "readelf is too old and does not understand \"--lto-syms\"" >&2
		exit 77
	fi

	readelf -s -W --lto-syms "${libvarlink_a}" 2>/dev/null |
		awk '$2 == "DEF" && $3 == "DEFAULT" && /varlink_/ { print $NF }' |
		while read -r f; do
			echo "${f#_}"
		done |
		sort -u >symbols.list
else
	echo "Error: No varlink_ symbols found in ${libvarlink_a}" >&2
	exit 1
fi

grep varlink_ "${libvarlink_sym}" | sed 's/[ ;]//g' | sort >symbols.lib
diff -u symbols.list symbols.lib
r=$?

rm -f symbols.list symbols.lib

exit $r
