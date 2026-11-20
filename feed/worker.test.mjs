import { test } from "node:test";
import assert from "node:assert/strict";
import worker, { validateSnapshot, publicSnapshot } from "./worker.mjs";
const now = Date.now();
const song = { id: "song", title: "A song", artist: "An artist", kind: "music", started_at: now - 1000 };
const snapshot = () => ({ version: 1, updated_at: now, state: "local", now: { ...song, path: "/private/music.wav" }, history: [] });
function database() {
  const db = { row: null };
  db.prepare = sql => ({
    first: async () => db.row && { snapshot: db.row.snapshot, updated_at: db.row.updated_at },
    bind: (...values) => ({ run: async () => {
      if (sql.includes("INSERT")) {
        const [updated, body] = values;
        if (!db.row || updated > db.row.updated_at) db.row = { updated_at: updated, snapshot: body };
      } else {
        const [body, expectedUpdated] = values;
        if (db.row?.updated_at === expectedUpdated) db.row.snapshot = body;
      }
    } })
  });
  return db;
}
test("exports metadata without paths or arbitrary private fields", () => {
  const s = validateSnapshot(snapshot(), now);
  assert.equal(s.now.title, "A song"); assert.equal("path" in s.now, false);
});
test("keeps only actual history overlapping the past hour", () => {
  const s = snapshot();
  s.history = [{ ...song, id: "old", started_at: now - 7200000, ended_at: now - 3600001 },
    { ...song, id: "overlap", started_at: now - 4000000, ended_at: now - 3599000 }];
  assert.deepEqual(validateSnapshot(s, now).history.map(e => e.id), ["overlap"]);
});
test("stale broadcaster stops claiming a track is currently playing", () => {
  const s = publicSnapshot(validateSnapshot(snapshot(), now), now + 90001);
  assert.equal(s.stale, true); assert.equal(s.now, null); assert.equal(s.state, "unavailable");
});
test("remote sessions have their own history entries", () => {
  const s = snapshot(); s.state = "remote"; s.now.kind = "remote"; s.now.title = "Remote DJ — Alice";
  assert.equal(validateSnapshot(s, now).now.kind, "remote");
});
test("rejects inconsistent, duplicate, and malformed playback events", () => {
  for (const alter of [s => s.state = "stopped", s => s.now.started_at = "yesterday",
    s => s.history = [{ ...song, ended_at: now }, { ...song, ended_at: now }],
    s => s.now.kind = "remote"]) {
    const s = snapshot(); alter(s); assert.throws(() => validateSnapshot(s, now));
  }
});
test("public readers cannot publish without the station credential", async () => {
  const env = { DB: database(), PUBLISH_TOKEN: "test-key" };
  const response = await worker.fetch(new Request("https://feed.test/publish", { method: "POST", body: JSON.stringify(snapshot()) }), env);
  assert.equal(response.status, 401); assert.equal(env.DB.row, null);
});
test("authenticated updates are idempotent and old deliveries cannot replace new state", async () => {
  const env = { DB: database(), PUBLISH_TOKEN: "test-key" };
  const post = s => worker.fetch(new Request("https://feed.test/publish", { method: "POST",
    headers: { Authorization: "Bearer test-key" }, body: JSON.stringify(s) }), env);
  assert.equal((await post(snapshot())).status, 200);
  const old = snapshot(); old.updated_at -= 1000; old.now.title = "Out of order delivery";
  await post(old); await post(snapshot());
  const read = await worker.fetch(new Request("https://feed.test/feed.json"), env);
  assert.equal((await read.json()).now.title, "A song");
  assert.equal(read.headers.get("Access-Control-Allow-Origin"), "*");
  assert.equal(read.headers.get("Cache-Control"), "no-store");
});
test("stopped and empty station responses are valid and do not invent playback", async () => {
  const response = await worker.fetch(new Request("https://feed.test/feed.json"), { DB: database() });
  assert.equal((await response.json()).now, null);
  const s = snapshot(); s.state = "stopped"; s.now = null;
  assert.equal(validateSnapshot(s, now).now, null);
});
test("scheduled cleanup purges expired history from persistent storage", async () => {
  const env = { DB: database() };
  env.DB.row = { updated_at: now, snapshot: JSON.stringify({ ...snapshot(), now: null, state: "stopped",
    history: [
      { ...song, id: "expired", started_at: now - 7200000, ended_at: now - 3600001 },
      { ...song, id: "recent", started_at: now - 4000000, ended_at: now - 3599000 }
    ] }) };
  await worker.scheduled({}, env, { waitUntil: promise => promise });
  assert.deepEqual(JSON.parse(env.DB.row.snapshot).history.map(e => e.id), ["recent"]);
  assert.equal(env.DB.row.updated_at, now);
});
