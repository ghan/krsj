# Station Desk behavior and test assumptions

The station operator's instructions in the October 24, 2026 conversation take precedence over generated tests and earlier prototype documentation.

## Confirmed by the operator

- Broadcast continuously. Prepare approximately the next hour, replenishing the tail as playback advances. Include the complete final track even when it extends past the hour.
- Choose music and inserts using the configured folder rotation definitions. The local queue contains those already chosen items before playback starts.
- Next and natural EOF advance through that same visible queue. A skip does not silently replace a scheduled insertion with another music choice.
- Operator edits always take precedence. Dragging/reordering or removing items preserves the edited queue order; generation appends to it.
- Use shuffled cycles, exhausting a folder before repeating. Small libraries may repeat to maintain continuous playback.
- Show the first quarter-hour wall-clock marker that occurs during each track. At most one marker per track. A track with no quarter-hour boundary has no marker.
- Hold the local playlist during remote DJ playback; resuming local shifts its estimates.
- The website shows now playing and the last two hours actually played. It does not publish upcoming choices.
- Remote sessions appear as Remote DJ plus the selected name, with no invented per-song remote metadata.
- Send outbound updates to a hosted service. Public website readers cannot control playback.

## Implementation choices made explicit

These are assumptions for review, not requirements established by the existence of a test:

- Startup restores the queue and current track position paused, with microphone off. The prominent Start rotation button starts/resumes local playback. Opening the app alone does not begin broadcasting.
- A music slot counts toward future generation once advanced, including when skipped. Existing choices remain in place after a skip. Minute rules place inserts at the first available track boundary after the interval in the projected local audio timeline; remote time is excluded.
- When several categories are due, the inherited priority remains Promos, Journalism, then Station IDs. Each pending category inserts once at successive boundaries.
- Configuration changes regenerate the untouched preview. After an operator edit, all rows already present are protected; changed rules affect the generated tail after those rows. This preserves exact manual order, even when it overrides a rule.
- Each folder has its own shuffle cycle. Avoid an immediate cycle-boundary repeat when the folder has alternatives. Changing folder/rule configuration begins fresh generation cycles while preserving edited rows.
- The currently playing/held row is pinned; upcoming rows are draggable and also have arrow controls. Double-click plays a selected upcoming row next and retains the others.
- Intervals include their start and exclude their end. A marker exactly at a track boundary belongs to the track starting there. App and website display station time in America/Los_Angeles. The app's timezone can be overridden with STATION_DESK_TIMEZONE.
- Prefer embedded title/artist metadata, falling back to the filename for title. History records actual playback segments, including skipped songs and separate local segments surrounding a remote session or pause.
- Website updates use a 15-second heartbeat, retry off the audio thread, and expire now-playing information after 90 seconds without an update. Played history expires after two hours, including when broadcasting stops. Failed publishing does not interrupt audio.
- Reopening restores operator order and remaining shuffle bags. Unreadable files are excluded or skipped; no playable library means a visible stopped status.
- The inherited Ogg-format check means Vorbis audio in an Ogg container. Other codecs can also use the `.ogg` extension; the file extension alone does not establish decoder support. Test fixtures explicitly select and verify Vorbis.

## Test changes

The broader inventory of inherited test assumptions is [tests/ASSUMPTIONS.md](tests/ASSUMPTIONS.md). Those defaults remain open to operator correction.

Removed the assertion that manual skips must not count toward insertion cadence, plus tests that assumed choices were made only at EOF. Replaced them with a visible preselected sequence checked through skips and through real decoder EOF callbacks.

The DSP, device routing, microphone, and remote isolation tests remain inherited behavior checks. They do not establish real Quantum hardware routing, internet SonoBus interoperability, or unattended broadcast reliability.
