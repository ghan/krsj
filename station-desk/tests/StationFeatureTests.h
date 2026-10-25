// SPDX-License-Identifier: GPL-3.0-or-later
// Included only by the opt-in test build, after StationPanel is defined.
#include <mutex>
#include <stdexcept>

namespace station_test {
struct Device final : AudioIODevice {
    Device(bool fail) : AudioIODevice("Station Test", "Station Test"), failOpen(fail) {}
    ~Device() override { close(); }
    StringArray getOutputChannelNames() override { return {"FM L", "FM R", "Cue L", "Cue R"}; }
    StringArray getInputChannelNames() override { return {"Mic 1", "Mic 2"}; }
    Array<double> getAvailableSampleRates() override { return {44100., 48000.}; }
    Array<int> getAvailableBufferSizes() override { return {128, 256, 512}; }
    int getDefaultBufferSize() override { return 256; }
    String open(const BigInteger& in, const BigInteger& out, double r, int b) override {
        if (failOpen) return "Simulated device open failure";
        inputs = in; outputs = out; rate = r; block = b; opened = true; return {};
    }
    void close() override { stop(); opened = false; }
    bool isOpen() override { return opened; }
    void start(AudioIODeviceCallback* cb) override {
        stop(); callback = cb;
        if (!callback) return;
        callback->audioDeviceAboutToStart(this); running = true;
        worker = std::thread([this] {
            while (running) {
                float in[2][512], out[4][512]{};
                for (int i=0;i<block;++i) { in[0][i]=.2f; in[1][i]=.4f; }
                const float* ip[] = {inputs[0] ? in[0] : nullptr, inputs[1] ? in[1] : nullptr};
                float* op[] = {outputs[0] ? out[0] : nullptr, outputs[1] ? out[1] : nullptr,
                               outputs[2] ? out[2] : nullptr, outputs[3] ? out[3] : nullptr};
                callback->audioDeviceIOCallbackWithContext(ip,2,op,4,block,{});
                { std::lock_guard<std::mutex> lock(mutex);
                  for(int c=0;c<4;++c) last[c]=out[c][block-1]; }
                ++blocks; Thread::sleep(jmax(1, int(1000*block/rate)));
            }
        });
    }
    void stop() override {
        running=false; if(worker.joinable())worker.join();
        if(callback)callback->audioDeviceStopped(); callback=nullptr;
    }
    bool isPlaying() override { return running; }
    String getLastError() override { return {}; }
    int getCurrentBufferSizeSamples() override { return block; }
    double getCurrentSampleRate() override { return rate; }
    int getCurrentBitDepth() override { return 32; }
    BigInteger getActiveOutputChannels() const override { return outputs; }
    BigInteger getActiveInputChannels() const override { return inputs; }
    int getOutputLatencyInSamples() override { return 0; }
    int getInputLatencyInSamples() override { return 0; }
    std::array<float,4> samples() { std::lock_guard<std::mutex> lock(mutex); return last; }
    std::atomic<int> blocks {0};
private:
    bool failOpen, opened=false;
    BigInteger inputs, outputs;
    double rate=48000; int block=256;
    AudioIODeviceCallback* callback=nullptr;
    std::atomic<bool> running {false}; std::thread worker; std::mutex mutex;
    std::array<float,4> last {};
};
struct DeviceType final : AudioIODeviceType {
    explicit DeviceType(bool fail) : AudioIODeviceType("Station Test"), failOpen(fail) {}
    void scanForDevices() override {}
    StringArray getDeviceNames(bool = false) const override { return {"Station Test"}; }
    int getDefaultDeviceIndex(bool) const override { return 0; }
    int getIndexOfDevice(AudioIODevice*,bool) const override { return 0; }
    bool hasSeparateInputsAndOutputs() const override { return false; }
    AudioIODevice* createDevice(const String&,const String&) override { return new Device(failOpen); }
    bool failOpen;
};
inline void installDevice(StationEngine& engine, bool fail=false) {
    auto& types=engine.devices.getAvailableDeviceTypes();
    while(!types.isEmpty())engine.devices.removeAudioDeviceType(types[0]);
    engine.devices.addAudioDeviceType(std::make_unique<DeviceType>(fail));
}
struct Results {
    int passed=0,failed=0;
    void check(bool ok,const char* id) {
        std::cout<<(ok?"PASS ":"FAIL ")<<id<<std::endl;
        if(ok)++passed;else ++failed;
    }
    template<class F> bool wait(F predicate,int milliseconds=1500) {
        for(int i=0;i<milliseconds/5;++i) { if(predicate())return true;Thread::sleep(5); }
        return predicate();
    }
};
inline bool near(float a,float b,float tolerance=.003f) { return std::isfinite(a)&&std::abs(a-b)<tolerance; }
}

