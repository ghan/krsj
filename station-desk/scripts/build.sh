#!/bin/bash
set -euo pipefail
overlay="$(cd "$(dirname "$0")/.." && pwd)"
source_dir="${STATION_SOURCE_DIR:-$overlay/../.build/source}"
build_dir="${STATION_BUILD_DIR:-$overlay/../.build/native}"
station_cmake="${STATION_CMAKE:-cmake}"
bash "$overlay/scripts/prepare-source.sh" "$source_dir"
args=(-S "$source_dir" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release
      "-DUniversalBinary=${STATION_UNIVERSAL:-ON}" "-DSTATION_DESK_TESTS=${STATION_DESK_TESTS:-OFF}")
if [ -n "${STATION_SDK:-}" ]; then args+=("-DCMAKE_OSX_SYSROOT=$STATION_SDK"); fi
"$station_cmake" "${args[@]}"
"$station_cmake" --build "$build_dir" --target SonoBus_Standalone -j "${STATION_JOBS:-6}"
codesign --force --deep --sign - "$build_dir/SonoBus_artefacts/Release/Standalone/Station Desk.app"
printf '\nBuilt: %s\n' "$build_dir/SonoBus_artefacts/Release/Standalone/Station Desk.app"
