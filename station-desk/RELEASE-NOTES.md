Station Desk now prepares the next hour of local playback from the folder rotation settings. Music and inserts are already chosen in the visible list, using shuffled cycles. Next and natural track endings follow that list.

Drag upcoming tracks or use the arrow buttons to reorder them. Operator edits take precedence over generated rotation. Quarter-hour markers show the first time boundary falling within each track. Remote sessions hold the local queue; returning to local shifts its estimates.

The outbound website publisher records now-playing and the last two hours actually played. Cloudflare feed activation requires a station account, database, and publisher key; see feed/README.md. Remote sessions are labeled with the selected DJ's name.

Download the universal DMG, open it, and drag Station Desk into Applications. Supports Apple silicon and Intel Macs running macOS 11+. This development build is ad hoc signed and is not Apple notarized; first launch may require Privacy & Security → Open Anyway.

Automated checks cover the scheduler, real decoder EOF-to-insertion path using a simulated device, routing, persistence, feed validation, and native window lifecycle. Five deliberate scheduler faults must be rejected by the revised behavioral tests. Physical broadcast-device routing and a live internet DJ session still require a listening check on the station hardware.