class StationFeatureTests {
public:
    static int run(const File& fixtures) {
        using namespace station_test;
        Results t;
        const auto tone=fixtures.getChildFile("tone.wav"), other=fixtures.getChildFile("other.wav");
        auto prefs=StationEngine::dataDirectory();
        // Never allow this mode to delete or overwrite the operator's settings.
        if(SystemStats::getEnvironmentVariable("STATION_DESK_DATA_DIR","").isEmpty()
            || !prefs.getChildFile("test-only.marker").existsAsFile()) {
            std::cerr<<"Test-only settings directory with marker required"<<std::endl; return 2;
        }
        auto reset=[&] { prefs.getChildFile("StationDesk.json").deleteFile();prefs.getChildFile("AudioDevice.xml").deleteFile(); };
        reset();
        {
            auto e=std::make_unique<StationEngine>(); installDevice(*e);
            t.check(!e->playing&&!e->remoteOn&&!e->micOn&&!e->audioRunning,"startup.safe_defaults");
            t.check(near(e->receiver.getValueTreeState().getRawParameterValue(SonobusAudioProcessor::paramDefaultPeerLevel)->load(),0),"remote.new_peers_muted_by_default");
            t.check(near(e->receiver.getValueTreeState().getRawParameterValue(SonobusAudioProcessor::paramMainSendMute)->load(),1),"remote.local_send_muted");
            t.check(e->devices.initialise(2,4,nullptr,false).isEmpty(),"device.selector_opens_device");
            t.check(!e->audioRunning,"device.selector_does_not_attach_engine");
            t.check(e->startAudio().isEmpty()&&e->audioRunning,"device.start_attaches_preopened_device");
            auto* d=dynamic_cast<Device*>(e->devices.getCurrentAudioDevice());
            if(!d)throw std::runtime_error("Simulated device was not installed");
            auto freshOutput = [&](auto predicate) {
                int previous = d->blocks.load();
                return t.wait([&] { return d->blocks.load() >= previous + 3; }) && t.wait(predicate);
            };
            t.check(e->load(tone),"decode.wav");e->play();
            t.check(t.wait([&]{return e->position>.05 && e->programPeak>.01;}),"playback.advances_and_outputs");
            int before=d->blocks; double position=e->position;
            t.check(e->startAudio().isEmpty()&&e->startAudio().isEmpty(),"device.repeated_start_succeeds");
            t.wait([&]{return d->blocks>=before+20;});
            double elapsed=e->position-position;
            t.check(elapsed>.07&&elapsed<.2,"device.repeated_start_no_duplicate_callback");
            e->pause();double held=e->position;Thread::sleep(35);
            t.check(!e->playing&&std::abs(e->position-held)<.0001&&e->programPeak<.0001,"playback.pause_holds_and_silences");
            e->play(); t.check(t.wait([&]{return e->position>held+.03;}),"playback.resume_advances");
            String name=e->loadedName;
            t.check(!e->load(fixtures.getChildFile("missing.wav"))&&e->loadedName==name&&e->playing,"decode.missing_preserves_current_track");
            t.check(!e->load(fixtures.getChildFile("broken.wav")),"decode.corrupt_rejected");
            e->fade=0;e->selectRemote(true);t.wait([&]{return e->fadePosition==1;});
            Thread::sleep(30);held=e->position;Thread::sleep(35);
            t.check(std::abs(e->position-held)<.0001&&e->programPeak<.0001,"source.remote_holds_local_without_leak");
            e->micOn=true;
            t.check(freshOutput([&]{return near(d->samples()[0],.14f);}),"mic.over_remote");
            e->micInput=1;t.check(freshOutput([&]{return near(d->samples()[0],.28f);}),"mic.input_selection");
            e->micInput=99;t.check(freshOutput([&]{return near(d->samples()[0],0);}),"mic.invalid_input_silent");
            e->micInput=0;e->micLevel=1;e->duck=true;
            t.check(freshOutput([&]{return near(d->samples()[0],.2f);}),"mic.duck_does_not_duck_voice");
            e->cueRemote=true;
            t.check(freshOutput([&]{auto s=d->samples();return near(s[0],.2f)&&near(s[2],0);}),"cue.remote_excludes_program_and_mic");
            e->cueRemote=false;e->headphones=.5f;
            t.check(freshOutput([&]{return near(d->samples()[2],.1f);}),"cue.program_level");
            e->cuePair=0;e->cueRemote=true;
            t.check(freshOutput([&]{return near(d->samples()[0],.2f);}),"cue.overlap_cannot_overwrite_fm");
            e->cuePair=1;Thread::sleep(30);
            t.check(near(d->samples()[0],.2f)&&near(d->samples()[1],.2f),"cue.partial_overlap_cannot_overwrite_fm");
            e->programPair=99;Thread::sleep(30);
            t.check(near(d->samples()[0],0)&&near(d->samples()[1],0),"routing.invalid_program_silent");
            e->programPair=2;e->cuePair=0;e->cueRemote=false;Thread::sleep(30);
            t.check(near(d->samples()[2],.2f)&&near(d->samples()[0],.1f),"routing.swapped_program_and_cue");
            e->devices.closeAudioDevice();t.check(!e->audioRunning,"device.close_updates_status");
            t.check(e->startAudio().isEmpty()&&e->remoteOn,"device.restart_preserves_remote_selection");
            d=dynamic_cast<Device*>(e->devices.getCurrentAudioDevice());Thread::sleep(30);
            t.check(near(d->samples()[2],.2f),"device.restart_no_local_leak");
            e->micOn=false;e->duck=false;e->programPair=0;e->cuePair=2;e->selectRemote(false);
            t.check(t.wait([&]{return e->position>held+.03&&e->programPeak>.01;}),"source.return_resumes_local");
            for(auto ext:{"aiff","flac","ogg","mp3","m4a"}) {
                auto file=fixtures.getChildFile(String("tone.")+ext);
                auto id=String("decode.")+ext;
                bool ok=e->load(file);if(ok)e->play();
                t.check(ok&&t.wait([&]{return e->position>.03&&e->programPeak>.01;}),id.toRawUTF8());
            }
            t.check(e->load(fixtures.getChildFile("short.wav")),"playback.short_load");e->play();
            t.check(t.wait([&]{return e->ended.load();})&&!e->playing,"playback.eof_signals_completion");
            t.check(e->load(tone)&&!e->ended,"playback.new_track_clears_completion");
            e->play();bool loaded=true;
            for(int i=0;i<60;++i) {loaded=loaded&&e->load(i%2?tone:other);e->play();Thread::sleep(7);}
            t.check(loaded&&t.wait([&]{return e->position>.04&&e->programPeak>.01;}),"playback.concurrent_next_stress");
            e->localLevel=0;t.check(t.wait([&]{return e->programPeak<.00001f;}),"levels.local_zero_silences_program");
            e->localLevel=1.5f;e->load(fixtures.getChildFile("hot.wav"));e->play();
            t.check(freshOutput([&]{auto out=d->samples();return std::abs(out[0])>.99f&&std::abs(out[0])<=1.f;}),"routing.final_output_clamped");
            t.check(e->programPeak>1,"meter.preclamp_peak_warns_clipping");
            e->pause();e->transport.setSource(nullptr);
            t.check(!e->transport.hasStreamFinished(),"playback.detached_source_safe");
            e->devices.closeAudioDevice();e->prepare(48000,256);
            std::vector<float> oversized(20000,1.f);float* output[]={oversized.data(),nullptr};
            e->audioDeviceIOCallbackWithContext(nullptr,0,output,2,20000,{});
            t.check(std::all_of(oversized.begin(),oversized.end(),[](float x){return x==0;}),"routing.oversized_callback_silent");
            e->micOn=false;e->audioDeviceIOCallbackWithContext(nullptr,0,output,2,256,{});
            t.check(oversized[0]==0,"routing.null_channels_safe");e->audioDeviceStopped();
        }
        reset();
        {
            auto e=std::make_unique<StationEngine>();installDevice(*e,true);
            t.check(e->startAudio().isNotEmpty()&&!e->audioRunning,"device.open_failure_reported");
        }
        reset();
        {
            auto p=std::make_unique<StationPanel>();p->stopTimer();installDevice(p->engine);
            auto& e=p->engine;e.fade=0;
            auto add=[&](const File& f) {if(auto track=p->readTrack(f,0))p->playlist.add(*track);};
            t.check(!e.audioRunning&&p->playlist.upcoming.empty(),"panel.no_autoplay");
            p->local.onClick();t.check(!e.playing&&p->status.getText().contains("No playable"),"queue.empty_reports_status");
            add(tone);add(other);p->local.onClick();
            t.check(e.playing&&e.loadedName=="tone"&&p->playlist.upcoming.size()==1,"button.play_local_loads_queue");
            t.check(p->getNumRows()==2&&p->rowTrack(0)->title=="tone","queue.current_track_visible_and_pinned");
            p->playPause.onClick();t.check(!e.playing&&!p->history.live,"button.pause_updates_feed");
            p->playPause.onClick();t.check(e.playing&&p->history.live.has_value(),"button.resume_updates_feed");
            p->skip.onClick();t.check(e.loadedName=="other"&&p->playlist.upcoming.empty(),"button.next_follows_visible_queue");
            add(tone);p->selectedPeer="Alice";p->remote.onClick();p->skip.onClick();p->playPause.onClick();
            t.check(e.remoteOn&&p->playlist.upcoming.size()==1&&e.loadedName=="other","source.remote_blocks_next_and_pause");
            t.check(p->history.live&&p->history.live->kind=="remote"&&p->history.live->title==String::fromUTF8("Remote DJ — Alice").toStdString(),"feed.remote_session_has_no_invented_track_titles");
            t.wait([&]{return e.fadePosition==1;});Thread::sleep(30);
            auto before=p->rowTimes(1,1000000);auto after=p->rowTimes(1,1060000);
            t.check(after.first-before.first==60000,"queue.remote_hold_shifts_estimates_without_consuming_items");
            p->local.onClick();t.check(!e.remoteOn&&e.playing&&e.loadedName=="other","button.local_resumes_without_consuming_queue");
            p->queueView.updateContent();p->queueView.selectRow(0);p->remove.onClick();
            t.check(p->playlist.current&&p->playlist.upcoming.size()==1,"queue.current_cannot_be_removed");
            p->queueView.selectRow(1);p->remove.onClick();
            t.check(p->playlist.upcoming.empty()&&tone.existsAsFile(),"queue.remove_keeps_audio_file");
            p->playlist.upcoming.push_back({fixtures.getChildFile("broken.wav").getFullPathName().toStdString(),"broken","",8,0});add(tone);p->skip.onClick();
            t.check(e.loadedName=="tone"&&p->playlist.upcoming.empty(),"queue.skips_unreadable_entry");
            add(tone);add(other);p->moveRow(2,1);
            t.check(p->playlist.upcoming.front().title=="other","queue.move_controls_change_play_order");
            auto first=p->playlist.upcoming.front().path;p->refill();
            t.check(p->playlist.upcoming.front().path==first,"queue.refill_preserves_operator_order");
            p->listBoxItemDoubleClicked(1,MouseEvent(Desktop::getInstance().getMainMouseSource(),{},ModifierKeys(),0,0,0,0,0,p.get(),p.get(),Time(),{},Time(),2,false));
            t.check(e.loadedName=="other"&&p->playlist.upcoming.front().title=="tone","queue.double_click_plays_selected_and_preserves_other_rows");
            p->folders[0]=fixtures.getChildFile("library");p->scan(0);
            t.check(p->playlist.libraries[0].size()==2,"folders.recursive_supported_only");
            // Inspect the preview at a stable playback position. An advancing
            // audio callback otherwise consumes a few milliseconds between the
            // refill and the assertion, which is not a scheduling failure.
            e.pause();Thread::sleep(20);
            p->playlist.upcoming.clear();p->playlist.protectedPrefix=0;p->rotationChanged();
            t.check(p->playlist.queuedSeconds()+p->remaining()>=3600&&!e.playing,"queue.folder_populates_hour_without_manual_add");
            p->micToggle.onClick();t.check(e.micOn,"button.mic_on");p->micToggle.onClick();t.check(!e.micOn,"button.mic_off");
            p->duck.onClick();t.check(e.duck,"button.duck_on");p->duck.onClick();t.check(!e.duck,"button.duck_off");
            p->audition.onClick();t.check(e.cueRemote,"button.audition_remote");p->audition.onClick();t.check(!e.cueRemote,"button.audition_program");
            p->group.setText("  ");p->connect.onClick();t.check(!p->pendingJoin&&p->status.getText().contains("Enter"),"connection.empty_group_rejected");
            p->pendingJoin=true;p->connectionStart=Time::getMillisecondCounterHiRes()-16000;p->timerCallback();
            t.check(!p->pendingJoin&&p->status.getText().contains("timed out"),"connection.timeout_reported");
            p->pendingJoin=true;p->disconnect.onClick();t.check(!p->pendingJoin,"connection.leave_cancels_pending");
            p->updateRoutes();t.check(p->programRoute.getNumItems()==2&&p->micRoute.getNumItems()==2,"routing.device_choices");
            p->programRoute.setSelectedId(3,sendNotificationSync);p->cueRoute.setSelectedId(1,sendNotificationSync);p->micRoute.setSelectedId(2,sendNotificationSync);
            t.check(e.programPair==2&&e.cuePair==0&&e.micInput==1,"routing.controls_update_engine");
            p->localVolume.setValue(.31,sendNotificationSync);p->remoteVolume.setValue(.42,sendNotificationSync);p->micVolume.setValue(.53,sendNotificationSync);p->cueVolume.setValue(.64,sendNotificationSync);p->crossfade.setValue(2.3,sendNotificationSync);
            t.check(near(e.localLevel,.31)&&near(e.remoteLevel,.42)&&near(e.micLevel,.53)&&near(e.headphones,.64)&&near(e.fade,2.3),"levels.controls_update_engine");
            auto snapshot=JSON::toString(p->feedSnapshot());
            t.check(!snapshot.contains(fixtures.getFullPathName())&&!snapshot.contains("path"),"feed.never_exports_local_file_paths");
            p->password.setText("TEST-SECRET-DO-NOT-PERSIST");p->playlist.upcoming.clear();p->playlist.protectedPrefix=0;add(tone);p->save();
            t.check(!p->preferences().loadFileAsString().contains("TEST-SECRET"),"settings.password_not_saved");
            p.reset();
            auto restored=std::make_unique<StationPanel>();restored->stopTimer();
            t.check(!restored->playlist.upcoming.empty()&&restored->playlist.upcoming.front().path==tone.getFullPathName().toStdString(),"settings.edited_queue_order_roundtrip");
            t.check(restored->playlist.protectedPrefix==1,"settings.operator_protection_roundtrip");
            t.check(restored->playlist.libraries[0].size()==2,"settings.folders_rescanned");
            t.check(restored->playlist.current&&restored->engine.loadedName=="other","settings.current_track_restored_paused");
            t.check(near(restored->engine.localLevel,.31)&&near(restored->engine.remoteLevel,.42)&&near(restored->engine.micLevel,.53)&&near(restored->engine.headphones,.64)&&near(restored->engine.fade,2.3),"settings.levels_roundtrip");
            t.check(restored->engine.programPair==2&&restored->engine.cuePair==0&&restored->engine.micInput==1,"settings.routes_roundtrip");
            t.check(!restored->engine.audioRunning&&!restored->engine.playing&&!restored->engine.remoteOn&&!restored->engine.micOn,"settings.reopen_never_autoplays");
        }
        reset();
        {
            auto p=std::make_unique<StationPanel>();p->stopTimer();installDevice(p->engine);p->engine.fade=0;
            // Real decoder + real callback EOF + real timer: two full music slots
            // must lead to the exact promo preselected in the visible preview.
            p->playlist.setLibrary(0,{*p->readTrack(tone,0),*p->readTrack(other,0)});
            p->playlist.setLibrary(1,{*p->readTrack(fixtures.getChildFile("insert0.wav"),1)});
            p->rules[0].mode.setSelectedId(2,dontSendNotification);p->rules[0].amount.setValue(2,dontSendNotification);p->rotationChanged();
            auto expected=p->playlist.upcoming[2].path;p->local.onClick();bool endedTwice=true;
            for(int i=0;i<2;++i) {
                p->engine.transport.setPosition(p->engine.duration-.04);
                endedTwice=endedTwice&&t.wait([&]{return p->engine.ended.load();});p->timerCallback();
            }
            t.check(endedTwice&&p->playlist.current->path==expected&&p->engine.loadedName=="insert0","cadence.real_eof_twice_reaches_visible_promo");
            auto next=p->playlist.upcoming.front().path;p->skip.onClick();
            t.check(p->playlist.current->path==next,"cadence.skip_follows_same_queue_after_insert");
        }
        reset();
        {auto* state=new DynamicObject;Array<var> files;files.add(fixtures.getChildFile("missing.wav").getFullPathName());files.add(tone.getFullPathName());state->setProperty("queue",files);
         prefs.getChildFile("StationDesk.json").replaceWithText(JSON::toString(var(state)));
         auto p=std::make_unique<StationPanel>();p->stopTimer();t.check(p->playlist.upcoming.size()==1&&p->playlist.upcoming[0].path==tone.getFullPathName().toStdString(),"settings.legacy_queue_migrates_and_removes_missing_files");}
        reset();prefs.getChildFile("StationDesk.json").replaceWithText("{broken");
        {auto p=std::make_unique<StationPanel>();p->stopTimer();t.check(p->playlist.upcoming.empty()&&near(p->engine.localLevel,.8)&&!p->engine.audioRunning,"settings.corrupt_uses_defaults");}
        reset();
        std::cout<<"SUMMARY "<<t.passed<<" passed "<<t.failed<<" failed"<<std::endl;
        return t.failed?1:0;
    }
};
