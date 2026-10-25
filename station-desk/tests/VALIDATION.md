# Validation — October 24, 2026

170 app/model/audio/window checks passed, plus 8 hosted-feed tests. Five deliberately introduced scheduler defects were rejected by their intended behavioral assertions.

| Suite | Passed |
|---|---:|
| mixer-baseline | 30 |
| playlist-baseline | 35 |
| features-baseline | 89 |
| legacy-audio-regressions | 14 |
| window-smoke | 2 |
| Hosted feed | 8 |

The silent-audio negative control failed the three expected audible-output checks. Failure, crash, missing output, duplicate IDs, and an incomplete feature run cannot count as successful verification.

The feature run used the production panel/engine, a simulated audio device, and isolated preferences. The new end-to-end cadence test decodes real files through their final audio blocks and checks that two music EOF events reach the exact promo already visible in the queue. Scheduler checks cover skips, edits, regeneration, shuffle cycles, preview coverage, quarter-hour ownership, long tracks, and history retention.

Fault injection exposed a weak order test: repeated shuffle cycles could place the same song at both ends. That assertion now uses distinct queue items, so choosing from the wrong end reliably fails. A preview assertion also had a timing race against the running audio callback; its hour-coverage check now inspects a paused position.

The universal binary contains x86_64 and arm64 slices. Local execution used the Apple silicon slice. The native UI was rendered and inspected; special-character labels, readiness text, markers, and row layout were checked. Release builds disable test-only entry points. The DMG was mounted read-only; its signature, version, both architectures, Applications shortcut, and image checksum passed verification. It is ad hoc signed and is not Apple notarized.

The GitHub milestone workflow ran successfully on macos-15-intel: [run 40160400841](https://github.com/ghan/krsj/actions/runs/40160400841). It passed the same 170 native checks, the 8 feed checks, and the five scheduler fault-injection checks, then rebuilt with test helpers disabled and verified/published the universal DMG.

Cloudflare account provisioning, D1 migration, endpoint credentials, and live app-to-website delivery are not activated by these tests. Real broadcast hardware and an internet DJ session still need station-side checks.

Behavioral authority and remaining assumptions: [SPEC.md](../SPEC.md). Full local logs are generated under `.build/test-results/final`; CI preserves logs as a workflow artifact.

## Evaluated source hashes

- `Source/StationApplication.h`: `af751519a8f91ba0bc5bd10677905ff4647c1914af23db2b4a8874cfcb19d144`
- `Source/StationPlaylist.h`: `6900612ceba7c699fd6fa1788a7a0e3365c04f90b4457bf9c1ce3f72208e0029`
- `Source/StationFeed.h`: `e05d2eed365de587530d2b15345f62e0b6f3edf7dca9edfa0da1623bbdc28495`
- `Source/StationMix.h`: `977af70d47977464338ddff227ab7acf1cf11faf8b5ac9929f936b0c93748e0f`
- `tests/StationFeatureTests.h`: `61362c2f30f17430e178d710c0000a1ca4889a93eecef337b77215addf3a6ddc`
- `tests/StationPlaylistTests.cpp`: `9053766bb44875bbc7f2d13e7c5e6d1741d9b89188f9c0003ac7428b6b49626d`

A fresh pinned upstream archive plus the checked-in patch and overlay reproduced all evaluated native build inputs byte-for-byte. The preparation script explicitly prevents Git from finding the parent website repository and silently skipping the native-source patch.

- `tests/run_tests.py`: `b1b6c9fed67def335070742d7e75d539c9a964ad116c4345dc1dcab277f1baa4`

The first hosted Intel run passed the queue checks but failed the Ogg decoder check. The fixture had relied on FFmpeg's build-dependent default codec for the Ogg container. Fixtures now explicitly encode native Vorbis and independently verify its identification header before decoder checks. The fresh local run passed all 170 checks with that fixture. The corrected hosted rerun passed and the installer was published after all gates completed.

## Published installer

[Station Desk 0.2.0 universal DMG](https://github.com/ghan/krsj/releases/download/station-desk-0.2.0-4/Station-Desk-0.2.0-universal.dmg)

Release: `station-desk-0.2.0-4`; evaluated app source commit: `c857b567ebe033144a70d48fddc5b64274a3a23a`. Both installer and checksum assets are uploaded. SHA-256 of the hosted DMG: `e945ec53e21ad450282fa669fa351c171986858582ad4085687b5ca934a0eb1b`.
