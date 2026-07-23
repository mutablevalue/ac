#!/usr/bin/env bash
set -euo pipefail

ProjectDir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
BuildDir="${ProjectDir}/.build"

if ! command -v cmake >/dev/null || ! command -v ninja >/dev/null; then
    echo "Build tools are missing. Run: ./scripts/setup-fedora.sh" >&2
    exit 1
fi

if ! pkg-config --exists libevdev 2>/dev/null; then
    echo "libevdev development files are missing. Run: ./scripts/setup-fedora.sh" >&2
    exit 1
fi

ConfigureFresh=()
CacheFile="${BuildDir}/CMakeCache.txt"
if [[ -f "${CacheFile}" ]]; then
    CachedLibevdevPrefix="$(sed -n 's/^LIBEVDEV_PREFIX:INTERNAL=//p' "${CacheFile}")"
    CurrentLibevdevPrefix="$(pkg-config --variable=prefix libevdev)"
    if [[ -n "${CachedLibevdevPrefix}" && "${CachedLibevdevPrefix}" != "${CurrentLibevdevPrefix}" ]]; then
        echo "Dependency location changed; refreshing the CMake cache."
        ConfigureFresh=(--fresh)
    fi
fi

cmake "${ConfigureFresh[@]}" -S "${ProjectDir}" -B "${BuildDir}" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DAUTOCLICKER_USE_SYSTEM_DEPS=ON
cmake --build "${BuildDir}" --parallel

if [[ "${1:-}" == "--test" ]]; then
    ctest --test-dir "${BuildDir}" --output-on-failure
    exit 0
fi

exec "${BuildDir}/autoclicker-gui"
