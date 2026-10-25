CREATE TABLE IF NOT EXISTS playback_snapshot (
  id INTEGER PRIMARY KEY CHECK (id = 1),
  updated_at INTEGER NOT NULL,
  snapshot TEXT NOT NULL
);
