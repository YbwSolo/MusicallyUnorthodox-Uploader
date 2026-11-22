#include "Publisher.h"
#include "Texts.h"

namespace sd
{
Publisher::Publisher (Library& l, Settings& s, GoogleAuth& g) : juce::Thread ("MU Uploader publish"), library (l), settings (s), google (g) {}
Publisher::~Publisher() { cancel(); }

juce::String Publisher::stage() const       { const juce::ScopedLock sl (lock); return stageText; }
juce::String Publisher::lastError() const   { const juce::ScopedLock sl (lock); return error; }
juce::String Publisher::lastUrl() const     { const juce::ScopedLock sl (lock); return url; }
juce::File Publisher::lastPack() const      { const juce::ScopedLock sl (lock); return pack; }
juce::String Publisher::lastBeatId() const  { const juce::ScopedLock sl (lock); return beatId; }

void Publisher::setStage (const juce::String& text, double f)
{
    { const juce::ScopedLock sl (lock); stageText = text; }
    fraction.store (f);
    sendChangeMessage();
}

bool Publisher::start (Job j)
{
    if (isThreadRunning()) return false;
    auto* found = library.find (j.beatId);
    if (found == nullptr) return false;

    job = std::move (j);
    beat = *found;
    {
        const juce::ScopedLock sl (lock);
        error.clear(); url.clear(); pack = juce::File(); beatId = beat.id;
    }
    setStage ("Starting", 0.0);
    startThread();
    return true;
}

void Publisher::cancel()
{
    signalThreadShouldExit();
    stopThread (8000);
}

static juce::String safeName (const juce::String& title)
{
    auto name = juce::File::createLegalFileName (title.trim());
    return name.isEmpty() ? juce::String ("beat") : name;
}

juce::File Publisher::packFolder (const Library& lib, const Beat& b) { return lib.beatDir (b.id).getChildFile ("For BeatStars"); }

bool Publisher::makePack (const Beat& b, juce::String& problem)
{
    auto dir = packFolder (library, b);
    dir.deleteRecursively();
    if (! dir.createDirectory()) { problem = "Could not create the BeatStars folder."; return false; }

    const auto audio = library.audioOf (b);
    const auto name = safeName (b.title);
    if (! audio.existsAsFile()) { problem = "The beat's audio file is missing."; return false; }
    audio.copyFileTo (dir.getChildFile (name + audio.getFileExtension()));

    if (audio.getFileExtension().equalsIgnoreCase (".wav") || audio.getFileExtension().equalsIgnoreCase (".aiff"))
    {
        juce::String ignored;       // a build without MP3 support still gives a usable pack
        media::encodeMp3 (audio, dir.getChildFile (name + ".mp3"), b.seconds,
                          [this] (double f) { fraction.store (f); return ! threadShouldExit(); }, ignored);
    }

    const auto cover = juce::ImageFileFormat::loadFrom (library.coverOf (b));
    if (cover.isValid())
    {
        juce::JPEGImageFormat jpeg;
        jpeg.setQuality (0.92f);
        auto out = dir.getChildFile (name + " cover.jpg");
        if (auto stream = out.createOutputStream())
            jpeg.writeImageToStream (media::squareCover (cover, 1500), *stream);
    }

    juce::StringArray lines;
    lines.add ("Title: " + b.title);
    lines.add ("Price: " + texts::priceText (b.price));
    if (b.bpm > 0)            lines.add ("BPM: " + juce::String (juce::roundToInt (b.bpm)));
    if (b.key.isNotEmpty())   lines.add ("Key: " + b.key);
    lines.add ("Tags: " + texts::youtubeTags (b).joinIntoString (", "));
    if (b.description.isNotEmpty()) { lines.add ({}); lines.add (b.description); }
    dir.getChildFile ("details.txt").replaceWithText (lines.joinIntoString ("\r\n"));

    const juce::ScopedLock sl (lock);
    pack = dir;
    return true;
}

void Publisher::run()
{
    juce::String problem, videoId;
    const auto dir = library.beatDir (beat.id);
    const auto audio = library.audioOf (beat);

    if (job.youtube)
    {
        const auto frameFile = dir.getChildFile ("frame.png");
        const auto video = dir.getChildFile ("video.mp4");
        frameFile.deleteFile();
        {
            juce::PNGImageFormat png;
            if (auto stream = frameFile.createOutputStream())
                png.writeImageToStream (job.frame, *stream);
        }

        setStage ("Making the video", 0.0);
        bool ok = audio.existsAsFile()
               && media::renderVideo (frameFile, audio, video, beat.seconds,
                                      [this] (double f) { fraction.store (f * 0.35); sendChangeMessage(); return ! threadShouldExit(); }, problem);
        if (! ok && problem.isEmpty()) problem = "The beat's audio file is missing.";

        if (ok)
        {
            setStage ("Uploading to YouTube", 0.35);
            videoId = youtube::upload (google, video, job.request,
                                       [this] (double f) { fraction.store (0.35 + f * 0.6); sendChangeMessage(); return ! threadShouldExit(); }, problem);
        }
    }

    if (problem.isEmpty() && job.beatstarsPack && ! threadShouldExit())
    {
        setStage ("Packing the files for BeatStars", 0.96);
        makePack (beat, problem);
    }

    const bool scheduled = job.request.publishAt > juce::Time::getCurrentTime();
    {
        const juce::ScopedLock sl (lock);
        error = threadShouldExit() ? juce::String ("Cancelled") : problem;
        if (videoId.isNotEmpty()) url = "https://youtu.be/" + videoId;
        stageText = error.isNotEmpty() ? juce::String ("Stopped")
                  : (videoId.isNotEmpty() ? (scheduled ? "Scheduled on YouTube" : "Live on YouTube") : juce::String ("Ready"));
    }
    fraction.store (1.0);

    juce::MessageManager::callAsync ([this, id = beat.id, videoId]
    {
        if (videoId.isNotEmpty())
            if (auto* saved = library.find (id))
            {
                auto updated = *saved;
                updated.youtubeId = videoId;
                updated.youtubeUrl = "https://youtu.be/" + videoId;
                updated.uploadedMs = juce::Time::currentTimeMillis();
                library.save (updated, {}, {}, false);
            }
        sendChangeMessage();
    });
}
}
