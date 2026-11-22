#pragma once
#include "Library.h"
#include "YouTube.h"
#include "Contacts.h"
#include "Updater.h"

namespace sd
{
/** The beat being prepared on the Upload page. Kept by the plugin so it survives closing the window. */
struct Draft
{
    Beat beat;
    juce::File audio, cover;            // files chosen or captured for it (already in the library for a saved beat)
    juce::String privacy { "public" };
    bool schedule = false;
    juce::Time publishAt;
    bool toYouTube = true, toBeatStars = false, toShort = false;
};

/** Does the slow work in the background: makes the video, uploads it, and packs the files for BeatStars. */
class Publisher : public juce::ChangeBroadcaster, private juce::Thread
{
public:
    struct Job
    {
        juce::String beatId;
        bool youtube = false, beatstarsPack = false, clip = false;
        YouTubeRequest request, shortRequest;       // the Short's description gets the link to the full video added
        media::Frame frame, tallFrame;
    };

    Publisher (Library&, Settings&, GoogleAuth&);
    ~Publisher() override;

    bool start (Job);                   // false when another upload is still running
    void cancel();
    bool running() const                { return isThreadRunning(); }

    juce::String stage() const;
    double progress() const             { return fraction.load(); }
    juce::String lastError() const;
    juce::String lastUrl() const;
    juce::File lastPack() const;
    juce::String lastBeatId() const;

    static juce::File packFolder (const Library&, const Beat&);
    static juce::File clipFile (const Library&, const Beat&);

private:
    void run() override;
    void setStage (const juce::String&, double);
    bool makePack (const Beat&, juce::String& error);

    Library& library;
    Settings& settings;
    GoogleAuth& google;
    Job job;
    Beat beat;
    mutable juce::CriticalSection lock;
    juce::String stageText, error, url, beatId;
    juce::File pack;
    std::atomic<double> fraction { 0.0 };
};

/** Everything shared by all instances of the plugin in one host. */
struct Services
{
    Settings settings;
    Library library { Settings::libraryDir() };
    Contacts contacts { Settings::appDataDir().getChildFile ("contacts.json") };
    GoogleAuth google { settings };
    Publisher publisher { library, settings, google };
    Updater updater { settings };
};
}
