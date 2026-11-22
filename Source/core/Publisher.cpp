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
juce::File Publisher::clipFile (const Library& lib, const Beat& b)   { return lib.beatDir (b.id).getChildFile (safeName (b.title) + " vertical.mp4"); }

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
    juce::String problem, videoId, shortId;
    const auto dir = library.beatDir (beat.id);
    const auto audio = library.audioOf (beat);
    auto writePng = [] (const juce::Image& image, const juce::File& file)
    {
        file.deleteFile();
        juce::PNGImageFormat png;
        if (auto stream = file.createOutputStream())
            png.writeImageToStream (image, *stream);
    };

    if (! audio.existsAsFile())
        problem = "The beat's audio file is missing.";

    if (problem.isEmpty() && job.youtube)
    {
        const auto frameFile = dir.getChildFile ("frame.png");
        const auto video = dir.getChildFile ("video.mp4");
        writePng (job.frame, frameFile);

        setStage ("Making the video", 0.0);
        if (media::renderVideo (frameFile, audio, video, beat.seconds,
                                [this] (double f) { fraction.store (f * 0.25); sendChangeMessage(); return ! threadShouldExit(); }, problem))
        {
            setStage ("Uploading to YouTube", 0.25);
            videoId = youtube::upload (google, video, job.request,
                                       [this] (double f) { fraction.store (0.25 + f * 0.45); sendChangeMessage(); return ! threadShouldExit(); }, problem);
        }
    }

    if (problem.isEmpty() && job.clip && ! threadShouldExit())
    {
        // a short vertical cut of the fullest part of the beat, for Shorts, TikTok and Reels
        const auto frameFile = dir.getChildFile ("frame-tall.png");
        const auto clip = clipFile (library, beat);
        writePng (job.tallFrame, frameFile);
        const double length = juce::jmin (45.0, beat.seconds);
        const double start = media::bestClipStart (audio, length);

        setStage ("Making the vertical clip", 0.70);
        if (media::renderVideo (frameFile, audio, clip, length,
                                [this] (double f) { fraction.store (0.70 + f * 0.10); sendChangeMessage(); return ! threadShouldExit(); }, problem,
                                start, beat.seconds > length ? 1.5 : 0.0)
            && job.youtube)
        {
            auto request = job.shortRequest;
            const auto full = videoId.isNotEmpty() ? "https://youtu.be/" + videoId : beat.youtubeUrl;
            request.description = full.isNotEmpty() ? request.description.replace ("{full}", full)
                                                    : request.description.replace ("Full beat: {full}\n", "");
            setStage ("Posting the Short", 0.80);
            shortId = youtube::upload (google, clip, request,
                                       [this] (double f) { fraction.store (0.80 + f * 0.15); sendChangeMessage(); return ! threadShouldExit(); }, problem);
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

    juce::MessageManager::callAsync ([this, id = beat.id, videoId, shortId]
    {
        if (videoId.isNotEmpty() || shortId.isNotEmpty())
            if (auto* saved = library.find (id))
            {
                auto updated = *saved;
                if (videoId.isNotEmpty())
                {
                    updated.youtubeId = videoId;
                    updated.youtubeUrl = "https://youtu.be/" + videoId;
                }
                if (shortId.isNotEmpty())
                    updated.shortUrl = "https://youtube.com/shorts/" + shortId;
                updated.uploadedMs = juce::Time::currentTimeMillis();
                library.save (updated, {}, {}, false);
            }
        sendChangeMessage();
    });
}
}
