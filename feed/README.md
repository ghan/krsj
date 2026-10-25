# Station website feed

Station Desk sends authenticated HTTPS snapshots to a Cloudflare Worker backed by one D1 row. GitHub Pages reads public JSON. No inbound HTTP server, port forwarding, or public access to the broadcast computer is required.

The snapshot contains now-playing metadata and actual history overlapping the last two hours. It contains no local file paths, future playlist, or audio. Remote sessions have a Remote DJ label. Repeated/out-of-order delivery cannot overwrite newer state. Public now-playing becomes unavailable after a 90-second missed heartbeat; history continues to expire independently.

## Activate with your Cloudflare account

From `feed/`, use a current Wrangler installation:

```sh
npx wrangler login
npx wrangler d1 create krsj-playback
```

Put the resulting database ID in `wrangler.toml`. Then:

```sh
npx wrangler d1 migrations apply krsj-playback --remote
npx wrangler secret put PUBLISH_TOKEN
npx wrangler deploy
```

Choose a long random publisher key and enter it at the secret prompt. Keep that key private. In Station Desk's **Website feed…** dialog, enter `https://YOUR-WORKER.workers.dev/publish` and the same key. The app saves the key in macOS Keychain and publishes every 15 seconds plus playback transitions. Environment overrides `STATION_DESK_FEED_URL` and `STATION_DESK_FEED_TOKEN` are available for managed installations.

Set `window.KRSJ_FEED_URL` in the website's `feed-config.js` to `https://YOUR-WORKER.workers.dev/feed.json`, then push that file to GitHub Pages. The widget is hidden until a URL is configured. The publisher key never belongs in the website, repository, or URL.

The app also writes a public-format `NowPlaying.json` beside its preferences for diagnostics. Feed failures run in the background and do not change playback.

Cloudflare free-plan limits still apply to requests and database operations; check the account's usage after activation. This component does not activate billing or create an account.

## Test

`node --test feed/worker.test.mjs` checks validation, authentication, public metadata, retention, stale state, and idempotent updates using an in-memory database adapter. Cloudflare D1 deployment and live connectivity need a separate activation check.
