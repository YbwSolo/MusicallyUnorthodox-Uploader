#include "Media.h"
#include "Settings.h"
#include "../ui/Theme.h"
#include <juce_audio_formats/juce_audio_formats.h>

namespace sd::media
{
juce::File findFFmpeg()
{
   #if JUCE_WINDOWS
    const juce::String exe = "ffmpeg.exe";
   #else
    const juce::String exe = "ffmpeg";
   #endif

    juce::Array<juce::File> places { Settings::sharedDir().getChildFile (exe), Settings::appDataDir().getChildFile (exe) };
   #if ! JUCE_WINDOWS
    places.add (juce::File ("/opt/homebrew/bin/ffmpeg"));
    places.add (juce::File ("/usr/local/bin/ffmpeg"));
    places.add (juce::File ("/usr/bin/ffmpeg"));
   #endif

    for (auto& f : places)
        if (f.existsAsFile())
            return f;
    return {};
}

bool available() { return findFFmpeg().existsAsFile(); }

static juce::String encoderList()
{
    static juce::String cached;
    static juce::CriticalSection lock;
    const juce::ScopedLock sl (lock);
    if (cached.isEmpty())
    {
        juce::ChildProcess p;
        if (p.start (juce::StringArray { findFFmpeg().getFullPathName(), "-hide_banner", "-encoders" }))
            cached = p.readAllProcessOutput();
        if (cached.isEmpty()) cached = " ";
    }
    return cached;
}

static bool hasEncoder (const juce::String& name) { return encoderList().contains (" " + name + " "); }

static bool run (juce::StringArray args, double seconds, const Progress& progress, juce::String& error)
{
    auto ffmpeg = findFFmpeg();
    if (! ffmpeg.existsAsFile()) { error = "The video tool (ffmpeg) is missing. Run the installer again."; return false; }

    args.insert (0, ffmpeg.getFullPathName());
    juce::ChildProcess process;
    if (! process.start (args)) { error = "Could not start the video tool."; return false; }

    juce::String log;
    char buffer[2048];
    for (;;)
    {
        const int n = process.readProcessOutput (buffer, sizeof (buffer) - 1);
        if (n <= 0)
        {
            if (! process.isRunning()) break;
            juce::Thread::sleep (30);
            continue;
        }

        buffer[n] = 0;
        log << buffer;
        if (log.length() > 16000) log = log.substring (log.length() - 8000);

        // ffmpeg reports how far it has got as "time=00:01:23.45"
        const auto at = log.fromLastOccurrenceOf ("time=", false, false).upToFirstOccurrenceOf (" ", false, false);
        const auto parts = juce::StringArray::fromTokens (at, ":", {});
        if (parts.size() == 3 && seconds > 0.0 && progress != nullptr)
        {
            const double t = parts[0].getDoubleValue() * 3600.0 + parts[1].getDoubleValue() * 60.0 + parts[2].getDoubleValue();
            if (! progress (juce::jlimit (0.0, 1.0, t / seconds)))
            {
                process.kill();
                error = "Cancelled";
                return false;
            }
        }
    }

    if (process.getExitCode() != 0)
    {
        auto lines = juce::StringArray::fromLines (log.trim());
        error = "The video tool failed: " + (lines.isEmpty() ? juce::String ("unknown error") : lines[lines.size() - 1]);
        return false;
    }
    return true;
}

static juce::Image blurred (const juce::Image& source, int w, int h)
{
    // halving step by step averages the picture properly; stretching the tiny result back gives a soft, even blur
    auto small = source;
    while (small.getWidth() > 96 || small.getHeight() > 96)
        small = small.rescaled (juce::jmax (24, small.getWidth() / 2), juce::jmax (14, small.getHeight() / 2), juce::Graphics::mediumResamplingQuality);
    small = small.rescaled (24, 14, juce::Graphics::highResamplingQuality);
    auto mid = small.rescaled (192, 108, juce::Graphics::highResamplingQuality);
    return mid.rescaled (w, h, juce::Graphics::highResamplingQuality);
}

juce::Image composeFrame (const juce::Image& cover, const juce::String& title, const juce::String& subtitle, bool tall)
{
    const int w = tall ? 1080 : 1920, h = tall ? 1920 : 1080;
    juce::Image frame (juce::Image::RGB, w, h, true);
    juce::Graphics g (frame);
    g.fillAll (juce::Colours::black);

    // on a tall frame the phone apps cover the bottom and the right edge, so everything sits in the upper middle
    juce::Rectangle<float> art;
    if (cover.isValid())
    {
        // fill the frame with a blurred, darkened copy so there are never black bars
        const double scale = juce::jmax ((double) w / cover.getWidth(), (double) h / cover.getHeight());
        const int cw = juce::roundToInt (w / scale), ch = juce::roundToInt (h / scale);
        auto middle = cover.getClippedImage ({ (cover.getWidth() - cw) / 2, (cover.getHeight() - ch) / 2, cw, ch });
        g.setOpacity (1.0f);
        g.drawImageAt (blurred (middle, w, h), 0, 0);
        g.fillAll (juce::Colours::black.withAlpha (0.45f));

        const float side = tall ? 860.0f : 760.0f;
        const float fit = juce::jmin (side / cover.getWidth(), side / cover.getHeight());
        art = juce::Rectangle<float> (0, 0, cover.getWidth() * fit, cover.getHeight() * fit);
        art.setCentre (w * 0.5f, tall ? h * 0.40f : h * 0.5f - 40.0f);

        juce::DropShadow (juce::Colours::black.withAlpha (0.6f), 60, { 0, 24 }).drawForRectangle (g, art.toNearestInt());
        juce::Path clip;
        clip.addRoundedRectangle (art, 18.0f);
        g.saveState();
        g.reduceClipRegion (clip);
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.setOpacity (1.0f);
        g.drawImage (cover, art);
        g.restoreState();
    }
    else
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1a1a1a), 0, 0, juce::Colours::black, 0, (float) h, false));
        g.fillAll();
        g.setColour (theme::yellow);
        g.fillRect (juce::Rectangle<float> (w * 0.5f - 60.0f, h * (tall ? 0.40f : 0.5f) - 150.0f, 120.0f, 8.0f));
    }

    const int margin = tall ? 90 : 120;
    const float titleSize = cover.isValid() ? (tall ? 84.0f : 64.0f) : 120.0f;
    const float textY = cover.isValid() ? (tall ? art.getBottom() + 70.0f : (float) h - 170.0f) : h * (tall ? 0.40f : 0.5f) - 110.0f;
    const int titleHeight = (int) (titleSize * 1.2f);
    g.setColour (juce::Colours::white);
    g.setFont (theme::headline (titleSize));
    g.drawFittedText (title.toUpperCase(), juce::Rectangle<int> (margin, (int) textY, w - margin * 2, cover.isValid() ? titleHeight : 150), juce::Justification::centred, 1);
    g.setColour (theme::yellow);
    g.setFont (theme::bodyBold (tall ? 36.0f : 30.0f));
    g.drawFittedText (subtitle, juce::Rectangle<int> (margin, (int) textY + (cover.isValid() ? titleHeight + 10 : 170), w - margin * 2, 48), juce::Justification::centred, 1);
    return frame;
}

