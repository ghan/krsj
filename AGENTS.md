# Project instructions

## KRSJ handoff

- The project roadmap and current feature status live in [FEATURE-TRACKING.md](FEATURE-TRACKING.md). Read it before choosing the next task.
- The repository contains the GitHub Pages site, the macOS Station Desk app under `station-desk/`, and the Cloudflare Worker/D1 playback feed under `feed/`.
- The static site shows now playing and one hour of history. It links to the KRSJ Google Group at `https://groups.google.com/g/krsj`. The feed worker still stores and serves up to two hours; one-hour persistent cleanup is outstanding.
- The latest published Station Desk release is 0.2.0 build 9. It includes parent-folder artist fallback and the broadcast compressor. The compressor's on-air listening check is still outstanding.
- The user prefers Cloudflare setup guidance that follows the current UI step by step; previous instructions did not match the screen.

## Station Desk production releases

- Increment the user-visible semantic version for each production release so the version in the app header identifies which release is installed. Use a patch increment for routine fixes and small improvements, and choose a minor or major increment when the scope warrants it.
- Keep the app version, window/header version, installer filename, release title, and release tag aligned.
- Do not publish a new production release identified only by an increasing build or workflow-run number. Build numbers may remain in internal CI metadata, but they are not a substitute for the app version.
- The current app release is 0.2.0; plan the next routine production release as 0.2.1 unless the scope warrants a minor or major version bump. The release workflow still hard-codes the build-number tag format, so update it as part of the next release.
- Version references currently include `station-desk/Source/StationApplication.h`, `station-desk/upstream.patch`, `station-desk/scripts/package-dmg.sh`, and `.github/workflows/station-desk.yml`; keep them in sync.
