// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#if defined(__APPLE__)
#include <Security/Security.h>
#endif
#include "JuceHeader.h"
#include "SonobusPluginProcessor.h"
#include "StationMix.h"
#include <array>
#include <random>
#include <iostream>
#include <thread>
#include <ctime>
#include <cstdlib>
#include "StationPlaylist.h"
#include "StationFeed.h"
using namespace juce;

class StationEngine : public AudioIODeviceCallback {
public:
    SonobusAudioProcessor receiver;
    AudioDeviceManager devices;
    AudioFormatManager formats;
    TimeSliceThread readerThread { "Station file reader" };
    AudioTransportSource transport;
    std::unique_ptr<AudioFormatReaderSource> fileSource;
    std::atomic<bool> remoteOn { false }, micOn { false }, duck { false }, cueRemote { false };
    std::atomic<float> localLevel { 0.8f }, remoteLevel { 0.8f }, micLevel { 0.7f }, headphones { 0.7f }, fade { 1.f };
    std::atomic<int> programPair { 0 }, cuePair { 2 }, micInput { 0 };
    std::atomic<float> programPeak { 0 }, remotePeak { 0 }, fadePosition { 0 };
    std::atomic<bool> playing { false }, ended { false }, audioRunning { false };
    std::atomic<double> position { 0 };
    double duration = 0;
    String loadedName;

    StationEngine() {
        formats.registerBasicFormats(); readerThread.startThread();
        auto set = [this](const String& name, float value) {
            if (auto* p = receiver.getValueTreeState().getParameter(name))
                p->setValueNotifyingHost(p->convertTo0to1(value));
        };
        set(SonobusAudioProcessor::paramDefaultPeerLevel, 0);
        set(SonobusAudioProcessor::paramMainSendMute, 1);
        set(SonobusAudioProcessor::paramDry, 0);
        set(SonobusAudioProcessor::paramWet, 1);
    }
    ~StationEngine() override {
        devices.removeAudioCallback(this); devices.closeAudioDevice();
        transport.setSource(nullptr); readerThread.stopThread(2000);
    }
    static File dataDirectory() {
        auto overridePath = SystemStats::getEnvironmentVariable("STATION_DESK_DATA_DIR", "");
        return overridePath.isNotEmpty() ? File(overridePath) : File::getSpecialLocation(File::userApplicationDataDirectory).getChildFile("Application Support/StationDesk");
    }
    String startAudio() {
        if (devices.getCurrentAudioDevice() == nullptr) {
            auto state = XmlDocument::parse(dataDirectory().getChildFile("AudioDevice.xml"));
            auto error = devices.initialise(2, 4, state.get(), true);
            if (error.isNotEmpty()) return error;
        }
        // The device selector can open a device before Start audio is pressed.
        // JUCE deduplicates callbacks, so repeated starts are safe.
        devices.addAudioCallback(this);
        return audioRunning ? String() : "Audio device is not running. Choose an output device and try again.";
    }
    bool load(const File& file) {
        std::unique_ptr<AudioFormatReader> reader(formats.createReaderFor(file));
        if (!reader) return false;
        const double rate = reader->sampleRate;
        playing = false; ended = false;
        transport.stop(); transport.setSource(nullptr);
        fileSource.reset(new AudioFormatReaderSource(reader.release(), true));
        transport.setSource(fileSource.get(), 32768, &readerThread, rate, 2);
        duration = transport.getLengthInSeconds(); loadedName = file.getFileNameWithoutExtension();
        position = 0; ended = false; playing = false;
        return true;
    }
    void play() { if (fileSource) { ended = false; transport.start(); playing = true; } }
    void pause() { transport.stop(); playing = false; }
    void selectRemote(bool remote) {
        if (!remote && remoteOn && fileSource) play();
        remoteOn = remote;
    }
    void audioDeviceAboutToStart(AudioIODevice* device) override {
        prepare(device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples());
        audioRunning = true;
    }
    void prepare(double rate, int block) {
        sampleRate = rate;
        capacity = std::max(16384, block);
        remoteBuffer.setSize(2, capacity); localBuffer.setSize(2, capacity);
        receiver.setPlayConfigDetails(2, 2, sampleRate, block);
        receiver.prepareToPlay(sampleRate, block);
        transport.prepareToPlay(block, sampleRate); mix.reset(sampleRate);
        // Device restart preserves the selected source without briefly airing local audio.
        mix.remoteMix = remoteOn ? 1.f : 0.f;
    }
    void audioDeviceStopped() override { audioRunning = false; transport.releaseResources(); receiver.releaseResources(); }
    void audioDeviceIOCallbackWithContext(const float* const* inputs, int nIn, float* const* outputs,
                                          int nOut, int count, const AudioIODeviceCallbackContext&) override {
        ScopedNoDenormals denormals;
        for (int c = 0; c < nOut; ++c) if (outputs[c]) FloatVectorOperations::clear(outputs[c], count);
        if (count > capacity) return;
        remoteBuffer.setSize(2, count, false, false, true); remoteBuffer.clear();
        MidiBuffer midi; receiver.processBlock(remoteBuffer, midi);
        localBuffer.setSize(2, count, false, false, true); localBuffer.clear();
        // Freeze the file position after the outgoing fade; do not block the audio thread with stop().
        bool localNeeded = !remoteOn || mix.remoteMix < 1;
        if (localNeeded) {
            AudioSourceChannelInfo info(&localBuffer, 0, count); transport.getNextAudioBlock(info);
            position = transport.getCurrentPosition();
            if (playing && transport.hasStreamFinished()) { playing = false; ended = true; }
        }
        int out = programPair.load(), cue = cuePair.load(), mic = micInput.load();
        bool broadcastValid = out >= 0 && out + 1 < nOut && outputs[out] && outputs[out+1];
        bool cueValid = cue >= 0 && cue + 1 < nOut && outputs[cue] && outputs[cue+1]
                        && (cue + 1 < out || cue > out + 1);
        float peak = 0, remoteMax = 0;
        for (int i = 0; i < count; ++i) {
            mix.advance(remoteOn, fade, duck, micOn ? micLevel.load() : 0.f);
            float voice = mic >= 0 && mic < nIn && inputs[mic] ? inputs[mic][i] : 0;
            for (int c = 0; c < 2; ++c) {
                float incoming = remoteBuffer.getSample(c, i);
                float program = mix.program(localBuffer.getSample(c, i), incoming, voice, localLevel, remoteLevel);
                peak = std::max(peak, std::abs(program)); remoteMax = std::max(remoteMax, std::abs(incoming));
                if (broadcastValid) outputs[out+c][i] = jlimit(-1.f, 1.f, program);
                if (cueValid) outputs[cue+c][i] = jlimit(-1.f, 1.f, (cueRemote ? incoming : program) * headphones.load());
            }
        }
        programPeak = peak; remotePeak = remoteMax; fadePosition = mix.remoteMix;
    }
private:
    AudioBuffer<float> remoteBuffer, localBuffer;
    StationMix mix;
    int capacity = 0;
    double sampleRate = 48000;
};