juce::Image squareCover (const juce::Image& cover, int size)
{
    if (! cover.isValid()) return {};
    const int side = juce::jmin (cover.getWidth(), cover.getHeight());
    auto middle = cover.getClippedImage ({ (cover.getWidth() - side) / 2, (cover.getHeight() - side) / 2, side, side });
    return middle.rescaled (size, size, juce::Graphics::highResamplingQuality);
}

bool renderVideo (const juce::File& frame, const juce::File& audio, const juce::File& outMp4, double seconds,
                  Progress progress, juce::String& error, double startAt, double fadeOut)
{
    // whichever H.264 encoder this ffmpeg has, best first; MPEG-4 always exists and YouTube accepts it too
    juce::Array<juce::StringArray> encoders;
    if (hasEncoder ("libx264"))             encoders.add ({ "-c:v", "libx264", "-tune", "stillimage", "-preset", "veryfast", "-crf", "18" });
    if (hasEncoder ("h264_videotoolbox"))   encoders.add ({ "-c:v", "h264_videotoolbox", "-allow_sw", "1", "-b:v", "3000k" });
    if (hasEncoder ("h264_mf"))             encoders.add ({ "-c:v", "h264_mf", "-b:v", "3000k" });
    if (hasEncoder ("libopenh264"))         encoders.add ({ "-c:v", "libopenh264", "-b:v", "3000k" });
    encoders.add ({ "-c:v", "mpeg4", "-q:v", "3" });

    // the fade is tried first and dropped if this ffmpeg cannot do it: a clip that ends abruptly beats no clip
    for (const bool fade : { fadeOut > 0.0 && seconds > fadeOut * 2.0, false })
    {
        for (auto& encoder : encoders)
        {
            outMp4.deleteFile();
            juce::StringArray args { "-hide_banner", "-y", "-loop", "1", "-framerate", "2", "-i", frame.getFullPathName() };
            if (startAt > 0.0) args.addArray ({ "-ss", juce::String (startAt, 3) });
            args.addArray ({ "-i", audio.getFullPathName() });
            args.addArray (encoder);
            args.addArray ({ "-pix_fmt", "yuv420p", "-r", "2", "-c:a", "aac", "-b:a", "320k", "-ar", "48000" });
            if (fade) args.addArray ({ "-af", "afade=t=out:st=" + juce::String (seconds - fadeOut, 3) + ":d=" + juce::String (fadeOut, 3) });
            // the length is given outright: some encoders hold frames back, and "stop with the audio" alone
            // then leaves seconds of silent picture on the end
            if (seconds > 0.0) args.addArray ({ "-t", juce::String (seconds, 3) });
            args.addArray ({ "-shortest", "-movflags", "+faststart", outMp4.getFullPathName() });

            error.clear();
            if (run (args, seconds, progress, error) && outMp4.getSize() > 1024)
                return true;
            if (error == "Cancelled")
                return false;
        }

        if (! fade) break;
    }

    if (error.isEmpty()) error = "The video could not be made.";
    return false;
}

