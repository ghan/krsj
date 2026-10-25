// Station Desk pushes metadata here. Public readers can only fetch history.
const TWO_HOURS = 2 * 60 * 60 * 1000;
const MAX_BYTES = 1024 * 1024;
const cors = { "Access-Control-Allow-Origin": "*", "Access-Control-Allow-Methods": "GET, OPTIONS",
  "Access-Control-Allow-Headers": "Content-Type", "Cache-Control": "no-store" };
const json = (body, status = 200) => Response.json(body, { status, headers: cors });
function event(value, live, now) {
  if (!value || typeof value !== "object" || typeof value.id !== "string" || !value.id || value.id.length > 100
      || typeof value.title !== "string" || value.title.length > 2000 || !Number.isSafeInteger(value.started_at)
      || value.started_at > now + 300000 || typeof value.artist !== "string" || value.artist.length > 2000
      || !["music", "promos", "journalism", "station ids", "remote"].includes(value.kind)) throw Error("Invalid playback event");
  if (!live && (!Number.isSafeInteger(value.ended_at) || value.ended_at < value.started_at || value.ended_at > now + 300000))
    throw Error("Invalid end time");
  // Copy only public fields; paths, keys, and any accidental extra metadata cannot leak.
  return { id: value.id, title: value.title, artist: value.artist, kind: value.kind,
    started_at: value.started_at, ...(live ? {} : { ended_at: value.ended_at }) };
}
export function validateSnapshot(input, now = Date.now()) {
  if (!input || input.version !== 1 || !Number.isSafeInteger(input.updated_at)
      || input.updated_at > now + 300000 || !["local", "remote", "stopped"].includes(input.state)
      || !Array.isArray(input.history) || input.history.length > 10000) throw Error("Invalid snapshot");
  if ((input.state === "stopped") !== (input.now === null)) throw Error("Inconsistent current playback");
  const current = input.now === null ? null : event(input.now, true, now);
  if (current && ((input.state === "remote") !== (current.kind === "remote"))) throw Error("Inconsistent source");
  const history = input.history.map(e => event(e, false, now)).filter(e => e.ended_at > now - TWO_HOURS);
  if (new Set(history.map(e => e.id)).size !== history.length || history.some(e => e.id === current?.id)) throw Error("Duplicate playback event");
  return { version: 1, updated_at: input.updated_at, state: input.state, now: current, history };
}
export function publicSnapshot(stored, now = Date.now()) {
  const stale = !stored || now - stored.updated_at > 90000;
  return { version: 1, updated_at: stored?.updated_at ?? null, stale,
    state: stale ? "unavailable" : stored.state, now: stale ? null : stored.now,
    history: (stored?.history ?? []).filter(e => e.ended_at > now - TWO_HOURS)
      .sort((a, b) => b.started_at - a.started_at) };
}
export default {
  async fetch(request, env) {
    const path = new URL(request.url).pathname;
    if (request.method === "OPTIONS" && path === "/feed.json") return new Response(null, { status: 204, headers: cors });
    if (path === "/feed.json" && request.method === "GET") {
      const row = await env.DB.prepare("SELECT snapshot FROM playback_snapshot WHERE id = 1").first();
      return json(publicSnapshot(row ? JSON.parse(row.snapshot) : null));
    }
    if (path !== "/publish") return json({ error: "Not found" }, 404);
    if (request.method !== "POST") return json({ error: "Method not allowed" }, 405);
    if (!env.PUBLISH_TOKEN) return json({ error: "Publisher is not configured" }, 503);
    if (request.headers.get("Authorization") !== `Bearer ${env.PUBLISH_TOKEN}`) return json({ error: "Unauthorized" }, 401);
    if (Number(request.headers.get("Content-Length")) > MAX_BYTES) return json({ error: "Snapshot too large" }, 413);
    try {
      const raw = await request.text();
      if (new TextEncoder().encode(raw).length > MAX_BYTES) return json({ error: "Snapshot too large" }, 413);
      const snapshot = validateSnapshot(JSON.parse(raw));
      await env.DB.prepare(`INSERT INTO playback_snapshot (id, updated_at, snapshot) VALUES (1, ?, ?)
        ON CONFLICT(id) DO UPDATE SET updated_at = excluded.updated_at, snapshot = excluded.snapshot
        WHERE excluded.updated_at > playback_snapshot.updated_at`)
        .bind(snapshot.updated_at, JSON.stringify(snapshot)).run();
      return json({ ok: true }); // retries and older snapshots are harmless
    } catch (error) {
      if (error instanceof SyntaxError || error.message?.startsWith("Invalid") || error.message?.startsWith("Inconsistent") || error.message?.startsWith("Duplicate"))
        return json({ error: "Invalid snapshot" }, 400);
      console.error("Feed storage operation failed");
      return json({ error: "Feed temporarily unavailable" }, 503);
    }
  }
};
