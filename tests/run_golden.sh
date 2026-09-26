#!/usr/bin/env bash
# Golden-file tests for the CLI.
#
# Runs the built `imessage-exporter` against the synthetic fixture
# tests/fixtures/chat.db (fake data only; see tests/fixtures/make_chat_db.py)
# in every export format and diffs the output directory against the committed
# expectation under tests/golden/<case>/.
#
#   tests/run_golden.sh [--update] [path/to/imessage-exporter]
#
# The binary defaults to build/imessage-exporter. --update rewrites the golden
# directories from the current output (review the diff before committing).
# Also wired into CTest as the "golden" test, so `ctest --test-dir build` runs it.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(dirname "$here")"
fixture="$here/fixtures/chat.db"
contacts="$here/fixtures/contacts.vcf"
golden="$here/golden"

update=0
bin=""
for arg in "$@"; do
    case "$arg" in
        --update) update=1 ;;
        -h|--help) sed -n '2,14p' "$0"; exit 0 ;;
        *) bin="$arg" ;;
    esac
done
bin="${bin:-$root/build/imessage-exporter}"
if [ ! -x "$bin" ]; then
    echo "run_golden: exporter binary not found at $bin (build first, or pass its path)" >&2
    exit 2
fi

# Timestamps render in local time; pin the zone (and locale) so the output is
# identical on every machine and CI runner.
export TZ=UTC LC_ALL=C IMSG_LOG_LEVEL=warn

tmp="$(mktemp -d "${TMPDIR:-/tmp}/imsg-golden.XXXXXX")"
trap 'rm -rf "$tmp"' EXIT

# case name | CLI arguments (in addition to --db/--output)
cases=(
    "txt|--format txt"
    "md|--format md"
    "json|--format json"
    "html|--format html"
    "android|--format android"
    "json-combined-contacts|--format json --combined --contacts-db $contacts"
    "html-stats-timeline|--format html --theme matrix --stats --timeline --me Tester"
    "txt-since-until|--format txt --since 2024-03-01 --until 2024-04-30"
)

failed=0
for spec in "${cases[@]}"; do
    name="${spec%%|*}"
    args="${spec#*|}"
    out="$tmp/$name"
    mkdir -p "$out"
    # shellcheck disable=SC2086  # word-splitting of $args is intended
    if ! "$bin" --db "$fixture" --output "$out" $args >"$tmp/$name.stdout" 2>"$tmp/$name.stderr"; then
        echo "FAIL [$name]: exporter exited non-zero" >&2
        cat "$tmp/$name.stderr" >&2
        failed=1
        continue
    fi
    if [ "$update" = 1 ]; then
        rm -rf "${golden:?}/$name"
        mkdir -p "$golden"
        cp -R "$out" "$golden/$name"
        echo "updated $name"
    elif diff -ru "$golden/$name" "$out"; then
        echo "ok   [$name]"
    else
        echo "FAIL [$name]: output differs from tests/golden/$name" >&2
        failed=1
    fi
done

# --list-chats writes to stdout rather than files; compare that text directly.
list_out="$tmp/list-chats.txt"
"$bin" --db "$fixture" --list-chats >"$list_out"
if [ "$update" = 1 ]; then
    cp "$list_out" "$golden/list-chats.txt"
    echo "updated list-chats"
elif diff -u "$golden/list-chats.txt" "$list_out"; then
    echo "ok   [list-chats]"
else
    echo "FAIL [list-chats]: output differs from tests/golden/list-chats.txt" >&2
    failed=1
fi

if [ "$update" = 1 ]; then
    echo "golden files rewritten under $golden — review with git diff"
    exit 0
fi
if [ "$failed" != 0 ]; then
    echo "golden tests FAILED (regenerate with: tests/run_golden.sh --update $bin)" >&2
    exit 1
fi
echo "golden tests passed"