double bestClipStart (const juce::File& audio, double clipSeconds)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    auto reader = std::unique_ptr<juce::AudioFormatReader> (formats.createReaderFor (audio));
    if (reader == nullptr || reader->sampleRate <= 0.0) return 0.0;

    const int perSecond = (int) reader->sampleRate;
    const int totalSeconds = (int) (reader->lengthInSamples / perSecond);
    const int window = (int) std::ceil (clipSeconds);
    if (totalSeconds <= window) return 0.0;

    // how much sound there is in each second of the beat
    juce::Array<double> energy;
    juce::AudioBuffer<float> buffer ((int) reader->numChannels, perSecond);
    for (int second = 0; second < totalSeconds; ++second)
    {
        reader->read (&buffer, 0, perSecond, (juce::int64) second * perSecond, true, true);
        double sum = 0.0;
        for (int c = 0; c < buffer.getNumChannels(); ++c)
        {
            const double rms = buffer.getRMSLevel (c, 0, perSecond);
            sum += rms * rms;
        }
        energy.add (sum);
    }

    double running = 0.0, best = -1.0;
    int bestStart = 0;
    for (int second = 0; second < totalSeconds; ++second)
    {
        running += energy[second];
        if (second >= window) running -= energy[second - window];
        // the first stretch that is clearly the fullest wins, so the clip starts where the beat first comes in fully
        if (second >= window - 1 && running > best * 1.03)
        {
            best = running;
            bestStart = second - window + 1;
        }
    }
    return (double) bestStart;
}

bool encodeMp3 (const juce::File& audio, const juce::File& outMp3, double seconds, Progress progress, juce::String& error)
{
    if (! hasEncoder ("libmp3lame")) { error = "This build cannot make MP3s."; return false; }
    outMp3.deleteFile();
    return run ({ "-hide_banner", "-y", "-i", audio.getFullPathName(), "-c:a", "libmp3lame", "-b:a", "320k", outMp3.getFullPathName() },
                seconds, progress, error);
}

double lengthInSeconds (const juce::File& audio)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    if (auto reader = std::unique_ptr<juce::AudioFormatReader> (formats.createReaderFor (audio)))
        return reader->sampleRate > 0.0 ? (double) reader->lengthInSamples / reader->sampleRate : 0.0;
    return 0.0;
}
}