class StationPanel : public Component, public DragAndDropContainer, private Timer, private ListBoxModel {
    friend class StationFeatureTests;
    class QueueList final : public ListBox, public DragAndDropTarget {
    public:
        explicit QueueList(StationPanel& p) : owner(p) {}
        bool isInterestedInDragSource(const SourceDetails& d) override {
            return d.sourceComponent.get() == this && d.description.toString().startsWith("station-slot:");
        }
        void itemDragMove(const SourceDetails& d) override {
            auto* viewport = getViewport();
            viewport->autoScroll(d.localPosition.x, d.localPosition.y, 30, 8);
        }
        void itemDropped(const SourceDetails& d) override {
            uint64_t slot = uint64_t(d.description.toString().fromFirstOccurrenceOf(":", false, false).getLargeIntValue());
            auto found=std::find_if(owner.playlist.upcoming.begin(),owner.playlist.upcoming.end(),[&](const station::Track& t){return t.slot==slot;});
            if(found==owner.playlist.upcoming.end())return;
            int from=int(found-owner.playlist.upcoming.begin())+owner.rowOffset();
            int insertion = getInsertionIndexForPosition(d.localPosition.x, d.localPosition.y);
            int to = insertion - (insertion > from ? 1 : 0);
            owner.moveRow(from, jlimit(owner.rowOffset(), owner.getNumRows() - 1, to));
        }
    private:
        StationPanel& owner;
    };
public:
    StationEngine engine;
    StationPanel() : queueView(*this) {
        setSize(1120, 960);
        lnf.setColour(ResizableWindow::backgroundColourId, Colour(0xff101820));
        lnf.setColour(TextButton::buttonColourId, Colour(0xff253543));
        lnf.setColour(TextButton::buttonOnColourId, Colour(0xff087f72));
        lnf.setColour(Slider::thumbColourId, Colour(0xff5ad5bb));
        lnf.setColour(Slider::trackColourId, Colour(0xff338e80)); setLookAndFeel(&lnf);
        label(title,"STATION DESK",27); label(subtitle,"Community radio / operator console",14);
        label(onAir,"LOCAL SELECTED",23); label(status,"Choose your music folder, then Start rotation.",14);
        button(audio,"Start audio",[this] { ensureAudio(); });
        button(settings,"Audio device…",[this] { showDeviceSettings(); });
        button(feedSettings,"Website feed…",[this] { showFeedSettings(); });
        button(local,"Start rotation",[this] {
            if (!ensureAudio()) return;
            engine.selectRemote(false);
            if (!engine.fileSource || engine.transport.hasStreamFinished()) nextTrack(); else engine.play();
            syncBroadcast(); publishFeed(); save();
        });
        local.setColour(TextButton::buttonColourId,Colour(0xff087f72));
        button(remote,"Play remote",[this] {
            if (ensureAudio()) { engine.selectRemote(true); syncBroadcast(); publishFeed(); save(); }
        });
        slider(crossfade,"Crossfade (seconds)",0,5,.1,1,[this](double v) {engine.fade=float(v);});
        label(localTitle,"LOCAL PLAYBACK · NEXT HOUR",16);
        label(nowPlaying,"Choose a Music folder to populate the queue",17); label(progress,"00:00 / 00:00",13);
        button(addFiles,"Add files…",[this] {chooseFiles();});
        button(playPause,"Play / pause",[this] {
            if (engine.remoteOn) {status.setText("Select local before changing playback.",dontSendNotification);return;}
            if (engine.playing) engine.pause();
            else if (ensureAudio()) {
                if (!engine.fileSource || engine.transport.hasStreamFinished()) nextTrack(); else engine.play();
            }
            syncBroadcast(); publishFeed(); save();
        });
        button(skip,"Next",[this] {if (!engine.remoteOn) nextTrack();});
        button(moveUp,"↑",[this] {int row=queueView.getSelectedRow();moveRow(row,row-1);});
        button(moveDown,"↓",[this] {int row=queueView.getSelectedRow();moveRow(row,row+1);});
        moveUp.setTooltip("Move selected upcoming track earlier"); moveDown.setTooltip("Move selected upcoming track later");
        button(remove,"Remove selected",[this] {
            int row=queueView.getSelectedRow()-rowOffset();
            if (row<0 || row>=int(playlist.upcoming.size())) return;
            playlist.remove(size_t(row)); refill(); save();
        });
        queueView.setModel(this);queueView.setRowHeight(38);addAndMakeVisible(queueView);
        queueView.setColour(ListBox::backgroundColourId,Colour(0xff17232d));
        queueView.setTooltip("Drag upcoming tracks to reorder. Your edits take precedence over rotation rules.");
        slider(localVolume,"Local level",0,1.5,.01,.8,[this](double v) {engine.localLevel=float(v);});
        label(remoteTitle,"REMOTE DJ / SONOBUS",16);
        editor(group,"Private group name");editor(password,"Group password (optional)");password.setPasswordCharacter('*');
        button(connect,"Join group",[this] {
            if(group.getText().trim().isEmpty()) {status.setText("Enter a SonoBus group name first.",dontSendNotification);return;}
            if(engine.receiver.isConnectedToServer())engine.receiver.disconnectFromServer();
            pendingJoin=true;connectionStart=Time::getMillisecondCounterHiRes();
            engine.receiver.connectToServer("aoo.sonobus.net",10998,"StationDesk","");status.setText("Connecting to SonoBus...",dontSendNotification);
        });
        button(disconnect,"Leave",[this] {pendingJoin=false;engine.receiver.disconnectFromServer();});
        label(connection,"Not connected",14);addAndMakeVisible(peers);peers.setTextWhenNothingSelected("Select the DJ to receive");
        peers.onChange=[this] {selectedPeer=peers.getText();syncBroadcast();publishFeed();};
        button(audition,"Headphones: program",[this] {
            engine.cueRemote=!engine.cueRemote;audition.setButtonText(engine.cueRemote?"Headphones: remote":"Headphones: program");
        });
        slider(remoteVolume,"Remote level",0,1.5,.01,.8,[this](double v) {engine.remoteLevel=float(v);});
        label(micTitle,"MICROPHONE & MONITOR",16);
        button(micToggle,"Mic OFF",[this] {engine.micOn=!engine.micOn;micToggle.setButtonText(engine.micOn?"Mic ON":"Mic OFF");micToggle.setToggleState(engine.micOn,dontSendNotification);});
        button(duck,"Duck program",[this] {engine.duck=!engine.duck;duck.setToggleState(engine.duck,dontSendNotification);});
        slider(micVolume,"Mic level",0,2,.01,.7,[this](double v) {engine.micLevel=float(v);});
        slider(cueVolume,"Headphone level",0,1,.01,.7,[this](double v) {engine.headphones=float(v);});
        for(auto* combo:{&programRoute,&cueRoute,&micRoute})addAndMakeVisible(combo);
        programRoute.setTextWhenNothingSelected("Broadcast output pair");cueRoute.setTextWhenNothingSelected("Headphone output pair");micRoute.setTextWhenNothingSelected("Microphone input");
        programRoute.onChange=[this] {engine.programPair=programRoute.getSelectedId()-1;save();};
        cueRoute.onChange=[this] {engine.cuePair=cueRoute.getSelectedId()-1;save();};
        micRoute.onChange=[this] {engine.micInput=micRoute.getSelectedId()-1;save();};
        label(routing,"Headphone audio requires a separate output pair.",13);
        label(automationTitle,"FOLDER ROTATION",16);
        for(int i=0;i<4;++i) {
            button(folderButtons[i],categoryNames[i]+" folder…",[this,i] {chooseFolder(i);});label(folderLabels[i],"No folder",12);
            if(i>0) {
                auto& rule=rules[i-1];addAndMakeVisible(rule.mode);
                rule.mode.addItem("Off",1);rule.mode.addItem("Every N music tracks",2);rule.mode.addItem("Every N minutes",3);
                rule.mode.setSelectedId(i==1?2:3);addAndMakeVisible(rule.amount);rule.amount.setRange(1,240,1);rule.amount.setValue(i==1?4:i==2?30:60);
                rule.amount.setSliderStyle(Slider::IncDecButtons);rule.amount.setTextBoxStyle(Slider::TextBoxLeft,false,45,24);
                rule.mode.onChange=[this] {rotationChanged();};rule.amount.onValueChange=[this] {rotationChanged();};
            }
        }
        label(automationNote,"Shuffled cycles · drag to reorder · quarter-hour markers · your edits take precedence",13);
        restore();
        auto urlOverride=SystemStats::getEnvironmentVariable("STATION_DESK_FEED_URL","");
        if(urlOverride.isNotEmpty())feedURL=urlOverride;
        auto token=SystemStats::getEnvironmentVariable("STATION_DESK_FEED_TOKEN","");
        publisher.configure(feedURL,token.isNotEmpty()?token:(feedURL.isNotEmpty()?StationFeedPublisher::credential():String()));
        syncRules();refill();
        if(!playlist.upcoming.empty())status.setText("Next hour is ready. Press Start rotation.",dontSendNotification);
        startTimerHz(10);
    }
    ~StationPanel() override {
        stopTimer();history.stop(Time::currentTimeMillis());broadcastKey.clear();publishFeed();save();
        queueView.setModel(nullptr);setLookAndFeel(nullptr);
    }
    void paint(Graphics& g) override {
        g.fillAll(Colour(0xff101820));g.setColour(Colour(0xff192631));
        g.fillRoundedRectangle(20,170,float(getWidth()/2-30),435,12);
        g.fillRoundedRectangle(float(getWidth()/2+10),170,float(getWidth()/2-30),435,12);
        g.fillRoundedRectangle(20,620,float(getWidth()-40),130,12);g.fillRoundedRectangle(20,765,float(getWidth()-40),165,12);
        auto meter=[&](int x,int y,int w,float peak) {
            g.setColour(Colour(0xff263b46));g.fillRoundedRectangle(float(x),float(y),float(w),7,3);
            float scaled=jlimit(0.f,1.f,(Decibels::gainToDecibels(peak,-60.f)+60)/60);
            g.setColour(peak>=1?Colour(0xfffa725c):Colour(0xff5ad5bb));g.fillRoundedRectangle(float(x),float(y),float(w)*scaled,7,3);
        };
        meter(24,148,getWidth()-48,engine.programPeak);meter(getWidth()/2+26,420,getWidth()/2-62,engine.remotePeak);
    }
    void resized() override {
        int w=getWidth(),half=w/2,col=half-60;
        title.setBounds(24,18,300,36);subtitle.setBounds(24,55,400,24);
        feedSettings.setBounds(w-420,26,135,32);audio.setBounds(w-275,26,115,32);settings.setBounds(w-150,26,125,32);
        onAir.setBounds(24,95,300,35);local.setBounds(330,96,150,36);remote.setBounds(490,96,130,36);
        crossfade.setBounds(w-260,108,230,32);status.setBounds(24,935,w-48,22);
        localTitle.setBounds(36,180,col,25);nowPlaying.setBounds(36,211,col,30);progress.setBounds(36,244,col,22);
        addFiles.setBounds(36,279,100,30);playPause.setBounds(144,279,105,30);skip.setBounds(257,279,55,30);
        moveUp.setBounds(320,279,28,30);moveDown.setBounds(356,279,28,30);remove.setBounds(392,279,half-428,30);
        queueView.setBounds(36,320,col+8,220);localVolume.setBounds(36,558,col,30);
        int x=half+26;remoteTitle.setBounds(x,180,col,25);group.setBounds(x,219,col/2-5,30);password.setBounds(x+col/2+5,219,col/2,30);
        connect.setBounds(x,261,120,30);disconnect.setBounds(x+130,261,90,30);connection.setBounds(x,302,col,25);
        peers.setBounds(x,336,col,30);audition.setBounds(x,380,col,30);remoteVolume.setBounds(x,558,col,30);
        micTitle.setBounds(36,627,235,24);micToggle.setBounds(36,665,95,30);duck.setBounds(141,665,125,30);
        micVolume.setBounds(280,664,230,30);cueVolume.setBounds(530,664,230,30);micRoute.setBounds(780,665,w-816,30);
        programRoute.setBounds(36,708,240,27);cueRoute.setBounds(288,708,240,27);routing.setBounds(540,708,w-576,27);
        automationTitle.setBounds(36,772,240,24);int cell=(w-72)/4;
        for(int i=0;i<4;++i) {int fx=36+cell*i;folderButtons[i].setBounds(fx,805,cell-12,28);folderLabels[i].setBounds(fx,835,cell-12,20);
            if(i>0) {rules[i-1].mode.setBounds(fx,862,cell-88,25);rules[i-1].amount.setBounds(fx+cell-83,862,71,25);}}
        automationNote.setBounds(36,898,w-72,22);
    }
private:
    struct Cadence {ComboBox mode;Slider amount;};
    std::array<Cadence,3> rules;
    std::array<File,4> folders;
    std::array<TextButton,4> folderButtons;
    std::array<Label,4> folderLabels;
    const std::array<String,4> categoryNames {"Music","Promos","Journalism","Station IDs"};
    station::Playlist playlist;
    station::History history;
    StationFeedPublisher publisher;
    String selectedPeer,peerSignature,deviceSignature,feedURL,broadcastKey;
    bool pendingJoin=false,restoring=true;
    double connectionStart=0;
    int ticks=0;
    LookAndFeel_V4 lnf;
    OwnedArray<Label> sliderLabels;
    Label title,subtitle,onAir,status,localTitle,nowPlaying,progress,remoteTitle,connection,micTitle,routing,automationTitle,automationNote;
    TextButton audio,settings,feedSettings,local,remote,addFiles,playPause,skip,moveUp,moveDown,remove,connect,disconnect,audition,micToggle,duck;
    Slider crossfade,localVolume,remoteVolume,micVolume,cueVolume;
    TextEditor group,password;
    ComboBox peers,programRoute,cueRoute,micRoute;
    QueueList queueView;
    std::unique_ptr<FileChooser> chooser;
    void label(Label& l,const String& text,float size) {addAndMakeVisible(l);l.setText(text,dontSendNotification);l.setFont(Font(size));l.setColour(Label::textColourId,Colour(0xffe1ecef));}
    void label(Label& l,const char* text,float size) {label(l,String::fromUTF8(text),size);}
    void button(TextButton& b,const String& text,std::function<void()> f) {addAndMakeVisible(b);b.setButtonText(text);b.onClick=std::move(f);}
    void button(TextButton& b,const char* text,std::function<void()> f) {button(b,String::fromUTF8(text),std::move(f));}
    void editor(TextEditor& e,const String& hint) {addAndMakeVisible(e);e.setTextToShowWhenEmpty(hint,Colours::grey);}
    void slider(Slider& s,const String& name,double min,double max,double step,double value,std::function<void(double)> f) {
        addAndMakeVisible(s);s.setName(name);s.setTooltip(name);s.setRange(min,max,step);s.setValue(value);
        s.setSliderStyle(Slider::LinearHorizontal);s.setTextBoxStyle(Slider::TextBoxRight,false,62,25);
        s.onValueChange=[this,&s,f] {f(s.getValue());save();};auto* caption=sliderLabels.add(new Label());label(*caption,name,12);caption->attachToComponent(&s,false);
    }
    double remaining() const {return playlist.current?std::max(0.0,playlist.current->seconds-engine.position.load()):0;}
    int rowOffset() const {return playlist.current?1:0;}
    int getNumRows() override {return int(playlist.upcoming.size())+rowOffset();}
    const station::Track* rowTrack(int row) const {
        if(row<0)return nullptr;
        if(playlist.current) {if(row==0)return &*playlist.current;--row;}
        return row<int(playlist.upcoming.size())?&playlist.upcoming[size_t(row)]:nullptr;
    }
    std::pair<int64,int64> rowTimes(int row,int64 now) const {
        int64 start=now;
        if(playlist.current) {
            if(row==0) {start-=int64(engine.position.load()*1000);return {start,start+int64(playlist.current->seconds*1000)};}
            start+=int64(remaining()*1000);--row;
        }
        for(int i=0;i<row && i<int(playlist.upcoming.size());++i)start+=int64(playlist.upcoming[size_t(i)].seconds*1000);
        if(row<0||row>=int(playlist.upcoming.size()))return {start,start};
        return {start,start+int64(playlist.upcoming[size_t(row)].seconds*1000)};
    }
    static String clockText(int64 ms) {return Time(ms).formatted("%I:%M%p").toLowerCase().trimCharactersAtStart("0");}
    void paintListBoxItem(int row,Graphics& g,int width,int height,bool selected) override {
        auto* track=rowTrack(row);if(!track)return;
        bool current=playlist.current&&row==0;
        if(selected||current) {g.setColour(Colour(current?0xff214139:0xff285349));g.fillAll();}
        auto times=rowTimes(row,Time::currentTimeMillis());auto marker=station::Playlist::firstMarker(times.first,times.second);
        g.setFont(13.f);g.setColour(Colour(0xff5ad5bb));
        if(marker)g.drawText(clockText(*marker),6,0,78,height,Justification::centredLeft);
        g.setColour(Colours::white);g.setFont(14.f);g.drawText(String(track->title),88,2,width-94,18,Justification::centredLeft);
        String detail=String::fromUTF8(current?(engine.remoteOn?"HELD · ":engine.playing?"NOW · ":"PAUSED · "):"")+categoryNames[size_t(track->category)]+" · "+timeText(track->seconds);
        if(!track->artist.empty())detail+=" · "+String(track->artist);
        g.setColour(Colour(0xffa7bbc6));g.setFont(11.f);g.drawText(detail,88,20,width-94,15,Justification::centredLeft);
    }
    var getDragSourceDescription(const SparseSet<int>& rows) override {
        auto* track=rows.size()==1?rowTrack(rows[0]):nullptr;
        return track && rows[0]>=rowOffset()?var("station-slot:"+String(int64(track->slot))):var();
    }
    bool mayDragToExternalWindows() const override {return false;}
    String getTooltipForRow(int row) override {
        auto* t=rowTrack(row);if(!t)return {};
        auto times=rowTimes(row,Time::currentTimeMillis());
        return clockText(times.first)+" – "+clockText(times.second)+" · "+String(t->title)+"\n"+String(t->path);
    }
    void moveRow(int from,int to) {
        if(from<rowOffset()||to<rowOffset())return;
        if(playlist.move(size_t(from-rowOffset()),size_t(to-rowOffset()))) {queueView.updateContent();queueView.selectRow(to);save();queueView.repaint();}
    }
    void listBoxItemDoubleClicked(int row,const MouseEvent&) override {
        if(engine.remoteOn||row<rowOffset()||row>=getNumRows())return;
        int index=row-rowOffset();if(index>0)playlist.move(size_t(index),0);
        nextTrack();
    }
    String wildcard() const {return "*.wav;*.aif;*.aiff;*.mp3;*.flac;*.ogg;*.m4a";}
    std::optional<station::Track> readTrack(const File& file,int category) {
        std::unique_ptr<AudioFormatReader> reader(engine.formats.createReaderFor(file));
        if(!reader||reader->sampleRate<=0||reader->lengthInSamples<=0)return {};
        auto name=reader->metadataValues.getValue("title",reader->metadataValues.getValue("id3title",file.getFileNameWithoutExtension()));
        if(name.trim().isEmpty())name=file.getFileNameWithoutExtension();
        auto artist=reader->metadataValues.getValue("artist",reader->metadataValues.getValue("id3artist",""));
        return station::Track{file.getFullPathName().toStdString(),name.toStdString(),artist.toStdString(),double(reader->lengthInSamples)/reader->sampleRate,category};
    }
    void chooseFiles() {
        chooser.reset(new FileChooser("Add audio files",File(),wildcard()));
        chooser->launchAsync(FileBrowserComponent::openMode|FileBrowserComponent::canSelectFiles|FileBrowserComponent::canSelectMultipleItems,
            [safe=Component::SafePointer<StationPanel>(this)](const FileChooser& fc) {if(!safe)return;for(auto f:fc.getResults())if(auto t=safe->readTrack(f,0))safe->playlist.add(*t);safe->refill();safe->save();});
    }
    void chooseFolder(int i) {
        chooser.reset(new FileChooser("Choose "+categoryNames[i]+" folder",folders[i]));
        chooser->launchAsync(FileBrowserComponent::openMode|FileBrowserComponent::canSelectDirectories,
            [safe=Component::SafePointer<StationPanel>(this),i](const FileChooser& fc) {if(!safe||!fc.getResult().isDirectory())return;safe->folders[i]=fc.getResult();safe->scan(i);safe->rotationChanged();});
    }
    void scan(int i) {
        Array<File> files;if(folders[i].isDirectory())folders[i].findChildFiles(files,File::findFiles,true,wildcard());
        std::vector<station::Track> tracks;
        for(const auto& f:files)if(auto t=readTrack(f,i))tracks.push_back(*t);
        int unreadable=files.size()-int(tracks.size());playlist.setLibrary(i,std::move(tracks));
        folderLabels[i].setText(String(playlist.libraries[i].size())+" files · "+folders[i].getFileName()+(unreadable?String::fromUTF8(" · ")+String(unreadable)+" unreadable":""),dontSendNotification);
        folderLabels[i].setTooltip(folders[i].getFullPathName());
    }
    void syncRules() {for(int i=0;i<3;++i)playlist.rules[i]={rules[i].mode.getSelectedId(),jmax(1,int(rules[i].amount.getValue()))};}
    void refill() {
        playlist.fill(remaining());queueView.updateContent();queueView.repaint();
        if(!playlist.current)nowPlaying.setText(playlist.upcoming.empty()?"Choose a Music folder to populate the queue":"Ready: "+String(playlist.upcoming.front().title),dontSendNotification);
    }
    void rotationChanged() {if(restoring)return;syncRules();playlist.rebuild(remaining());refill();save();}
    bool ensureAudio() {
        auto error=engine.startAudio();if(error.isNotEmpty()){status.setText(error,dontSendNotification);return false;}
        updateRoutes();status.setText("Audio running. "+publisher.getStatus(),dontSendNotification);return true;
    }
    void rejectCurrent() {
        if(!playlist.current)return;auto path=playlist.current->path;
        for(int cat=0;cat<4;++cat) {
            auto removePath=[&](auto& tracks) {tracks.erase(std::remove_if(tracks.begin(),tracks.end(),[&](const station::Track& t){return t.path==path;}),tracks.end());};
            removePath(playlist.libraries[cat]);removePath(playlist.bags[cat]);
        }
        playlist.upcoming.erase(std::remove_if(playlist.upcoming.begin(),playlist.upcoming.end(),[&](const station::Track& t){return t.path==path;}),playlist.upcoming.end());
        playlist.protectedPrefix=std::min(playlist.protectedPrefix,playlist.upcoming.size());playlist.current.reset();
    }
    void nextTrack() {
        if(engine.remoteOn||!ensureAudio())return;
        history.stop(Time::currentTimeMillis());broadcastKey.clear();
        double elapsed=engine.position.load();
        while(playlist.advance(elapsed)) {
            if(engine.load(File(String(playlist.current->path)))) {
                engine.play();nowPlaying.setText(String(playlist.current->title),dontSendNotification);
                refill();syncBroadcast();publishFeed();save();return;
            }
            rejectCurrent();elapsed=0;
        }
        engine.pause();refill();syncBroadcast();publishFeed();save();
        status.setText("No playable local tracks. Choose a Music folder or add files.",dontSendNotification);
    }
    void syncBroadcast() {
        auto now=Time::currentTimeMillis();String key;
        if(engine.audioRunning) {
            if(engine.remoteOn)key="remote:"+selectedPeer;
            else if(engine.playing&&playlist.current)key="local:"+String(playlist.current->path);
        }
        if(key==broadcastKey)return;
        broadcastKey=key;history.stop(now);
        if(key.isEmpty())return;
        station::PlayEvent event;event.id=Uuid().toString().toStdString();
        if(engine.remoteOn) {event.title=("Remote DJ"+(selectedPeer.isNotEmpty()?String::fromUTF8(" — ")+selectedPeer:String())).toStdString();event.kind="remote";}
        else {event.title=playlist.current->title;event.artist=playlist.current->artist;event.kind=categoryNames[size_t(playlist.current->category)].toLowerCase().toStdString();}
        history.start(event,now);
    }
    static var eventJSON(const station::PlayEvent& event) {
        auto* o=new DynamicObject;o->setProperty("id",String(event.id));o->setProperty("title",String(event.title));o->setProperty("artist",String(event.artist));o->setProperty("kind",String(event.kind));
        o->setProperty("started_at",int64(event.started));if(event.ended)o->setProperty("ended_at",int64(event.ended));return var(o);
    }
    var feedSnapshot() {
        auto now=Time::currentTimeMillis();history.prune(now);auto* o=new DynamicObject;
        o->setProperty("version",1);o->setProperty("updated_at",int64(now));o->setProperty("state",history.live?(engine.remoteOn?"remote":"local"):"stopped");
        o->setProperty("now",history.live?eventJSON(*history.live):var());Array<var> events;
        for(const auto& e:history.played)events.add(eventJSON(e));o->setProperty("history",events);return var(o);
    }
    void publishFeed() {
        if(restoring)return;auto json=JSON::toString(feedSnapshot());
        auto file=preferences().getSiblingFile("NowPlaying.json");file.getParentDirectory().createDirectory();file.replaceWithText(json);publisher.submit(json);
    }
    void showFeedSettings() {
        auto* dialog=new AlertWindow("Website feed","Publish to your Cloudflare HTTPS endpoint. The publisher key is stored in macOS Keychain.",AlertWindow::NoIcon);
        dialog->addTextEditor("url",feedURL,"Publish URL (ending /publish)");dialog->addTextEditor("token","","Publisher key (blank keeps the saved key)",true);
        dialog->addButton("Save",1,KeyPress(KeyPress::returnKey));dialog->addButton("Cancel",0,KeyPress(KeyPress::escapeKey));
        dialog->enterModalState(true,ModalCallbackFunction::create([safe=Component::SafePointer<StationPanel>(this),dialog](int result) {
            if(safe&&result==1) {
                auto url=dialog->getTextEditorContents("url").trim();auto token=dialog->getTextEditorContents("token");
                if(url.isNotEmpty()&&!url.startsWithIgnoreCase("https://"))safe->status.setText("Website feed requires an HTTPS URL.",dontSendNotification);
                else if(token.containsAnyOf("\r\n"))safe->status.setText("Publisher key must be one line.",dontSendNotification);
                else {
                    auto error=token.isNotEmpty()?StationFeedPublisher::credential(token,true):String();
                    if(error.isNotEmpty())safe->status.setText(error,dontSendNotification);
                    else {safe->feedURL=url;safe->publisher.configure(url,token.isNotEmpty()?token:StationFeedPublisher::credential());safe->publishFeed();safe->save();safe->status.setText(url.isEmpty()?"Website feed off":"Website feed configured.",dontSendNotification);}
                }
            }
            delete dialog;
        }),false);
    }
    void showDeviceSettings() {
        auto* content=new AudioDeviceSelectorComponent(engine.devices,0,32,2,32,false,false,true,false);content->setSize(520,520);
        DialogWindow::LaunchOptions o;o.content.setOwned(content);o.dialogTitle="Station audio device";o.dialogBackgroundColour=Colour(0xff192631);o.escapeKeyTriggersCloseButton=true;o.useNativeTitleBar=true;o.resizable=false;o.launchAsync();
    }
    void updateRoutes() {
        auto* d=engine.devices.getCurrentAudioDevice();if(!d)return;int p=engine.programPair,c=engine.cuePair,m=engine.micInput;
        programRoute.clear(dontSendNotification);cueRoute.clear(dontSendNotification);micRoute.clear(dontSendNotification);
        auto outs=d->getOutputChannelNames(),ins=d->getInputChannelNames();auto enabledOut=d->getActiveOutputChannels(),enabledIn=d->getActiveInputChannels();
        for(int i=0;i+1<outs.size();i+=2)if(enabledOut[i]&&enabledOut[i+1]) {String name=String(i+1)+"–"+String(i+2)+": "+outs[i]+" / "+outs[i+1];programRoute.addItem("FM "+name,i+1);cueRoute.addItem("Phones "+name,i+1);}
        for(int i=0;i<ins.size();++i)if(enabledIn[i])micRoute.addItem(ins[i],i+1);
        programRoute.setSelectedId(p+1,dontSendNotification);cueRoute.setSelectedId(c+1,dontSendNotification);micRoute.setSelectedId(m+1,dontSendNotification);
    }
    static String timeText(double seconds) {int n=int(std::max(0.0,seconds));return String(n/60).paddedLeft('0',2)+":"+String(n%60).paddedLeft('0',2);}
    void timerCallback() override {
        if(pendingJoin&&engine.receiver.isConnectedToServer()) {engine.receiver.joinServerGroup(group.getText().trim(),password.getText(),false);pendingJoin=false;}
        else if(pendingJoin&&Time::getMillisecondCounterHiRes()-connectionStart>15000) {pendingJoin=false;status.setText("Connection timed out. Check internet access and try again.",dontSendNotification);}
        connection.setText(engine.receiver.getCurrentJoinedGroup().isNotEmpty()?"In group: "+engine.receiver.getCurrentJoinedGroup():pendingJoin?"Connecting...":"Not in a group",dontSendNotification);
        String signature;int count=engine.receiver.getNumberRemotePeers();for(int i=0;i<count;++i)signature+=engine.receiver.getRemotePeerUserName(i)+"\n";
        if(signature!=peerSignature) {peerSignature=signature;peers.clear(dontSendNotification);for(int i=0;i<count;++i)peers.addItem(engine.receiver.getRemotePeerUserName(i),i+1);for(int i=0;i<count;++i)if(engine.receiver.getRemotePeerUserName(i)==selectedPeer)peers.setSelectedId(i+1,dontSendNotification);}
        for(int i=0;i<count;++i)engine.receiver.setRemotePeerLevelGain(i,engine.receiver.getRemotePeerUserName(i)==selectedPeer&&selectedPeer.isNotEmpty()?1.f:0.f);
        if(!engine.remoteOn&&engine.ended.exchange(false))nextTrack();
        syncBroadcast();
        if(++ticks%10==0) {refill();queueView.repaint();}
        if(ticks%150==0) {publishFeed();save();if(engine.audioRunning)status.setText("Audio running. "+publisher.getStatus(),dontSendNotification);}
        audio.setButtonText(engine.audioRunning?"Audio running":"Start audio");
        onAir.setText(!engine.audioRunning?"AUDIO STOPPED":engine.remoteOn?(engine.fadePosition<1?"FADING TO REMOTE":"REMOTE SELECTED"):(engine.fadePosition>0?"FADING TO LOCAL":"LOCAL SELECTED"),dontSendNotification);
        local.setToggleState(!engine.remoteOn,dontSendNotification);remote.setToggleState(engine.remoteOn,dontSendNotification);
        progress.setText(timeText(engine.position)+" / "+timeText(engine.duration)+(engine.remoteOn?" · held for remote":""),dontSendNotification);
        if(auto* d=engine.devices.getCurrentAudioDevice()) {
            String sig=d->getName()+d->getActiveOutputChannels().toString(2)+d->getActiveInputChannels().toString(2);if(sig!=deviceSignature){deviceSignature=sig;updateRoutes();}
            bool overlap=std::abs(engine.programPair.load()-engine.cuePair.load())<2;auto active=d->getActiveOutputChannels();int c=engine.cuePair,p=engine.programPair;
            routing.setText(!active[p]||!active[p+1]?"Broadcast pair unavailable: choose an active pair.":overlap?"Headphones disabled: choose a separate pair.":!active[c]||!active[c+1]?"Headphone pair unavailable on this device.":"Separate broadcast and headphone outputs ready.",dontSendNotification);
        }
        repaint();
    }
    File preferences() {return StationEngine::dataDirectory().getChildFile("StationDesk.json");}
    static var trackJSON(const station::Track& t) {
        auto* o=new DynamicObject;o->setProperty("path",String(t.path));o->setProperty("category",t.category);o->setProperty("slot",int64(t.slot));return var(o);
    }
    std::optional<station::Track> restoreTrack(const var& v) {
        String path=v.isString()?v.toString():v["path"].toString();int cat=v.isString()?0:jlimit(0,3,int(v["category"]));
        auto track=path.isNotEmpty()?readTrack(File(path),cat):std::optional<station::Track>();
        if(track){track->slot=uint64_t(jmax(int64(0),int64(v["slot"])));if(!track->slot)track->slot=playlist.nextSlot++;playlist.nextSlot=std::max(playlist.nextSlot,track->slot+1);}
        return track;
    }
    void save() {
        if(restoring)return;auto* o=new DynamicObject;var root(o);Array<var> paths,files,cadences,bags,lasts,music,seconds;
        for(auto& f:folders)paths.add(f.getFullPathName());for(auto& t:playlist.upcoming)files.add(trackJSON(t));
        for(int i=0;i<3;++i){auto* r=new DynamicObject;r->setProperty("mode",rules[i].mode.getSelectedId());r->setProperty("amount",rules[i].amount.getValue());cadences.add(var(r));music.add(playlist.consumed.music[i]);seconds.add(playlist.consumed.seconds[i]);}
        for(int cat=0;cat<4;++cat){Array<var> items;for(auto& t:playlist.bags[cat])items.add(trackJSON(t));bags.add(items);lasts.add(String(playlist.lastChosen[cat]));}
        o->setProperty("version",2);o->setProperty("folders",paths);o->setProperty("queue",files);o->setProperty("cadences",cadences);o->setProperty("bags",bags);o->setProperty("lastChosen",lasts);
        o->setProperty("protectedPrefix",int(playlist.protectedPrefix));o->setProperty("musicCounts",music);o->setProperty("elapsedSeconds",seconds);
        if(playlist.current){o->setProperty("current",trackJSON(*playlist.current));o->setProperty("position",engine.position.load());}
        o->setProperty("feedURL",feedURL);o->setProperty("feed",feedSnapshot());
        o->setProperty("fade",double(engine.fade));o->setProperty("local",double(engine.localLevel));o->setProperty("remote",double(engine.remoteLevel));o->setProperty("mic",double(engine.micLevel));o->setProperty("phones",double(engine.headphones));
        o->setProperty("programPair",int(engine.programPair));o->setProperty("cuePair",int(engine.cuePair));o->setProperty("micInput",int(engine.micInput));
        auto f=preferences();f.getParentDirectory().createDirectory();f.replaceWithText(JSON::toString(root));if(auto xml=engine.devices.createStateXml())f.getSiblingFile("AudioDevice.xml").replaceWithText(xml->toString());
    }
    void restore() {
        auto data=JSON::parse(preferences());
        if(auto* paths=data["folders"].getArray())for(int i=0;i<jmin(4,paths->size());++i)if((*paths)[i].toString().isNotEmpty()){folders[i]=File((*paths)[i]);scan(i);}
        if(auto* files=data["queue"].getArray())for(auto& f:*files)if(auto t=restoreTrack(f))playlist.upcoming.push_back(*t);
        playlist.protectedPrefix=jlimit(0,int(playlist.upcoming.size()),data.hasProperty("protectedPrefix")?int(data["protectedPrefix"]):int(playlist.upcoming.size()));
        if(auto* cs=data["cadences"].getArray())for(int i=0;i<jmin(3,cs->size());++i){rules[i].mode.setSelectedId(jlimit(1,3,int((*cs)[i]["mode"])));rules[i].amount.setValue(jmax(1.0,double((*cs)[i]["amount"])));}
        if(auto* counts=data["musicCounts"].getArray())for(int i=0;i<jmin(3,counts->size());++i)playlist.consumed.music[i]=jmax(0,int((*counts)[i]));
        if(auto* elapsed=data["elapsedSeconds"].getArray())for(int i=0;i<jmin(3,elapsed->size());++i)playlist.consumed.seconds[i]=jmax(0.0,double((*elapsed)[i]));
        if(auto* bags=data["bags"].getArray())for(int cat=0;cat<jmin(4,bags->size());++cat)if(auto* items=(*bags)[cat].getArray())for(auto& item:*items)if(auto t=restoreTrack(item)) {
            if(std::any_of(playlist.libraries[cat].begin(),playlist.libraries[cat].end(),[&](const station::Track& candidate){return candidate.path==t->path;}))playlist.bags[cat].push_back(*t);
        }
        if(auto* lasts=data["lastChosen"].getArray())for(int cat=0;cat<jmin(4,lasts->size());++cat)playlist.lastChosen[cat]=(*lasts)[cat].toString().toStdString();
        if(data.hasProperty("current"))if(auto t=restoreTrack(data["current"]))if(engine.load(File(String(t->path)))) {
            playlist.current=*t;double pos=jlimit(0.0,jmax(0.0,t->seconds-.001),double(data["position"]));engine.transport.setPosition(pos);engine.position=pos;nowPlaying.setText(String(t->title),dontSendNotification);
        }
        auto restoreSlider=[&](const char* key,Slider& s){if(data.hasProperty(key))s.setValue(double(data[key]),sendNotificationSync);};
        restoreSlider("fade",crossfade);restoreSlider("local",localVolume);restoreSlider("remote",remoteVolume);restoreSlider("mic",micVolume);restoreSlider("phones",cueVolume);
        if(data.hasProperty("programPair"))engine.programPair=jlimit(0,30,int(data["programPair"]));
        if(data.hasProperty("cuePair"))engine.cuePair=jlimit(0,30,int(data["cuePair"]));if(data.hasProperty("micInput"))engine.micInput=jlimit(0,31,int(data["micInput"]));
        feedURL=data["feedURL"].toString();
        auto parseEvent=[](const var& v) {station::PlayEvent e;e.id=v["id"].toString().toStdString();e.title=v["title"].toString().toStdString();e.artist=v["artist"].toString().toStdString();e.kind=v["kind"].toString().toStdString();e.started=int64(v["started_at"]);e.ended=int64(v["ended_at"]);return e;};
        if(auto* events=data["feed"]["history"].getArray())for(auto& v:*events)history.played.push_back(parseEvent(v));
        if(data["feed"]["now"].isObject()){auto e=parseEvent(data["feed"]["now"]);e.ended=int64(data["feed"]["updated_at"]);history.played.push_back(e);}
        history.prune(Time::currentTimeMillis());restoring=false;queueView.updateContent();
    }
};

