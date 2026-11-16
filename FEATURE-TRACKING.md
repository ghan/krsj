# Station Desk and website feature tracking

This document tracks decisions and implementation status for Station Desk and the Radio Saint Johns website. Completed means the change is shipped; any remaining operational check is called out separately.

## A. Improve the listening experience

### Ready to work on

- [ ] **Archive dated Journalism files.** When a filename in the Journalism folder starts with a date in `YYYY.MM.DD` format, move it to `Archive/YYYY/` outside the active Journalism folder. Keep the original audio and filename; do not delete it.
- [x] **Add output compression/mastering.** Shipped in Station Desk 0.2.0 build 9. A stereo-linked soft-knee compressor processes the complete broadcast mix, including the microphone, with persistent controls and documented defaults. **Still needed:** listen on the station setup and adjust the defaults if needed. Folder-specific processing settings are a future feature and are out of scope for this pass.
- [x] **Fill missing artist metadata from the containing folder.** Shipped in Station Desk 0.2.0 build 6. Embedded artist metadata is preserved; the parent folder name is used when it is missing.
- [ ] **Purge expired published updates.** Now-playing expires publicly after 90 seconds without updates. The website filters history to one hour, while the worker currently stores and serves up to two hours. Change retention to one hour and ensure old persisted data is purged even when the broadcaster stops sending updates.

### Not ready to work on

- [ ] **Music genre folders and scheduling rules.** Sort music into genre folders and add scheduling or playback parameters by genre. This needs a separate scoping pass after the library has been reviewed and the genres are known. Do not design the taxonomy or scheduling rules yet.

## B. Make the static website more useful

### Ready to work on

- [x] **Show now playing and the last hour of history.** Shipped on the static site. The page shows current playback and filters actual history to the previous hour; upcoming queue choices remain private. Feed storage/retention still holds two hours; see the purge item above.
- [x] **Link the message group.** The static site links to the KRSJ Google Group at `https://groups.google.com/g/krsj`. The `/members` URL was rejected because it opened a member-list view.

## Completion checks

- Journalism files with a valid date prefix no longer appear in the active Journalism folder and remain recoverable from the archive; other filenames are unaffected.
- The Journalism archive move preserves original audio and filenames and affects only files with valid `YYYY.MM.DD` prefixes.
- The compressor is enabled on the broadcast mix and passes an on-air listening check for level consistency and artifacts.
- Expired history is removed from persistent feed storage and public responses, including after publishing stops; public retention is one hour.
- The site continues to display current playback and only history from the previous hour.
- The Google Group join link opens the group's join flow with the intended membership settings.

## Related implementation notes

- Current feed retention and stale-state behavior are described in [`feed/README.md`](feed/README.md) and [`station-desk/SPEC.md`](station-desk/SPEC.md).
- The website playback display is in [`index.html`](index.html) and [`now-playing.js`](now-playing.js).
