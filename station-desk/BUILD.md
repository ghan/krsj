# Build and package

Use macOS, the Apple Command Line Tools or Xcode, and CMake 3.25–3.31. For example, install CMake 3.31.6 in a Python virtual environment. The source/dependencies are pinned to the SonoBus commit in UPSTREAM.txt.

```sh
bash station-desk/scripts/build.sh
bash station-desk/scripts/package-dmg.sh \
  '.build/native/SonoBus_artefacts/Release/Standalone/Station Desk.app' \
  .build/releases
```

The default is a universal Apple silicon/Intel build. `STATION_UNIVERSAL=OFF` makes a native-only development build. Optional variables: `STATION_CMAKE`, `STATION_SDK`, `STATION_SOURCE_DIR`, `STATION_BUILD_DIR`, `STATION_JOBS`.

For the Command Line Tools rather than a selected Xcode installation, set `DEVELOPER_DIR=/Library/Developer/CommandLineTools` and `STATION_SDK=/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk`.

## Tests

```sh
STATION_DESK_TESTS=ON bash station-desk/scripts/build.sh
python3 .build/source/tests/run_tests.py \
  --no-build --build-dir .build/native --ffmpeg /absolute/path/to/ffmpeg \
  --results .build/test-results
python3 station-desk/tests/evaluate_playlist_tests.py
node --test feed/worker.test.mjs
```

The feature tests use isolated preferences and a simulated audio device. They never open the operator's hardware or join a SonoBus group. They need permission to initialize a native macOS application; running inside a shell sandbox without desktop registration access aborts before test code executes.

The test build includes test-only command-line helpers; release builds disable them. The generated fixture tools need FFmpeg. The targeted playlist evaluation introduces five independent defects in temporary headers and requires the intended behavioral assertion to reject each one. It never edits production source.

Current validation is recorded in `station-desk/tests/VALIDATION.md`. Earlier prototype reports describe the previous queue specification.
