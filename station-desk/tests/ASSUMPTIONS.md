# What the tests assume

These assertions describe the present implementation. They are not independent product requirements. The operator's instructions take precedence; a conflicting generated test should be rewritten along with the app. Confirmed playlist requirements are in [SPEC.md](../SPEC.md).

## Confirmed queue behavior

| Area | Assumption under test |
|---|---|
| Preview | Choose approximately the next hour from the configured folders before playback. Finish the last chosen track rather than cutting it at the horizon. |
| Advancing | Next and natural EOF consume the visible preselected order, including inserts. |
| Editing | Operator order takes precedence. Moving/removing items protects the existing edited sequence; refilling appends behind it. |
| Randomness | Use shuffled cycles per folder, with repeats after exhaustion to maintain continuous playback. Avoid an immediate cycle-boundary repeat when alternatives exist. |
| Clock markers | Quarter-hour boundaries belong to the track playing then. Show only its first boundary, and none when there is no boundary within that track. |
| Remote | Hold local playback and the chosen queue during remote selection; returning to local shifts estimates. |
| Public feed | Publish current playback and two hours of actual history, including labeled remote sessions. Public readers cannot change playback. |

## Retained or implementation-level assumptions for operator review

| Area | Current assertion or default |
|---|---|
| Launch | Opening the app does not start audio or playback. Local is selected, microphone is off. Restore current track/position paused. Start rotation is a deliberate action. |
| Audio startup | Playback controls start audio if needed. Repeated starts must not attach duplicate callbacks. Device failure is reported rather than silently choosing a different broadcasting behavior. |
| Preview edits | Current/held track is pinned. Upcoming rows can be dragged or moved by arrows. Double-click starts the selected upcoming item and keeps the other entries. Removing a row never deletes its audio file. |
| Configuration edits | Rebuild untouched generated rows when a folder/rule changes. Preserve all rows present at the operator's most recent edit; apply changed rules after that protected prefix. |
| Cadence | Track rules count music slots, including skipped slots, for new generation. Minute rules use the projected local program timeline and insert at track boundaries. Remote waiting time does not accrue local program minutes. |
| Simultaneous inserts | Inherited priority: Promos, Journalism, then Station IDs. Coalesce an overdue category to one insertion, rather than replaying every missed interval. |
| Clock | Display America/Los_Angeles station time. Intervals include their start and exclude their end. A boundary exactly at an end belongs to the next row. |
| Formats | The decoder tests cover WAV, AIFF, FLAC, Ogg/Vorbis, MP3, and M4A/AAC. An extension alone does not establish codec support. Ogg fixtures explicitly contain Vorbis. |
| Metadata | Prefer available title/artist tags; fall back to filename for title. No inferred remote song names. |
| Bad files | Exclude unreadable files from scans or skip them during advance. Stop with a visible status if no playable local source remains. |
| Group controls | Require a nonempty group name; report a pending connection timeout after 15 seconds. Joining a group does not automatically put a newly arrived DJ on air. |
| Remote isolation | Only the operator-selected peer is mixed into the remote source. New/unselected peers begin muted. Local program and microphone are not sent back to the remote DJ. |
| Local/remote fade | Fade between sources over the selected duration; default 1 second, range 0–5. Hold local file position once the outgoing fade finishes. Reversing a fade is continuous. |
| Microphone | Manual mic toggle adds the selected input over either program source. No voice-triggered activation. Invalid input selection contributes silence. |
| Duck | Manual toggle reduces program by about 12 dB; it does not reduce the microphone. No automatic voice-triggered ducking. |
| Levels | Default local and remote gains are 0.8, microphone 0.7, headphone gain 0.7. Local/remote sliders range 0–1.5, mic 0–2, headphones 0–1. |
| Output protection | Clamp final output samples to ±1; show the peak before that clamp. This is not a loudness normalizer or broadcast limiter. |
| Routing | Default broadcast is channels 1/2, headphone cue 3/4, microphone input 1. Select among active channels. Overlapping headphone/program pairs disable cue to preserve program audio. Unavailable output channels remain silent. |
| Cue | Headphones can audition program or the remote source at an independent level. Remote audition excludes local program and mic. |
| Preferences | Persist queue/order, shuffle state, folders/rules, counters, levels/routes, and paused current track/position. Discard missing files and tolerate malformed preferences. Group passwords are not saved. |
| History | A started and then skipped song counts as actually played. Pausing or switching sources closes a playback segment; resuming creates another segment. Retain segments overlapping the last two hours. |
| Publishing | Send metadata on transitions and a 15-second heartbeat, retrying off the audio thread. Public now-playing expires after 90 seconds without updates. Never publish local paths, credentials, or upcoming choices. |
| Publisher credentials | Outbound HTTPS uses a private publisher key; macOS Keychain stores it. The website receives only the public endpoint URL. |

The suite uses generated audio and a simulated device. It does not establish actual Quantum routing, microphone permission behavior, file/device picker behavior, live SonoBus connectivity, real Cloudflare delivery, or long unattended broadcast reliability. Automatic disconnect fallback/recovery remains unimplemented inherited prototype behavior.
