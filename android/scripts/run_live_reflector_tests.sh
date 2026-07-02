#!/bin/sh

set -eu

build_dir="${1:-}"

if [ -z "$build_dir" ]; then
    echo "usage: $0 <build-dir>" >&2
    exit 2
fi

if [ ! -d "$build_dir" ]; then
    echo "build directory not found: $build_dir" >&2
    exit 2
fi

required_vars="
LATRY_LIVE_REFLECTOR_HOST
LATRY_LIVE_REFLECTOR_CALLSIGN
LATRY_LIVE_REFLECTOR_AUTH_KEY
LATRY_LIVE_REFLECTOR_PORT
"

for var_name in $required_vars; do
    eval "var_value=\${$var_name:-}"
    if [ -z "$var_value" ]; then
        echo "missing required environment variable: $var_name" >&2
        exit 2
    fi
done

export LATRY_ENABLE_LIVE_REFLECTOR_TESTS=1

cd "$build_dir"
exec ctest --output-on-failure -R tst_reflector_live_integration
