(() => {
  const url = window.KRSJ_FEED_URL;
  const section = document.querySelector("#playback");
  if (!url || !section) return;
  section.hidden = false;
  const current = section.querySelector("[data-current]");
  const status = section.querySelector("[data-feed-status]");
  const history = section.querySelector("[data-history]");
  const clock = new Intl.DateTimeFormat("en-US", { timeZone: "America/Los_Angeles", hour: "numeric", minute: "2-digit" });
  const title = event => event.artist ? `${event.artist} — ${event.title}` : event.title;
  async function refresh() {
    if (document.hidden) return;
    try {
      const response = await fetch(url, { cache: "no-store", signal: AbortSignal.timeout(8000) });
      if (!response.ok) throw Error("Feed unavailable");
      const feed = await response.json();
      current.textContent = feed.now ? title(feed.now) : "Track information is temporarily unavailable.";
      status.textContent = feed.stale ? "Last received playlist information" : "Recently played · past two hours";
      const cutoff = Date.now() - 2 * 60 * 60 * 1000;
      history.replaceChildren(...feed.history.filter(event => event.ended_at > cutoff).map(event => {
        const item = document.createElement("li");
        item.dataset.endedAt = String(event.ended_at);
        const time = document.createElement("time");
        time.dateTime = new Date(event.started_at).toISOString(); time.textContent = clock.format(event.started_at);
        item.append(time, document.createTextNode(`  ${title(event)}`)); return item;
      }));
    } catch {
      current.textContent = "Track information is temporarily unavailable.";
      status.textContent = "Last received playlist information";
      for (const item of history.children) {
        const timestamp = Number(item.dataset.endedAt);
        if (timestamp < Date.now() - 2 * 60 * 60 * 1000) item.remove();
      }
    }
  }
  refresh(); setInterval(refresh, 15000);
  document.addEventListener("visibilitychange", () => { if (!document.hidden) refresh(); });
})();
