// Checks the parts that need no DAW and no account. Run with HOME pointing at an empty folder.
#include "core/Capture.h"
#include "core/Publisher.h"
#include "core/Assistant.h"
#include "core/Texts.h"
#include "PluginProcessor.h"
#include "ui/Theme.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <iostream>
#include <cstring>

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
        capture.setListen (false);
        capture.onCaptured = [&] (juce::File f, double s, Capture::Kind) { caught = f; caughtSeconds = s; };
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
        for (int b = 0; b < 900; ++b) { capture.process (buffer, true, true, false); if (b % 48 == 47) juce::Thread::sleep (4); }
        check (capture.state() == Capture::State::recording, "armed starts on play");
        capture.process (buffer, false, true, false);
        juce::File second;
        capture.onCaptured = [&] (juce::File f, double, Capture::Kind) { second = f; };
        pump (700);
        check (capture.state() == Capture::State::idle && second.existsAsFile(), "a play-through ends when the transport stops");
        second.deleteFile();

        capture.arm();
        for (int b = 0; b < 100; ++b) capture.process (buffer, true, true, false);
        capture.process (buffer, false, true, false);
        second = juce::File();
        pump (700);
        check (! second.existsAsFile(), "a take of a second or two is thrown away, not offered as a beat");
        capture.release();
    }

    std::cout << "Catching the beat just by playing it" << std::endl;
    {
        // feeds audio the way a host does while it plays: silence, then the beat, then silence, then stop
        auto play = [&] (Capture& capture, double leadIn, double sound, double tail, juce::int64 startSample = 0)
        {
            juce::AudioBuffer<float> buffer (2, block);
            double phase = 0;
            juce::int64 at = startSample;
            const int total = (int) ((leadIn + sound + tail) * 44100.0 / block);
            for (int b = 0; b < total; ++b)
            {
                const double t = (double) b * block / 44100.0;
                const bool audible = t >= leadIn && t < leadIn + sound;
                for (int i = 0; i < block; ++i)
                {
                    const float v = audible ? 0.4f * (float) std::sin (phase) : 0.0f;
                    phase += 2.0 * juce::MathConstants<double>::pi * 330.0 / 44100.0;
                    buffer.setSample (0, i, v);
                    buffer.setSample (1, i, v);
                }
                capture.process (buffer, true, true, false, at);
                at += block;
                if (b % 48 == 47) juce::Thread::sleep (4);          // let the disk keep up, as it does in real time
            }
            buffer.clear();
            capture.process (buffer, false, true, false, at);       // transport stopped
            return at;
        };

        Capture capture;
        juce::File heard;
        double heardSeconds = 0;
        Capture::Kind heardKind = Capture::Kind::exported;
        capture.onCaptured = [&] (juce::File f, double s, Capture::Kind k) { heard = f; heardSeconds = s; heardKind = k; };
        capture.prepare (44100.0, 2);

        play (capture, 1.5, 24.0, 2.0);
        pump (1200);
        check (heard.existsAsFile() && heardKind == Capture::Kind::heard, "a play-through is caught with nothing pressed");
        check (std::abs (heardSeconds - 24.4) < 0.25, "the silence before and after is trimmed off (" + juce::String (heardSeconds, 2) + " s from 27.5 s played)");
        heard.deleteFile();
        heard = juce::File();

        play (capture, 0.0, 9.0, 0.0);
        pump (900);
        check (! heard.existsAsFile(), "playing a few seconds of the project is not mistaken for the beat");

        // a loop: playback jumps back to the start without the transport stopping
        juce::AudioBuffer<float> buffer (2, block);
        for (int i = 0; i < block; ++i) { buffer.setSample (0, i, 0.3f); buffer.setSample (1, i, -0.3f); }
        juce::int64 at = 0;
        for (int b = 0; b < 2200; ++b) { capture.process (buffer, true, true, false, at); at += block; if (b % 48 == 47) juce::Thread::sleep (4); }
        capture.process (buffer, true, true, false, 0);
        pump (1200);
        check (heard.existsAsFile() && heardSeconds > 24.0 && heardSeconds < 27.0, "a jump back to the start ends the take there (" + juce::String (heardSeconds, 2) + " s)");
        capture.stop();
        pump (300);
        heard.deleteFile();
        capture.release();

        // the rules for what a heard take may replace
        UploaderProcessor processor;
        processor.prepareToPlay (44100.0, block);
        auto& d = processor.draft;
        play (processor.capture, 0.5, 30.0, 0.5);
        pump (1300);
        const auto first = d.audio;
        check (first.existsAsFile() && std::abs (d.beat.seconds - 30.4) < 0.3 && processor.draftWasHeard, "with nothing on the page, the play-through goes straight on");

        play (processor.capture, 0.0, 21.0, 0.2);
        pump (1300);
        check (d.audio == first, "then listening back to a part of it does not replace the full take");

        play (processor.capture, 0.5, 32.0, 0.5);
        pump (1300);
        check (d.audio != first && std::abs (d.beat.seconds - 32.4) < 0.3 && ! first.existsAsFile(), "a newer full play-through does, and the old take is cleaned up");

        d.beat.title = "Chosen";
        processor.draftWasHeard = false;                    // as if this audio had been exported or chosen on purpose
        const auto chosen = d.audio;
        play (processor.capture, 0.5, 30.0, 0.5);
        pump (1300);
        check (d.audio == chosen && processor.waitingTake.file.existsAsFile(), "audio put there on purpose is never replaced: the new take waits");
        processor.useWaitingTake();
        check (d.audio != chosen && d.beat.title == "Chosen" && ! processor.waitingTake.file.existsAsFile(), "Use the new take swaps the audio and keeps the details typed in");
        d.audio.deleteFile();
        processor.releaseResources();
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

    std::cout << "Updating from inside the plugin" << std::endl;
    {
        UpdateInfo info;
        const juce::String sha = juce::String::repeatedString ("ab", 32);
        check (Updater::parseFeed ("{\"build\":7,\"version\":\"0.1.7\",\"file\":\"MUUploader-bundles.zip\",\"sha256\":\"" + sha + "\"}", info)
               && info.build == 7 && info.version == "0.1.7", "reads the update feed");
        check (! Updater::parseFeed ("{\"build\":7,\"file\":\"../evil.zip\",\"sha256\":\"" + sha + "\"}", info)
               && ! Updater::parseFeed ("{\"build\":7,\"file\":\"a.zip\",\"sha256\":\"short\"}", info)
               && ! Updater::parseFeed ("not json", info), "refuses a feed that points somewhere odd or has no proper checksum");

       #if ! JUCE_WINDOWS
        // a pretend release on disk: new plugin bundles and a new video tool, zipped, with its feed
        const auto feed = scratch.getChildFile ("feed"), make = scratch.getChildFile ("make");
        const auto oldAU = scratch.getChildFile ("installed/MU Uploader.component"), oldVST = scratch.getChildFile ("installed/MU Uploader.vst3");
        const auto toolDir = scratch.getChildFile ("installed/tool");
        feed.createDirectory();
        toolDir.createDirectory();
        for (auto* kind : { "Components/MU Uploader.component", "VST3/MU Uploader.vst3" })
        {
            make.getChildFile (kind).getChildFile ("Contents/MacOS").createDirectory();
            make.getChildFile (kind).getChildFile ("Contents/MacOS/MU Uploader").replaceWithText ("new plugin");
            make.getChildFile (kind).getChildFile ("Contents/Info.plist").replaceWithText ("new info");
        }
        make.getChildFile ("Support").createDirectory();
        make.getChildFile ("Support/ffmpeg").replaceWithText ("new tool");
        oldAU.getChildFile ("Contents/MacOS").createDirectory();
        oldAU.getChildFile ("Contents/MacOS/MU Uploader").replaceWithText ("old plugin");
        oldAU.getChildFile ("Contents/Resources/gone.txt").create();
        oldVST.getChildFile ("Contents/MacOS").createDirectory();
        toolDir.getChildFile ("ffmpeg").replaceWithText ("old tool");

        juce::ChildProcess zipper;
        zipper.start (juce::StringArray { "sh", "-c", "cd '" + make.getFullPathName() + "' && zip -qry '"
                                                        + feed.getChildFile ("MUUploader-bundles.zip").getFullPathName() + "' Components VST3 Support" });
        zipper.waitForProcessToFinish (20000);
        const auto zipSha = Updater::sha256Of (feed.getChildFile ("MUUploader-bundles.zip"));
        feed.getChildFile ("latest.json").replaceWithText ("{\"build\":9,\"version\":\"0.1.9\",\"file\":\"MUUploader-bundles.zip\",\"sha256\":\"" + zipSha + "\"}");

        Settings s3;
        Updater updater (s3);
        auto waitFor = [&] { for (int i = 0; i < 300; ++i) { pump (50); const auto p = updater.getState().phase;
                                 if (p != UpdateState::Phase::checking && p != UpdateState::Phase::downloading && p != UpdateState::Phase::installing) break; } };
        const auto feedUrl = "file://" + feed.getFullPathName();
        updater.configureForTesting (feedUrl, oldAU, oldVST, toolDir, 9);
        updater.checkNow(); waitFor();
        check (updater.getState().phase == UpdateState::Phase::upToDate, "says it is up to date when it is");

        updater.configureForTesting (feedUrl, oldAU, oldVST, toolDir, 5);
        updater.checkNow(); waitFor();
        check (updater.getState().phase == UpdateState::Phase::available && updater.getState().latest.version == "0.1.9", "finds a newer build");
        updater.installNow(); waitFor();
        check (updater.getState().phase == UpdateState::Phase::installed
               && oldAU.getChildFile ("Contents/MacOS/MU Uploader").loadFileAsString() == "new plugin"
               && oldVST.getChildFile ("Contents/Info.plist").loadFileAsString() == "new info"
               && ! oldAU.getChildFile ("Contents/Resources/gone.txt").exists(), "installs it over the old plugin and clears out what the new one dropped  " + updater.getState().message);
        check (toolDir.getChildFile ("ffmpeg").loadFileAsString() == "new tool", "the video tool is updated with it");

        feed.getChildFile ("latest.json").replaceWithText ("{\"build\":10,\"version\":\"0.1.10\",\"file\":\"MUUploader-bundles.zip\",\"sha256\":\"" + sha + "\"}");
        oldAU.getChildFile ("Contents/MacOS/MU Uploader").replaceWithText ("keep me");
        Updater second (s3);
        auto waitSecond = [&] { for (int i = 0; i < 300; ++i) { pump (50); const auto p = second.getState().phase;
                                    if (p != UpdateState::Phase::checking && p != UpdateState::Phase::downloading && p != UpdateState::Phase::installing) break; } };
        second.configureForTesting (feedUrl, oldAU, oldVST, toolDir, 5);
        second.checkNow(); waitSecond();
        second.installNow(); waitSecond();
        check (second.getState().phase == UpdateState::Phase::failed && oldAU.getChildFile ("Contents/MacOS/MU Uploader").loadFileAsString() == "keep me",
               "a download that does not match its checksum is thrown away and nothing is touched");
       #endif
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

        media::FrameText words;
        words.title = beat.title; words.subtitle = "Gunna type beat"; words.label = "FREE FOR PROFIT"; words.credit = "prod. JP";
        words.chips = { "161 BPM", "E MINOR" };
        const auto frame = media::composeFrame (cover, words);
        check (frame.image.getWidth() == 1920 && frame.image.getHeight() == 1080 && frame.bands > 0
               && juce::Rectangle<int> (1920, 1080).contains (frame.bars), "picture is 1920 x 1080 with room for the bars");
        check (frame.accent.getHue() < 0.14f && frame.accent.getSaturation() > 0.5f, "the accent colour is taken from the cover (" + frame.accent.toDisplayString (false) + ")");
        check (media::accentOf (juce::Image (juce::Image::RGB, 64, 64, true)) == theme::yellow, "a colourless cover falls back to the house yellow");

        juce::String error;
        double last = 0;
        const auto video = scratch.getChildFile ("video.mp4");
        const bool ok = media::renderVideo (frame, lib.audioOf (beat), video, beat.seconds, [&] (double f) { last = f; return true; }, error);
        check (! scratch.getChildFile ("video-bars.raw").exists() && ! scratch.getChildFile ("video-picture.png").exists(), "working files are cleaned up afterwards");

        // kept for the build machine, which decodes the video and checks its colours against this picture
        const auto keep = juce::SystemStats::getEnvironmentVariable ("MUU_KEEP", {});
        if (keep.isNotEmpty())
        {
            juce::File dir (keep);
            dir.createDirectory();
            video.copyFileTo (dir.getChildFile ("video.mp4"));
            juce::PNGImageFormat png;
            dir.getChildFile ("picture.png").deleteFile();
            if (auto out = dir.getChildFile ("picture.png").createOutputStream()) png.writeImageToStream (frame.image, *out);
        }
        check (ok && video.getSize() > 50000, "video made (" + juce::String (video.getSize() / 1024) + " KB) " + error);
        check (last > 0.5, "progress was reported while it rendered");

        // read the MP4 itself rather than asking ffmpeg, because the small ffmpeg that ships only writes MP4s
        juce::MemoryBlock bytes;
        video.loadFileAsData (bytes);
        auto findAtom = [&] (const char* name) -> int
        {
            const auto* data = (const char*) bytes.getData();
            for (size_t i = 0; i + 4 <= bytes.getSize(); ++i)
                if (std::memcmp (data + i, name, 4) == 0) return (int) i;
            return -1;
        };
        auto bigEndian = [&] (int at)
        {
            const auto* d = (const unsigned char*) bytes.getData() + at;
            return (double) (((juce::uint32) d[0] << 24) | ((juce::uint32) d[1] << 16) | ((juce::uint32) d[2] << 8) | (juce::uint32) d[3]);
        };
        const int mvhd = findAtom ("mvhd");
        const double length = mvhd >= 0 ? bigEndian (mvhd + 20) / juce::jmax (1.0, bigEndian (mvhd + 16)) : 0.0;
        const bool h264 = findAtom ("avc1") >= 0;
        check (findAtom ("moov") >= 0 && (h264 || findAtom ("mp4v") >= 0) && findAtom ("mp4a") >= 0 && std::abs (length - 20.0) < 0.6,
               "the video is the full length with picture and sound (" + juce::String (length, 2) + " s, " + (h264 ? "H.264" : "MPEG-4") + ")");

        // the vertical clip: 30 quiet seconds then 40 loud ones, so the clip should start late and be 45 s long
        const auto longAudio = scratch.getChildFile ("long.wav");
        {
            juce::WavAudioFormat wav;
            auto* stream = longAudio.createOutputStream().release();
            std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream, 44100.0, 2, 16, {}, 0));
            juce::AudioBuffer<float> one (2, 44100);
            for (int sec = 0; sec < 70; ++sec)
            {
                for (int i = 0; i < 44100; ++i)
                {
                    const float v = (sec < 30 ? 0.05f : 0.5f) * (float) std::sin (i * 0.05);
                    one.setSample (0, i, v);
                    one.setSample (1, i, v);
                }
                writer->writeFromAudioSampleBuffer (one, 0, 44100);
            }
        }
        const double clipStart = media::bestClipStart (longAudio, 45.0);
        check (clipStart >= 18.0 && clipStart <= 25.0, "the clip is cut from the full part of the beat (starts at " + juce::String (clipStart, 0) + " s)");

        const auto tall = media::composeFrame (cover, words, true);
        const auto clip = scratch.getChildFile ("clip.mp4");
        const bool clipOk = media::renderVideo (tall, longAudio, clip, 45.0, {}, error, clipStart, 1.5);
        juce::MemoryBlock clipBytes;
        clip.loadFileAsData (clipBytes);
        bytes = clipBytes;
        const int clipHeader = findAtom ("mvhd");
        const double clipLength = clipHeader >= 0 ? bigEndian (clipHeader + 20) / juce::jmax (1.0, bigEndian (clipHeader + 16)) : 0.0;
        check (clipOk && tall.image.getWidth() == 1080 && tall.image.getHeight() == 1920 && juce::Rectangle<int> (1080, 1700).contains (tall.bars) && std::abs (clipLength - 45.0) < 0.6,
               "vertical clip made: 1080 x 1920, " + juce::String (clipLength, 1) + " s " + error);

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
