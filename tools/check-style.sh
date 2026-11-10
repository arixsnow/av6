#!/usr/bin/env bash
# Copyright (c) 2026, Arka Mondal
#
# SPDX-License-Identifier: BSD-3-Clause
#
# check-style.sh - fail on source layout the style rules reject
#
# A line over 80 columns passes only when it ends in a string that starts
# by column 80, so a message is never split and stays greppable.
#
# Usage:
#
#   tools/check-style.sh
#   make check-style
#
# Exit: 0 the tree is clean
#       1 a check failed

set -u

CLANG_FORMAT=${CLANG_FORMAT:-clang-format}
CLANG_FORMAT_MAJOR=23

cd "$(git rev-parse --show-toplevel)" || exit 1

major=$("$CLANG_FORMAT" --version 2>/dev/null |
    sed -n 's/.*version \([0-9][0-9]*\)\..*/\1/p')
if [ "$major" != "$CLANG_FORMAT_MAJOR" ]; then
    echo "check-style: needs clang-format $CLANG_FORMAT_MAJOR," \
        "found ${major:-none}" >&2
    exit 1
fi

status=0

git ls-files -z '*.c' '*.h' |
    xargs -0 -r "$CLANG_FORMAT" --dry-run --Werror || status=1

git grep -n -I "$(printf '\t')" -- . ':!Makefile' ':!*.mk'
case $? in
0)
    echo "check-style: tab outside a make file" >&2
    status=1
    ;;
1) ;;
*) status=1 ;;
esac

git ls-files -z '*.c' '*.h' '*.s' '*.ld' '*.lds' |
    LC_ALL=C.UTF-8 xargs -0 -r awk '
    BEGIN { sq = "\047" }
    FNR == 1 { incomment = 0 }
    {
        n = length($0)
        sstart = send = 0
        i = 1
        while (i <= n) {
            if (incomment) {
                if (substr($0, i, 2) == "*/") {
                    incomment = 0
                    i += 2
                } else {
                    i++
                }
                continue
            }
            c = substr($0, i, 1)
            if (substr($0, i, 2) == "/*") {
                incomment = 1
                i += 2
            } else if (substr($0, i, 2) == "//") {
                break
            } else if (c == "\"" || c == sq) {
                j = i + 1
                while (j <= n && substr($0, j, 1) != c) {
                    if (substr($0, j, 1) == "\\")
                        j++
                    j++
                }
                if (c == "\"") {
                    sstart = i
                    send = j
                }
                i = j + 1
            } else {
                i++
            }
        }
        if (n <= 80)
            next
        if (sstart && sstart <= 80 && substr($0, send + 1) ~ /^ *(,|[)];)? *$/)
            next
        printf "%s:%d: %d columns\n", FILENAME, FNR, n
        bad = 1
    }
    END { exit bad }' || status=1

exit $status
