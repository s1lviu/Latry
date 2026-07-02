#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 <build-dir>" >&2
    exit 64
fi

build_dir=$1
build_dir=$(CDPATH= cd -- "$build_dir" && pwd)
compile_commands="$build_dir/compile_commands.json"
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root_dir=$(CDPATH= cd -- "$script_dir/.." && pwd)

if [ ! -f "$compile_commands" ]; then
    echo "compile_commands.json not found in $build_dir" >&2
    exit 66
fi

if ! command -v jq >/dev/null 2>&1; then
    echo "jq is required to run the dead-code scan" >&2
    exit 69
fi

tmp_file=$(mktemp)
trap 'rm -f "$tmp_file"' EXIT HUP INT TERM

jq -r --arg root "$root_dir/" --arg build "$build_dir/" '
    [
        .[]
        | .command = (.command // ((.arguments // []) | join(" ")))
        | select(.file | startswith($root))
        | select(.file | startswith($build) | not)
        | select(.file | startswith($root + "3rdparty/") | not)
        | select(.file | startswith($root + "tests/") | not)
        | select(.file != ($root + "AndroidAudioRecordInput.cpp"))
        | select(.file != ($root + "AndroidAudioTrackOutput.cpp"))
        | select(.file != ($root + "ReflectorClientJni.cpp"))
        | select(.command | contains("QT_TESTLIB_LIB=1") | not)
    ]
    | sort_by(.file)
    | group_by(.file)[]
    | .[0]
    | [.directory, .command, .file]
    | @tsv
' "$compile_commands" > "$tmp_file"

if [ ! -s "$tmp_file" ]; then
    echo "No first-party compile commands were found in $compile_commands" >&2
    exit 1
fi

warning_flags='-Werror=unused-function -Werror=unused-private-field -Werror=unused-member-function -Werror=unreachable-code -Werror=unused-const-variable'
total=$(wc -l < "$tmp_file" | tr -d '[:space:]')
index=0

while IFS="$(printf '\t')" read -r directory command file; do
    index=$((index + 1))
    printf '[%s/%s] %s\n' "$index" "$total" "$file"
    (
        cd "$directory"
        /bin/sh -lc "$command $warning_flags"
    )
done < "$tmp_file"

printf 'Dead-code warning scan passed for %s translation units.\n' "$total"
