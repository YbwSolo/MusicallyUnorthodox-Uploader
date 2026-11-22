// Checks the parts that need no DAW and no account. Run with HOME pointing at an empty folder.
#include "core/Capture.h"
#include "core/Publisher.h"
#include "core/Assistant.h"
#include "core/Texts.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <iostream>

using namespace sd;

static int failures = 0;
static void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  ok    " : "  FAIL  ") << what << std::endl;
    if (! ok) ++failures;
}

static void pump (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

static bool has (const AssistantReply& r, const juce::String& type, const juce::String& text = {}, double number = -1)
{
    for (auto& a : r.actions)
        if (a.type == type && (text.isEmpty() || a.text == text) && (number < 0 || std::abs (a.number - number) < 0.001))
            return true;
    return false;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    const auto scratch = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("muu-tests-" + juce::String (juce::Random::getSystemRandom().nextInt (100000)));
    scratch.createDirectory();

    std::cout << "Catching an export" << std::endl;
    juce::File caught;
    double caughtSeconds = 0;
    const int block = 512, blocks = 1722;                  // 20 seconds at 44.1 kHz
    {
        Capture capture;
        capture.onCaptured = [&] (juce::File f, double s) { caught = f; caughtSeconds = s; };
        capture.prepare (44100.0, 2);
        juce::AudioBuffer<float> buffer (2, block);
        double phase = 0;
        for (int b = 0; b < blocks; ++b)                    // as fast as the machine can go, like a real export
        {
            for (int i = 0; i < block; ++i)
            {
                const float v = 0.4f * (float) std::sin (phase);
                phase += 2.0 * juce::MathConstants<double>::pi * (220.0 + 110.0 * (b / 400)) / 44100.0;
                buffer.setSample (0, i, v);
                buffer.setSample (1, i, v);
            }
            capture.process (buffer, true, true, true);
        }
        check (capture.state() == Capture::State::recording, "recording starts by itself when the host renders offline");
        pump (1400);
        check (capture.state() == Capture::State::idle, "it stops by itself when the export ends");
        check (caught.existsAsFile(), "the take is handed over as a file");
        check (std::abs (caughtSeconds - 20.0) < 0.05, "length is right (" + juce::String (caughtSeconds, 2) + " s)");

        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (caught));
        check (reader != nullptr && reader->lengthInSamples == (juce::int64) block * blocks, "no samples were dropped while the export ran faster than real time");

        capture.arm();
        capture.process (buffer, false, true, false);
        check (capture.state() == Capture::State::armed, "armed waits for play");
        for (int b = 0; b < 900; ++b) capture.process (buffer, true, true, false);
        check (capture.state() == Capture::State::recording, "armed starts on play");
        capture.process (buffer, false, true, false);
        juce::File second;
        capture.onCaptured = [&] (juce::File f, double) { second = f; };
        pump (500);
        check (capture.state() == Capture::State::idle && second.existsAsFile(), "a play-through ends when the transport stops");
        second.deleteFile();

        capture.arm();
        for (int b = 0; b < 100; ++b) capture.process (buffer, true, true, false);
        capture.process (buffer, false, true, false);
        second = juce::File();
        pump (500);
        check (! second.existsAsFile(), "a take of a second or two is thrown away, not offered as a beat");
        capture.release();
    }

    std::cout << "Library and catalogs" << std::endl;
    juce::String beatId;
    {
        Library lib (scratch.getChildFile ("lib"));
        const auto trap = lib.addCatalog ("Trap", {});
        const auto dark = lib.addCatalog ("Dark", trap.id);
        const auto rnb = lib.addCatalog ("R&B", {});
        Beat b;
        b.title = "Get It"; b.artist = "Gunna"; b.bpm = 161; b.key = "E Minor"; b.catalogId = dark.id; b.seconds = caughtSeconds;
        b = lib.save (b, caught, {}, true);
        beatId = b.id;
        check (lib.audioOf (b).existsAsFile() && ! caught.existsAsFile(), "saving moves the take into the beat's own folder");
        check (lib.catalogPath (dark.id) == "Trap / Dark", "sub-category path");
        check (lib.beatsIn (trap.id, {}).size() == 1 && lib.beatsIn (rnb.id, {}).isEmpty(), "a catalog shows the beats in its sub-categories");
        check (lib.beatsIn ({}, "gunna").size() == 1 && lib.beatsIn ({}, "drake").isEmpty(), "search");
        lib.removeCatalog (dark.id);
        check (lib.find (beatId)->catalogId == trap.id, "deleting a sub-category keeps its beats, one level up");
    }
    {
        Library lib (scratch.getChildFile ("lib"));
        check (lib.allBeats().size() == 1 && lib.allCatalogs().size() == 2 && lib.find (beatId)->title == "Get It", "everything is still there after reopening");
    }

    std::cout << "The words it writes" << std::endl;
    Settings settings;
    settings.set ("producer", "JP");
    settings.set ("storeLink", "https://www.beatstars.com/jp");
    {
        Beat b;
        b.title = "Get It"; b.artist = "Gunna"; b.bpm = 161; b.key = "E Minor"; b.price = 20; b.tags = "wunna, melodic";
        const auto year = juce::String (juce::Time::getCurrentTime().getYear());
        check (texts::youtubeTitle (b) == "[FREE FOR PROFIT] Gunna Type Beat " + year + " - \"Get It\"", "title: " + texts::youtubeTitle (b));
        const auto ffp = texts::youtubeDescription (b, settings);
        check (ffp.contains ("FREE FOR PROFIT") && ffp.contains ("Content ID") && ffp.contains ("prod. JP") && ffp.contains ("beatstars.com/jp") && ffp.contains ("$20"), "free for profit wording");
        b.licence = "free";
        const auto free = texts::youtubeDescription (b, settings);
        check (texts::youtubeTitle (b).startsWith ("[FREE] ") && free.contains ("you need a lease") && ! free.contains ("FREE FOR PROFIT"), "free wording says a lease is needed to release");
        check (! ffp.containsAnyOf ("<>") && ffp.length() < 5000 && texts::youtubeTitle (b).length() <= 100, "inside YouTube's limits");
        check (texts::youtubeTags (b).joinIntoString (",").length() < 500 && texts::youtubeTags (b).contains ("gunna type beat") && texts::youtubeTags (b).contains ("wunna"), "tags");
        b.title = juce::String::repeatedString ("Long ", 40);
        check (texts::youtubeTitle (b).length() <= 100, "a very long name is cut to fit");
        b.title = "Get It";
        const auto mail = texts::emailBody (b, settings, "Sam", "Northside Records", 0);
        check (mail.startsWith ("Hi Sam,") && mail.contains ("Northside Records") && mail.contains ("beatstars.com/jp") && mail.contains ("161 BPM"), "email");
    }

    std::cout << "Assistant" << std::endl;
    {
        Library lib (scratch.getChildFile ("lib"));
        Draft d;
        auto r = assistant::understand ("Name it Get It, Gunna type beat, 161 bpm, key E minor, $25, free for profit, upload it", d, lib);
        check (has (r, "setTitle", "Get It"), "title");
        check (has (r, "setArtist", "Gunna"), "artist");
        check (has (r, "setBpm", {}, 161) && has (r, "setKey", "E Minor") && has (r, "setPrice", {}, 25), "bpm, key, price");
        check (has (r, "setLicence", "ffp") && has (r, "upload"), "licence and upload");

        r = assistant::understand ("call it \"Midnight Run\" and make it free, price 30, put it in catalog Trap", d, lib);
        check (has (r, "setTitle", "Midnight Run") && has (r, "setLicence", "free") && has (r, "setPrice", {}, 30) && has (r, "setCatalog", "Trap"), "quoted title, free, catalog");

        r = assistant::understand ("make this a lil baby type beat in F# minor", d, lib);
        check (has (r, "setArtist", "Lil Baby") && has (r, "setKey", "F# Minor"), "two-word artist and sharp key");

        r = assistant::understand ("post it tomorrow at 6pm", d, lib);
        bool timeOk = false;
        for (auto& a : r.actions)
            if (a.type == "schedule")
            {
                const juce::Time t ((juce::int64) a.number);
                timeOk = t.getHours() == 18 && t.getMinutes() == 0 && t > juce::Time::getCurrentTime();
            }
        check (timeOk && has (r, "upload"), "schedule for tomorrow at 6pm");

        r = assistant::understand ("make it unlisted and save", d, lib);
        check (has (r, "setPrivacy", "unlisted") && has (r, "save") && ! has (r, "upload"), "unlisted and save");
        r = assistant::understand ("tags are dark, guitar, wunna", d, lib);
        check (has (r, "setTags", "dark, guitar, wunna"), "tags");
        r = assistant::understand ("what's the weather", d, lib);
        check (r.actions.isEmpty() && r.message.isNotEmpty(), "does nothing when it does not understand");
        r = assistant::understand ("this needs a major label", d, lib);
        check (! has (r, "setKey"), "ordinary words are not mistaken for a key");
    }

    std::cout << "Video, MP3 and the BeatStars pack" << std::endl;
    if (media::available())
    {
        Library lib (scratch.getChildFile ("lib"));
        auto beat = *lib.find (beatId);

        juce::Image cover (juce::Image::RGB, 1000, 1000, true);
        {
            juce::Graphics g (cover);
            g.setGradientFill (juce::ColourGradient (juce::Colours::darkred, 0, 0, juce::Colours::orange, 1000, 1000, false));
            g.fillAll();
        }
        const auto coverFile = scratch.getChildFile ("cover.png");
        { juce::PNGImageFormat png; auto out = coverFile.createOutputStream(); png.writeImageToStream (cover, *out); }
        beat = lib.save (beat, {}, coverFile, false);

        const auto frame = media::composeFrame (cover, beat.title, "Gunna type beat   |   prod. JP");
        const auto frameFile = scratch.getChildFile ("frame.png");
        { juce::PNGImageFormat png; auto out = frameFile.createOutputStream(); png.writeImageToStream (frame, *out); }
        check (frame.getWidth() == 1920 && frame.getHeight() == 1080, "frame is 1920 x 1080");

        juce::String error;
        double last = 0;
        const auto video = scratch.getChildFile ("video.mp4");
        const bool ok = media::renderVideo (frameFile, lib.audioOf (beat), video, beat.seconds, [&] (double f) { last = f; return true; }, error);
        check (ok && video.getSize() > 50000, "video made (" + juce::String (video.getSize() / 1024) + " KB) " + error);
        check (last > 0.5, "progress was reported while it rendered");

        juce::ChildProcess probe;
        probe.start (juce::StringArray { media::findFFmpeg().getFullPathName(), "-hide_banner", "-i", video.getFullPathName() });
        const auto info = probe.readAllProcessOutput();
        check ((info.contains ("Duration: 00:00:19.9") || info.contains ("Duration: 00:00:20")) && info.contains ("Video:") && info.contains ("Audio: aac") && info.contains ("1920x1080"), "the video is the full length, 1080p, with sound");

        Settings s2;
        GoogleAuth google (s2);
        Publisher publisher (lib, s2, google);
        Publisher::Job job;
        job.beatId = beat.id;
        job.beatstarsPack = true;
        check (publisher.start (job), "pack job starts");
        for (int i = 0; i < 200 && publisher.running(); ++i) pump (100);
        pump (200);
        const auto pack = Publisher::packFolder (lib, beat);
        check (pack.getChildFile ("Get It.wav").existsAsFile() && pack.getChildFile ("Get It cover.jpg").existsAsFile()
               && pack.getChildFile ("details.txt").loadFileAsString().contains ("Price: $20"), "BeatStars pack has the WAV, a square cover and the details " + publisher.lastError());
        std::cout << "        mp3 in pack: " << (pack.getChildFile ("Get It.mp3").existsAsFile() ? "yes" : "no (this ffmpeg has no MP3 encoder)") << std::endl;
        std::cout << "        files kept in " << scratch.getFullPathName() << std::endl;
    }
    else
        std::cout << "  skipped: no ffmpeg on this machine" << std::endl;

    std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}
