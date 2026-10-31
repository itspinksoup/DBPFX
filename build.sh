#!/usr/bin/env bash
# DBPFX - Linux/macOS build: native engine, then the .NET library and tests (when dotnet is installed).
set -euo pipefail
CONFIG="${1:-Release}"
ROOT="$(cd "$(dirname "$0")" && pwd)"
cmake -S "$ROOT/native" -B "$ROOT/build/native" -DCMAKE_BUILD_TYPE="$CONFIG"
cmake --build "$ROOT/build/native" --parallel
ctest --test-dir "$ROOT/build/native" --output-on-failure
mkdir -p "$ROOT/artifacts/native"
cp "$ROOT"/build/native/libDBPFX.Native.* "$ROOT/artifacts/native/"
cp "$ROOT/build/native/dbpfx-cli" "$ROOT/artifacts/native/" || true
cd "$ROOT"   # global.json (SDK 10.0.100 or newer) is read from the current folder
if command -v dotnet >/dev/null; then
    dotnet build "$ROOT/managed/DBPFX.slnx" -c "$CONFIG"
    dotnet test "$ROOT/managed/tests/DBPFX.Tests" -c "$CONFIG" --no-build
else
    echo "dotnet not found: skipped the .NET build"
fi
