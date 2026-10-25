#!/usr/bin/env python3
"""Ensure human-spec scenarios reject selected scheduler defects, without editing source."""
import json
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
original = (root / 'Source/StationPlaylist.h').read_text()
mutants = [
    ('music-count', 'if (track.category == 0) ++music[i];', '/* no count */',
     'preview.every_two_music_slots_places_promo_in_visible_queue'),
    ('marker-interval', 'constexpr int64_t interval = 15 * 60 * 1000;', 'constexpr int64_t interval = 30 * 60 * 1000;',
     'marker.long_track_shows_only_first_quarter_hour'),
    ('overwrite-edits', 'upcoming.erase(upcoming.begin() + protectedPrefix, upcoming.end());', 'upcoming.clear();',
     'edit.rule_changes_preserve_entire_operator_edited_order'),
    ('wrong-end', 'current = upcoming.front(); upcoming.pop_front();', 'current = upcoming.back(); upcoming.pop_back();',
     'playback.starts_first_visible_item'),
    ('repeated-choice', 'auto track = bag.back(); bag.pop_back();', 'auto track = bag.back(); /* no exhaustion */',
     'shuffle.first_cycle_has_no_repeated_music')]
results = []
with tempfile.TemporaryDirectory(prefix='krsj-playlist-eval-') as folder:
    temp = Path(folder)
    for name, old, new, expected in mutants:
        assert original.count(old) == 1, (name, 'ambiguous mutation')
        header = temp / 'StationPlaylist.h'
        header.write_text(original.replace(old, new))
        binary = temp / 'test'
        subprocess.run(['clang++', '-std=c++17', '-O2', '-DNDEBUG',
                        f'-DSTATION_PLAYLIST_HEADER="{header}"',
                        str(root/'tests/StationPlaylistTests.cpp'), '-o', str(binary)], check=True)
        result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
        detected = result.returncode == 1 and f'FAIL {expected}\n' in result.stdout
        results.append({'defect': name, 'expected_failure': expected, 'detected': detected})
        print(('PASS ' if detected else 'FAIL ') + name)
print(json.dumps(results, indent=2))
raise SystemExit(0 if all(r['detected'] for r in results) else 1)
