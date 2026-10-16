#!/bin/sh
# Run the native driver, which aggregates failures and restores configuration.
set -eu
cd "$(dirname "$0")/.."
if [ -n "${MWCC_TOOLS:-}" ]; then
    exec "$MWCC_TOOLS" gates "$@"
fi
exec cargo run --locked --release -p mwcc-tools -- gates "$@"
