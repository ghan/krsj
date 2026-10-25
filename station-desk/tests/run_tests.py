#!/usr/bin/env python3
"""Real application tests plus mutation evaluation. No operator settings or audio devices used."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import time
import wave

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cmake', default=os.environ.get('STATION_CMAKE', 'cmake'))
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--ffmpeg', required=True)
    parser.add_argument('--results', type=Path, required=True)
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--no-build', action='store_true')
    args = parser.parse_args()
    args.results = args.results.resolve();args.results.mkdir(parents=True, exist_ok=True)
    args.build_dir = args.build_dir.resolve()
    report = {'commands': [], 'mutations': [], 'started': time.strftime('%Y-%m-%dT%H:%M:%S'),
              'scope': 'Offline simulation and production-code tests; no physical audio or internet peer verification.'}

    def command(name, argv, env=None, timeout=180):
        started=time.monotonic()
        try:
            result=subprocess.run([str(x) for x in argv], env=env, stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT, text=True, timeout=timeout)
            code,output=result.returncode,result.stdout
        except subprocess.TimeoutExpired as e:
            code=124;output=e.stdout or b''
            if isinstance(output,bytes):output=output.decode(errors='replace')
            output+='\nTIMEOUT\n'
        log=args.results/(name+'.log');log.write_text(output)
        item={'name':name,'exit':code,'seconds':round(time.monotonic()-started,2),'log':str(log),
              'passed':[line[5:] for line in output.splitlines() if line.startswith('PASS ')],
              'failed':[line[5:] for line in output.splitlines() if line.startswith('FAIL ')]}
        report['commands'].append(item)
        print(f'{name}: exit={code}, {len(item["passed"])} pass, {len(item["failed"])} fail',flush=True)
        if item['failed']:print('Failed checks: '+', '.join(item['failed']),flush=True)
        return item

    def require(item, checks=0):
        if item['exit'] or item['failed'] or len(item['passed'])<checks or len(set(item['passed']))!=len(item['passed']):
            raise RuntimeError('Failed or incomplete check: '+item['name']+'; see '+item['log'])

    def build(name):
        require(command(name, [args.cmake,'--build',args.build_dir,'--target','SonoBus_Standalone','-j','6'],timeout=600))
        require(command(name+'-sign',['codesign','--force','--deep','--sign','-',app]))

    app=args.build_dir/'SonoBus_artefacts/Release/Standalone/Station Desk.app'
    binary=app/'Contents/MacOS/Station Desk'
    try:
        with tempfile.TemporaryDirectory(prefix='station-tests-') as temp:
            temp=Path(temp);fixtures=temp/'fixtures';fixtures.mkdir()
            def tone(name, rate=48000, duration=8, amplitude=.35, frequency=440):
                with wave.open(str(fixtures/name),'wb') as out:
                    out.setparams((2,2,rate,0,'NONE','not compressed'))
                    out.writeframes(b''.join(struct.pack('<hh',int(amplitude*32767*math.sin(2*math.pi*frequency*i/rate)),
                                                          int(amplitude*32767*math.sin(2*math.pi*frequency*i/rate)))
                                             for i in range(int(rate*duration))))
            tone('tone.wav');tone('other.wav',44100,frequency=220);tone('short.wav',duration=.12);tone('hot.wav',amplitude=.95)
            tone('silent.wav',amplitude=0)
            for i in range(3):tone(f'insert{i}.wav',duration=2,frequency=600+i*100)
            (fixtures/'broken.wav').write_text('not audio')
            (fixtures/'library/nested').mkdir(parents=True)
            shutil.copyfile(fixtures/'tone.wav',fixtures/'library/music.wav')
            shutil.copyfile(fixtures/'other.wav',fixtures/'library/nested/submusic.wav')
            (fixtures/'library/ignore.txt').write_text('not an audio file')
            for ext in ['aiff','flac','ogg','mp3','m4a']:
                # Ogg is a container. FFmpeg's default is FLAC when libvorbis is
                # absent; the app's advertised Ogg decoder is specifically Vorbis.
                codec=['-c:a','vorbis','-strict','experimental'] if ext=='ogg' else []
                require(command('fixture-'+ext,[args.ffmpeg,'-nostdin','-y','-loglevel','error','-i',fixtures/'tone.wav',*codec,fixtures/('tone.'+ext)]))
                if ext=='ogg' and b'\x01vorbis' not in (fixtures/'tone.ogg').read_bytes()[:4096]:
                    raise RuntimeError('Ogg fixture is not Vorbis; refusing a misleading decoder test')
            env=os.environ.copy()
            def isolated():
                folder=Path(tempfile.mkdtemp(prefix='settings-',dir=temp));(folder/'test-only.marker').touch()
                return dict(env,STATION_DESK_DATA_DIR=str(folder))
            if not args.no_build:
                require(command('configure',[args.cmake,'-S',ROOT,'-B',args.build_dir,'-DSTATION_DESK_TESTS=ON','-DCMAKE_BUILD_TYPE=Release','-DUniversalBinary=OFF'],timeout=600))
                build('build')
            mixer=temp/'mixer-tests'
            require(command('compile-mixer',['clang++','-std=c++17','-O2','-DNDEBUG',ROOT/'tests/StationMixTests.cpp','-o',mixer]))
            require(command('mixer-baseline',[mixer]),30)
            playlist_test=temp/'playlist-tests'
            require(command('compile-playlist',['clang++','-std=c++17','-O2','-DNDEBUG',ROOT/'tests/StationPlaylistTests.cpp','-o',playlist_test]))
            require(command('playlist-baseline',[playlist_test]),32)
            feature_result=command('features-baseline',[binary,'--feature-tests',fixtures],isolated())
            require(feature_result,80)
            critical={'cadence.real_eof_twice_reaches_visible_promo', 'queue.folder_populates_hour_without_manual_add',
                      'queue.move_controls_change_play_order', 'queue.remote_hold_shifts_estimates_without_consuming_items',
                      'settings.operator_protection_roundtrip', 'feed.never_exports_local_file_paths'}
            if not critical.issubset(feature_result['passed']):raise RuntimeError('Required human-spec feature checks missing')
            require(command('legacy-audio-regressions',[binary,'--self-test',fixtures/'tone.wav'],isolated()),14)
            require(command('window-smoke',[binary,'--smoke-test'],isolated()),2)
            # A silent fixture must fail audio assertions, rather than passing on timers alone.
            negative=command('negative-silent-fixture',[binary,'--self-test',fixtures/'silent.wav'],isolated())
            if negative['exit']!=1 or 'Local audio reaches program' not in negative['failed']:
                raise RuntimeError('Silent-fixture negative control was not rejected correctly')
            if args.mutate:
                mix=ROOT/'Source/StationMix.h';original=mix.read_text()
                mutants=[
                    ('mix-drop-local','local * localLevel * (1 - remoteMix)','0 * localLevel * (1 - remoteMix)','mix.local_isolation.48000'),
                    ('mix-drop-remote','remote * remoteLevel * remoteMix','0 * remoteLevel * remoteMix','mix.remote_isolation.48000'),
                    ('mix-double-fade','sampleRate * fadeSeconds','sampleRate * fadeSeconds * 2','mix.half_fade.48000'),
                    ('mix-disable-duck','duck ? 0.25f : 1.f','duck ? 1.f : 1.f','mix.duck_voice_independent.48000'),
                    ('mix-drop-mic','mic * microphoneGain','0 * microphoneGain','mix.duck_voice_independent.48000'),
                    ('mix-ignore-local-gain','local * localLevel','local * 1.f','mix.local_gain.48000'),
                    ('mix-ignore-remote-gain','remote * remoteLevel','remote * 1.f','mix.remote_gain.48000'),
                    ('mix-reverse-target','remote ? 1.f : 0.f','remote ? 0.f : 1.f','mix.remote_isolation.48000')]
                for name,old,new,expected in mutants:
                    assert original.count(old)==1,(name,'ambiguous patch')
                    header=temp/'mutant.h';header.write_text(original.replace(old,new))
                    require(command(name+'-compile',['clang++','-std=c++17','-O2','-DNDEBUG',f'-DSTATION_MIX_HEADER="{header}"',ROOT/'tests/StationMixTests.cpp','-o',mixer]))
                    result=command(name,[mixer]);killed=result['exit']==1 and expected in result['failed']
                    report['mutations'].append({'name':name,'detected':killed,'expected_failure':expected})
                playlist_original=(ROOT/'Source/StationPlaylist.h').read_text()
                playlist_mutants=[
                    ('playlist-no-music-count','if (track.category == 0) ++music[i];','/* omitted music slot */','preview.every_two_music_slots_places_promo_in_visible_queue'),
                    ('playlist-wrong-marker','constexpr int64_t interval = 15 * 60 * 1000;','constexpr int64_t interval = 30 * 60 * 1000;','marker.long_track_shows_only_first_quarter_hour'),
                    ('playlist-overwrite-edits','upcoming.erase(upcoming.begin() + protectedPrefix, upcoming.end());','upcoming.clear();','edit.rule_changes_preserve_entire_operator_edited_order'),
                    ('playlist-wrong-end','current = upcoming.front(); upcoming.pop_front();','current = upcoming.back(); upcoming.pop_back();','playback.starts_first_visible_item'),
                    ('playlist-repeat-choice','auto track = bag.back(); bag.pop_back();','auto track = bag.back(); /* never exhaust bag */','shuffle.first_cycle_has_no_repeated_music')]
                for name,old,new,expected in playlist_mutants:
                    assert playlist_original.count(old)==1,(name,'ambiguous patch')
                    header=temp/'playlist-mutant.h';header.write_text(playlist_original.replace(old,new))
                    require(command(name+'-compile',['clang++','-std=c++17','-O2','-DNDEBUG',f'-DSTATION_PLAYLIST_HEADER="{header}"',ROOT/'tests/StationPlaylistTests.cpp','-o',playlist_test]))
                    result=command(name,[playlist_test]);killed=result['exit']==1 and expected in result['failed']
                    report['mutations'].append({'name':name,'detected':killed,'expected_failure':expected})
                station=ROOT/'Source/StationApplication.h'
                transport=ROOT/'deps/juce/modules/juce_audio_devices/sources/juce_AudioTransportSource.cpp'
                mutations=[
                    ('device-preopened-regression',station,'String startAudio() {','String startAudio() {\n        if (devices.getCurrentAudioDevice()) return {};','device.start_attaches_preopened_device'),
                    ('settings-forget-local-level',station,'restoreSlider("local",localVolume);','/* mutant: forget local level */','settings.levels_roundtrip'),
                    ('cue-leaks-program',station,'(cueRemote ? incoming : program)','(cueRemote ? program : program)','cue.remote_excludes_program_and_mic'),
                    ('cue-overlap-protection-removed',station,'&& (cue + 1 < out || cue > out + 1)','&& true','cue.overlap_cannot_overwrite_fm'),
                    ('completion-disabled',transport,'return positionableSource != nullptr\r\n              && positionableSource->getNextReadPosition()', 'return false && positionableSource != nullptr\r\n              && positionableSource->getNextReadPosition()','playback.eof_signals_completion')]
                originals={p:p.read_bytes() for _,p,_,_,_ in mutations}
                for name,path,old,new,expected in mutations:
                    assert originals[path].count(old.encode())==1,(name,'ambiguous patch')
                try:
                    for name,path,old,new,expected in mutations:
                        raw=originals[path];needle=old.encode();assert raw.count(needle)==1,(name,'ambiguous patch')
                        path.write_bytes(raw.replace(needle,new.encode()))
                        try:
                            build(name+'-build')
                            result=command(name,[binary,'--feature-tests',fixtures],isolated(),timeout=240)
                            killed=result['exit']==1 and expected in result['failed']
                            report['mutations'].append({'name':name,'detected':killed,'expected_failure':expected})
                        finally:path.write_bytes(raw)
                finally:
                    for path,raw in originals.items():path.write_bytes(raw)
                    build('restored-production-source-build')
                    report['source_restored']=all(p.read_bytes()==raw for p,raw in originals.items())
                if not all(m['detected'] for m in report['mutations']):raise RuntimeError('One or more mutations survived or failed for an unrelated reason')
            if args.mutate: require(command('features-final-repeat',[binary,'--feature-tests',fixtures],isolated()),80)
            require(command('signature',['codesign','--verify','--deep','--strict',app]))
            report['status']='passed'
    except (Exception, KeyboardInterrupt) as error:
        report['status']='failed';report['error']=str(error);print(error,flush=True)
    finally:
        report['finished']=time.strftime('%Y-%m-%dT%H:%M:%S')
        report['source_sha256']={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest()
                                 for p in [ROOT/'Source/StationApplication.h',ROOT/'Source/StationMix.h',ROOT/'tests/StationFeatureTests.h',ROOT/'tests/StationMixTests.cpp', ROOT/'tests/run_tests.py',ROOT/'Source/StationPlaylist.h',ROOT/'Source/StationFeed.h',ROOT/'tests/StationPlaylistTests.cpp']}
        (args.results/'results.json').write_text(json.dumps(report,indent=2)+'\n')
    return 0 if report['status']=='passed' else 1

if __name__=='__main__':raise SystemExit(main())
