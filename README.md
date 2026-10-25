# Radio Saint Johns / KRSJ

The static website is at [radiosaintjohns.org](https://radiosaintjohns.org).

## Station Desk for the broadcast Mac

[Download Station Desk from the latest release](https://github.com/ghan/krsj/releases/latest).
Open the DMG and drag Station Desk into Applications. The universal app supports Intel and Apple silicon Macs running macOS 11 or newer. Development builds are ad hoc signed and may require macOS **Privacy & Security → Open Anyway** on first launch.

Choose your Music folder in **FOLDER ROTATION**. The next hour is selected automatically, including inserts according to the configured rules. Choose the audio device and output pairs, then press **Start rotation**. Drag upcoming tracks or use the arrow buttons to reorder them. Your edits take precedence. **Next** follows the queue shown on screen.

The queue shows the first quarter-hour marker within each track's estimated playback. Remote playback holds the local list; returning to local shifts its estimates.

See [the reviewed behavior and remaining assumptions](station-desk/SPEC.md), [build instructions](station-desk/BUILD.md), and [website feed setup](feed/README.md).

## Source

`station-desk/` contains the Station Desk source, tests, and a patch against the pinned SonoBus/JUCE source. The preparation script downloads that exact upstream commit into `.build/source`; generated dependencies are not checked into this website repository. Upstream licenses are retained. Station Desk modifications are GPL-3.0-or-later.

The GitHub Actions milestone build runs tests, builds both Mac architectures, and attaches a DMG and checksum to a GitHub Release after a successful push to main affecting the app. Website files remain at the repository root for GitHub Pages.
