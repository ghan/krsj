// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <deque>
#include <optional>
#include <random>
#include <string>
#include <vector>

// Scheduling has no audio-device or UI dependency. Queue order is authoritative:
// rotation rules choose only new entries appended to the preview.
namespace station {
struct Track {
    std::string path, title, artist;
    double seconds = 0;
    int category = 0;
    uint64_t slot = 0; // identifies a queue occurrence, even when the same file repeats
};
struct Rule { int mode = 1, amount = 1; }; // off / music slots / local program minutes
struct Counters {
    std::array<int, 3> music {};
    std::array<double, 3> seconds {};
    void include(const Track& track, double elapsed) {
        for (int i = 0; i < 3; ++i) {
            if (track.category == 0) ++music[i];
            seconds[i] += std::max(0.0, elapsed);
        }
        if (track.category > 0 && track.category < 4) {
            music[track.category - 1] = 0;
            seconds[track.category - 1] = 0;
        }
    }
};
class Playlist {
public:
    std::array<std::vector<Track>, 4> libraries, bags;
    std::array<std::string, 4> lastChosen;
    std::array<Rule, 3> rules;
    Counters consumed;
    std::deque<Track> upcoming;
    std::optional<Track> current;
    size_t protectedPrefix = 0;
    uint64_t nextSlot = 1;
    explicit Playlist(uint32_t seed = std::random_device{}()) : random(seed) {}

    void setLibrary(int category, std::vector<Track> tracks) {
        tracks.erase(std::remove_if(tracks.begin(), tracks.end(), [](const Track& t) {
            return t.path.empty() || !std::isfinite(t.seconds) || t.seconds <= 0;
        }), tracks.end());
        libraries[category] = std::move(tracks);
        bags[category].clear();
    }
    double queuedSeconds() const {
        double seconds = 0;
        for (const auto& t : upcoming) seconds += t.seconds;
        return seconds;
    }
    void fill(double remainingCurrent = 0, double horizon = 3600) {
        auto projected = consumed;
        if (current) projected.include(*current, current->seconds);
        for (const auto& t : upcoming) projected.include(t, t.seconds);
        double seconds = std::max(0.0, remainingCurrent) + queuedSeconds();
        // Every accepted item has positive duration, so this terminates even for
        // very short files. A single music file can repeat to maintain continuity.
        while (seconds < horizon) {
            int category = 0;
            for (int i = 0; i < 3; ++i) {
                const auto& r = rules[i];
                bool due = r.mode == 2 ? projected.music[i] >= r.amount
                         : r.mode == 3 && projected.seconds[i] >= r.amount * 60.0;
                if (due && !libraries[i + 1].empty()) { category = i + 1; break; }
            }
            if (libraries[category].empty()) break;
            auto t = choose(category);
            t.slot = nextSlot++;
            upcoming.push_back(t);
            projected.include(t, t.seconds);
            seconds += t.seconds;
        }
    }
    // Configuration changes replace the untouched generated tail. All rows that
    // existed at the operator's last edit retain their exact order.
    void rebuild(double remainingCurrent = 0) {
        protectedPrefix = std::min(protectedPrefix, upcoming.size());
        upcoming.erase(upcoming.begin() + protectedPrefix, upcoming.end());
        for (int cat = 0; cat < 4; ++cat) {
            bags[cat] = libraries[cat];
            auto exclude = [&](const Track& t) {
                if (t.category == cat) bags[cat].erase(std::remove_if(bags[cat].begin(), bags[cat].end(),
                    [&](const Track& candidate) { return candidate.path == t.path; }), bags[cat].end());
            };
            if (current) exclude(*current);
            for (const auto& t : upcoming) exclude(t);
            std::shuffle(bags[cat].begin(), bags[cat].end(), random);
        }
        fill(remainingCurrent);
    }
    bool advance(double elapsed) {
        if (current) consumed.include(*current, elapsed);
        current.reset();
        if (upcoming.empty()) fill();
        if (upcoming.empty()) return false;
        current = upcoming.front(); upcoming.pop_front();
        if (protectedPrefix) --protectedPrefix;
        fill(current->seconds);
        return true;
    }
    bool move(size_t from, size_t to) {
        if (from >= upcoming.size() || to >= upcoming.size() || from == to) return false;
        auto t = upcoming[from]; upcoming.erase(upcoming.begin() + from);
        upcoming.insert(upcoming.begin() + to, t);
        protectedPrefix = upcoming.size();
        return true;
    }
    void remove(size_t row) {
        if (row >= upcoming.size()) return;
        upcoming.erase(upcoming.begin() + row);
        protectedPrefix = upcoming.size();
    }
    bool moveSlot(uint64_t slot, size_t to) {
        auto found = std::find_if(upcoming.begin(), upcoming.end(), [&](const Track& t) {return t.slot == slot;});
        return found != upcoming.end() && move(size_t(found - upcoming.begin()), to);
    }
    void add(Track t) {
        if (t.path.empty() || t.seconds <= 0) return;
        t.slot = nextSlot++; upcoming.push_back(std::move(t)); protectedPrefix = upcoming.size();
    }
    static std::optional<int64_t> firstMarker(int64_t startMs, int64_t endMs) {
        constexpr int64_t interval = 15 * 60 * 1000;
        const auto marker = ((startMs + interval - 1) / interval) * interval;
        if (endMs > startMs && marker < endMs) return marker; // half-open track interval
        return {};
    }
private:
    std::mt19937 random;
    Track choose(int category) {
        auto& bag = bags[category];
        if (bag.empty()) {
            bag = libraries[category]; std::shuffle(bag.begin(), bag.end(), random);
            if (bag.size() > 1 && bag.back().path == lastChosen[category])
                std::swap(bag.back(), bag.front());
        }
        auto track = bag.back(); bag.pop_back();
        lastChosen[category] = track.path;
        return track;
    }
};
struct PlayEvent {
    std::string id, title, artist, kind;
    int64_t started = 0, ended = 0;
};
class History {
public:
    std::deque<PlayEvent> played;
    std::optional<PlayEvent> live;
    void stop(int64_t now) {
        if (live) { live->ended = std::max(live->started, now); played.push_back(*live); live.reset(); }
        prune(now);
    }
    void start(PlayEvent event, int64_t now) {
        stop(now); event.started = now; event.ended = 0; live = std::move(event);
    }
    void prune(int64_t now) {
        const auto cutoff = now - 2 * 60 * 60 * 1000;
        played.erase(std::remove_if(played.begin(), played.end(), [&](const PlayEvent& e) {
            return e.ended <= cutoff;
        }), played.end());
    }
};
} // namespace station
