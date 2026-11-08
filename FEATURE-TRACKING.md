# Station Desk and website feature tracking

This document tracks the next improvements to Station Desk and the Radio Saint Johns website. Status describes readiness to start, not implementation progress.

## A. Improve the listening experience

### Ready to work on

- [ ] **Archive dated Journalism files.** When a filename in the Journalism folder starts with a date in `YYYY.MM.DD` format, move it to `Archive/YYYY/` outside the active Journalism folder. Keep the original audio and filename; do not delete it.
- [ ] **Add output compression/mastering.** Add a simple mastering chain to the complete station output, including the microphone, so the broadcast level is more consistent. Use sensible documented defaults and check the sound by listening. Folder-specific processing settings are a future feature and are out of scope for this pass.
- [ ] **Fill missing artist metadata from the containing folder.** Preserve embedded artist metadata; when it is missing, use the track's parent folder name in the queue and website feed.
- [ ] **Purge expired published updates.** Ensure stale now-playing data is cleared and old playback history is pruned from stored and served feed data, including when the broadcaster stops sending updates. The current feed behavior expires now-playing after 90 seconds and retains two hours of history; align the history window with the one-hour website requirement below.

### Not ready to work on

- [ ] **Music genre folders and scheduling rules.** Sort music into genre folders and add scheduling or playback parameters by genre. This needs a separate scoping pass after the library has been reviewed and the genres are known. Do not design the taxonomy or scheduling rules yet.

## B. Make the static website more useful

### Ready to work on

- [ ] **Show now playing and the last hour of history.** Display the currently playing song and actual playback history covering the previous hour. Update the feed retention, website labels, and client-side cutoff together. Keep upcoming queue choices private. Current implementation displays two hours.
- [ ] **Add a mailing list.** Provide a clear signup on the static site and connect it to a mailing-list service. Choose the provider and subscription flow during implementation; confirm signup, confirmation, and unsubscribe behavior.

## Completion checks

- Journalism files with a valid date prefix no longer appear in the active Journalism folder and remain recoverable from the archive; other filenames are unaffected.
- Output processing is enabled on the intended broadcast output, has documented defaults, and passes a listening check for level consistency and artifacts.
- Expired current-playback state and history are removed from persisted/served feed data on schedule, including after publishing stops.
- The website identifies the current song and shows only actual playback within the previous hour.
- A visitor can join the mailing list from the site and can complete the mailing-list provider's confirmation and unsubscribe flows.

## Related implementation notes

- Current feed retention and stale-state behavior are described in [`feed/README.md`](feed/README.md) and [`station-desk/SPEC.md`](station-desk/SPEC.md).
- The website playback display is in [`index.html`](index.html) and [`now-playing.js`](now-playing.js).
