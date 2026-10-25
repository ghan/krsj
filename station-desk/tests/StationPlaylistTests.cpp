// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef STATION_PLAYLIST_HEADER
#define STATION_PLAYLIST_HEADER "../Source/StationPlaylist.h"
#endif
#include STATION_PLAYLIST_HEADER
#include <iostream>
#include <set>
#include <limits>
using namespace station;
static int failures = 0, checks = 0;
static void check(bool ok, const char* name) {
    ++checks; if (!ok) ++failures;
    std::cout << (ok ? "PASS " : "FAIL ") << name << '\n';
}
static Track music(std::string name, double seconds = 240) { return {name, name, "Artist", seconds, 0}; }
static Playlist example() {
    Playlist p(17);
    p.setLibrary(0, {music("A"), music("B"), music("C"), music("D")});
    p.setLibrary(1, {{"promo", "Promo", "", 30, 1}});
    p.rules[0] = {2, 2};
    return p;
}
int main() {
    {
        auto p = example(); p.fill();
        check(p.queuedSeconds() >= 3600 && p.queuedSeconds() < 3840, "preview.covers_one_hour_with_final_track");
        check(p.upcoming[0].category == 0 && p.upcoming[1].category == 0 && p.upcoming[2].category == 1,
              "preview.every_two_music_slots_places_promo_in_visible_queue");
        std::set<std::string> cycle;
        int firstCycle = 0; bool unique = true;
        for (const auto& t : p.upcoming) if (t.category == 0 && firstCycle++ < 4)
            unique = cycle.insert(t.path).second && unique;
        check(unique && cycle.size() == 4, "shuffle.first_cycle_has_no_repeated_music");
        std::string previous; bool noAdjacent = true;
        for (const auto& t : p.upcoming) if (t.category == 0) {
            noAdjacent = noAdjacent && previous != t.path; previous = t.path;
        }
        check(noAdjacent, "shuffle.no_immediate_repeat_at_cycle_boundary");
        auto first = p.upcoming[0].path, second = p.upcoming[1].path, third = p.upcoming[2].path;
        p.advance(0); check(p.current->path == first, "playback.starts_first_preview_item");
        p.advance(1); check(p.current->path == second, "skip.advances_to_second_visible_item");
        p.advance(1); check(p.current->path == third && p.current->category == 1, "skip.follows_visible_insertion_after_two_music_slots");
        check(p.queuedSeconds() + p.current->seconds >= 3600, "preview.replenishes_during_continuous_playback");
    }
    {
        Playlist ordered;
        ordered.add(music("Unique A")); ordered.add(music("Unique B")); ordered.add(music("Unique C"));
        ordered.advance(0);
        check(ordered.current->path == "Unique A" && ordered.upcoming.front().path == "Unique B",
              "playback.starts_first_visible_item");
        check(!ordered.moveSlot(ordered.current->slot,1),"edit.drag_of_already_started_track_is_ignored");
        auto dragged=ordered.upcoming.front().slot;
        ordered.add(music("Unique D"));
        check(ordered.moveSlot(dragged,2)&&ordered.upcoming[2].path=="Unique B",
              "edit.drag_uses_occurrence_identity_after_current_row_changes");
    }
    {
        auto natural = example(), skipped = example(); natural.fill(); skipped.fill();
        bool same = true;
        for (int i = 0; i < 30; ++i) {
            natural.advance(natural.current ? natural.current->seconds : 0);
            skipped.advance(skipped.current ? 1 : 0);
            same = same && natural.current->path == skipped.current->path;
        }
        check(same, "playback.skip_and_natural_end_preserve_same_preselected_order");
        auto p = example(); p.fill();
        auto moved = p.upcoming[2].path; p.move(2, 0);
        check(p.upcoming.front().path == moved, "edit.operator_can_move_promo_ahead_of_rule_position");
        std::vector<std::string> edited;
        for (const auto& t : p.upcoming) edited.push_back(t.path);
        p.rules[0] = {2, 9}; p.rebuild();
        bool kept = p.upcoming.size() >= edited.size();
        for (size_t i = 0; i < edited.size(); ++i) kept = kept && p.upcoming[i].path == edited[i];
        check(kept, "edit.rule_changes_preserve_entire_operator_edited_order");
        auto count = p.upcoming.size(); p.fill();
        check(p.upcoming.size() == count, "preview.refill_does_not_reshuffle_or_duplicate_existing_items");
        p.advance(0); check(p.current->path == moved, "edit.playback_honors_moved_item");
        check(!p.move(99999, 0), "edit.invalid_move_is_ignored");
    }
    {
        auto p = example(); p.fill();
        p.rules[0] = {2, 1}; p.rebuild();
        check(p.upcoming[1].category == 1, "config.untouched_preview_updates_when_cadence_changes");
        p.rules[0] = {1, 1}; p.rebuild();
        check(std::all_of(p.upcoming.begin(), p.upcoming.end(), [](const Track& t) {return t.category == 0;}),
              "config.off_excludes_generated_insertions");
        p.add(music("Manual", 100)); p.rules[0] = {2, 1}; p.rebuild();
        check(std::any_of(p.upcoming.begin(), p.upcoming.end(), [](const Track& t) {return t.path == "Manual";}),
              "edit.manually_added_track_survives_configuration_changes");
        p.upcoming.front() = music("Unique manual", 180); p.remove(0); p.fill();
        check(std::none_of(p.upcoming.begin(), p.upcoming.end(), [](const Track& t) {return t.path == "Unique manual";}),
              "edit.removal_is_respected_when_preview_refills");
    }
    {
        Playlist p(9); p.setLibrary(0, {music("Long", 420)});
        p.setLibrary(1, {{"promo", "Promo", "", 30, 1}}); p.rules[0] = {3, 10}; p.fill();
        check(p.upcoming[0].category == 0 && p.upcoming[1].category == 0 && p.upcoming[2].category == 1,
              "minutes.inserts_at_first_boundary_after_interval");
        Playlist empty; empty.fill(); check(empty.upcoming.empty(), "library.empty_does_not_spin");
        Playlist invalid; invalid.setLibrary(0, {music("Zero", 0), music("NaN", std::numeric_limits<double>::quiet_NaN())});
        invalid.fill(); check(invalid.upcoming.empty(), "library.nonpositive_and_nonfinite_durations_rejected");
        Playlist single; single.setLibrary(0, {music("Only", 600)}); single.fill();
        check(single.upcoming.size() == 6, "shuffle.small_library_repeats_for_continuity");
        Playlist longTrack; longTrack.setLibrary(0, {music("Long", 7200)}); longTrack.fill();
        check(longTrack.upcoming.size() == 1, "preview.does_not_cut_long_tracks_at_one_hour");
    }
    {
        constexpr int64_t minute = 60000;
        check(Playlist::firstMarker(9*60*minute, (9*60+5)*minute) == 9*60*minute,
              "marker.includes_exact_start_boundary");
        check(!Playlist::firstMarker((9*60+7)*minute, (9*60+12)*minute), "marker.omits_track_with_no_quarter_hour");
        check(Playlist::firstMarker((9*60+7)*minute, (9*60+50)*minute) == (9*60+15)*minute,
              "marker.long_track_shows_only_first_quarter_hour");
        check(!Playlist::firstMarker((9*60+7)*minute, (9*60+15)*minute), "marker.excludes_end_boundary_owned_by_next_track");
        check(Playlist::firstMarker((23*60+58)*minute, (24*60+6)*minute) == 24*60*minute,
              "marker.crosses_midnight");
        History h; int64_t now = 3*60*60*1000;
        h.start({"a","Song A","Artist","music"}, now-8000000); h.stop(now-7300000);
        h.start({"b","Song B","Artist","music"}, now-7250000); h.stop(now-7000000);
        h.start({"c","Remote DJ — Alice","","remote"},now-1000);
        check(h.played.size() == 1 && h.played.front().id == "b", "history.keeps_events_overlapping_last_two_hours");
        check(h.live && h.live->kind == "remote", "history.remote_session_is_current_without_invented_song_metadata");
        h.stop(now); check(h.played.size() == 2 && !h.live, "history.stopped_is_not_now_playing");
        h.prune(now+7200000); check(h.played.empty(), "history.expires_without_new_playback");
    }
    std::cout << "SUMMARY " << checks-failures << " passed " << failures << " failed\n";
    return failures ? 1 : 0;
}