#if STATION_DESK_TESTS
#include "../tests/StationFeatureTests.h"
#endif

class StationApplication : public JUCEApplication {
public:
    const String getApplicationName() override {return "Station Desk";}
    const String getApplicationVersion() override {return "0.2.0";}
    bool moreThanOneInstanceAllowed() override {
#if STATION_DESK_TESTS
        // Isolated test processes must not forward their command line to a running operator app.
        if (SystemStats::getEnvironmentVariable("STATION_DESK_DATA_DIR", "").isNotEmpty()
            && StationEngine::dataDirectory().getChildFile("test-only.marker").existsAsFile()) return true;
#endif
        return false;
    }
    void initialise(const String& command) override {
#if JUCE_MAC || JUCE_LINUX
        auto stationZone=SystemStats::getEnvironmentVariable("STATION_DESK_TIMEZONE","America/Los_Angeles");
        setenv("TZ",stationZone.toRawUTF8(),1);tzset();
#endif
        PluginHostType::jucePlugInClientCurrentWrapperType=AudioProcessor::wrapperType_Standalone;
        if(command.startsWith("--self-test ")) {runSelfTest(File(command.fromFirstOccurrenceOf("--self-test ",false,false).unquoted()));quit();return;}
#if STATION_DESK_TESTS
        if(command.startsWith("--feature-tests ")) {
            try { setApplicationReturnValue(StationFeatureTests::run(File(command.fromFirstOccurrenceOf("--feature-tests ",false,false).unquoted()))); }
            catch (const std::exception& error) { std::cerr << "TEST ERROR " << error.what() << std::endl; setApplicationReturnValue(2); }
            quit();return;
        }
#endif
        window.reset(new Window);
#if STATION_DESK_TESTS
        if(command.startsWith("--ui-preview ")) {
            File output(command.fromFirstOccurrenceOf("--ui-preview ",false,false).unquoted());
            Timer::callAfterDelay(500,[this,output] {
                auto* panel=window->getContentComponent();
                auto image=panel->createComponentSnapshot(panel->getLocalBounds());
                FileOutputStream stream(output);stream.setPosition(0);stream.truncate();PNGImageFormat png;
                bool ok=stream.openedOk()&&png.writeImageToStream(image,stream);
                std::cout<<(ok?"PASS":"FAIL")<<" native UI preview"<<std::endl;
                setApplicationReturnValue(ok?0:1);window.reset();quit();
            });
            return;
        }
#endif
        if(command == "--smoke-test") {
            std::cout << "PASS station window constructed" << std::endl;
            Timer::callAfterDelay(1500, [this] {window.reset();std::cout << "PASS station window closed cleanly" << std::endl;quit();});
        }
    }
    void runSelfTest(const File& tone) {
        int failures=0;
        auto check=[&](bool condition,const char* name) {std::cout<<(condition?"PASS ":"FAIL ")<<name<<std::endl;if(!condition)++failures;};
        {
            auto ownedEngine = std::make_unique<StationEngine>();
            auto& e = *ownedEngine; e.prepare(48000,256);
            check(e.load(tone),"WAV file reader opens"); e.play();
            float mic[256],out[4][256];for(auto& v:mic)v=.2f;
            const float* inputs[]={mic};float* outputs[]={out[0],out[1],out[2],out[3]};
            auto render=[&] {e.audioDeviceIOCallbackWithContext(inputs,1,outputs,4,256,AudioIODeviceCallbackContext{}); Thread::sleep(6);};
            Thread::sleep(80);for(int i=0;i<20;++i)render();
            check(e.position>0,"Local file advances");
            check(e.programPeak>0.01,"Local audio reaches program");
            e.cueRemote=true;render();
            check(std::abs(out[2][100])<.00001f && std::abs(out[3][100])<.00001f,"Remote audition excludes local program");
            e.selectRemote(true);for(int i=0;i<195;++i)render();
            double pos=e.position;for(int i=0;i<20;++i)render();
            check(std::abs(pos-e.position)<.00001,"Local playback position holds during remote");
            check(e.programPeak<.00001,"Silent remote does not leak local program");
            e.micOn=true;for(int i=0;i<10;++i)render();
            check(std::abs(out[0][100]-.14f)<.002f,"Microphone reaches program over remote");
            e.cuePair=0;render();
            check(std::abs(out[0][100]-.14f)<.002f,"Overlapping cue cannot overwrite broadcast");
            e.micOn=false;e.selectRemote(false);for(int i=0;i<200;++i)render();
            check(e.position>pos,"Returning local resumes playback");
            check(e.programPeak>0.01,"Local returns after crossfade");
            e.transport.stop();
            e.transport.setSource(nullptr);
            check(!e.transport.hasStreamFinished(),"Detached source is not reported as finished");
            // Reproduce the callback running during Next's source replacement window.
            e.playing = true; render();
            check(!e.ended && e.programPeak < .00001f,"Callback tolerates a detached track source");
            e.playing = false;
            std::atomic<bool> keepRendering { true };
            std::atomic<int> renderedBlocks { 0 };
            std::thread audioThread([&] {
                while (keepRendering) { render(); ++renderedBlocks; }
            });
            bool allLoaded = true;
            for (int i = 0; i < 40; ++i) {
                if (!e.load(tone)) { allLoaded = false; break; }
                e.play(); Thread::sleep(12);
            }
            e.pause(); keepRendering = false; audioThread.join();
            check(allLoaded && renderedBlocks > 40,"Forty track replacements during concurrent audio processing");
            e.play(); for(int i=0;i<20;++i)render();
            check(e.position > 0 && e.programPeak > .01f,"Playback continues after repeated Next transitions");
            e.transport.stop();e.audioDeviceStopped();
        }
        setApplicationReturnValue(failures?1:0);
    }
    void shutdown() override {window.reset();}
    void systemRequestedQuit() override {quit();}
    class Window : public DocumentWindow {
    public:
        Window():DocumentWindow("Station Desk 0.2.0",Colour(0xff101820),DocumentWindow::allButtons) {
            setUsingNativeTitleBar(true);setContentOwned(new StationPanel,true);setResizable(false,false);centreWithSize(getWidth(),getHeight());setVisible(true);
        }
        void closeButtonPressed() override {JUCEApplication::getInstance()->systemRequestedQuit();}
    };
    std::unique_ptr<Window> window;
};
START_JUCE_APPLICATION(StationApplication)
