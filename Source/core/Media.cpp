#include "Media.h"
#include "Settings.h"
#include "../ui/Theme.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <complex>
#include <vector>

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

juce::Colour accentOf (const juce::Image& cover)
{
    if (! cover.isValid()) return theme::yellow;

    // the strongest colour in the cover: pixels vote for their hue, and vivid, bright ones count for more
    const auto small = cover.rescaled (28, 28, juce::Graphics::mediumResamplingQuality);
    const int buckets = 18;
    double weight[18] = {}, hueX[18] = {}, hueY[18] = {}, sat[18] = {};
    double total = 0.0;

    for (int y = 0; y < small.getHeight(); ++y)
        for (int x = 0; x < small.getWidth(); ++x)
        {
            const auto c = small.getPixelAt (x, y);
            const double s = c.getSaturation(), v = c.getBrightness();
            const double w = s * s * (0.25 + v);
            const int b = juce::jlimit (0, buckets - 1, (int) (c.getHue() * buckets));
            const double angle = c.getHue() * juce::MathConstants<double>::twoPi;
            weight[b] += w; hueX[b] += std::cos (angle) * w; hueY[b] += std::sin (angle) * w; sat[b] += s * w;
            total += w;
        }

    int best = 0;
    for (int b = 1; b < buckets; ++b) if (weight[b] > weight[best]) best = b;
    if (total < 20.0 || weight[best] <= 0.0)
        return theme::yellow;                       // a black and white or washed-out cover: use the house yellow

    double hue = std::atan2 (hueY[best], hueX[best]) / juce::MathConstants<double>::twoPi;
    if (hue < 0.0) hue += 1.0;
    const float saturation = (float) juce::jlimit (0.55, 0.85, sat[best] / weight[best]);
    return juce::Colour::fromHSV ((float) hue, saturation, 1.0f, 1.0f);
}

static void drawChip (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r)
{
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (juce::Colours::white.withAlpha (0.38f));
    g.drawRoundedRectangle (r.reduced (1.0f), r.getHeight() * 0.5f - 1.0f, 2.0f);
    g.setColour (juce::Colours::white);
    g.drawText (text, r, juce::Justification::centred);
}

Frame composeFrame (const juce::Image& cover, const FrameText& text, bool tall)
{
    Frame frame;
    const int w = tall ? 1080 : 1920, h = tall ? 1920 : 1080;
    frame.image = juce::Image (juce::Image::RGB, w, h, true);
    frame.accent = accentOf (cover);
    juce::Graphics g (frame.image);
    g.fillAll (juce::Colours::black);
    const auto accent = frame.accent;

    // background: the cover blurred right out and darkened, with the accent colour glowing up from the bottom
    if (cover.isValid())
    {
        const double scale = juce::jmax ((double) w / cover.getWidth(), (double) h / cover.getHeight());
        const int cw = juce::roundToInt (w / scale), ch = juce::roundToInt (h / scale);
        auto middle = cover.getClippedImage ({ (cover.getWidth() - cw) / 2, (cover.getHeight() - ch) / 2, cw, ch });
        g.setOpacity (1.0f);
        g.drawImageAt (blurred (middle, w, h), 0, 0);
        g.fillAll (juce::Colours::black.withAlpha (0.58f));
    }
    else
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff161616), 0, 0, juce::Colours::black, 0, (float) h, false));
        g.fillAll();
    }

    g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.34f), (float) w * 0.5f, (float) h * 1.12f,
                                             accent.withAlpha (0.0f), (float) w * 0.5f, (float) h * (tall ? 0.62f : 0.45f), true));
    g.fillAll();
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.45f), 0, 0, juce::Colours::transparentBlack, 0, (float) h * 0.3f, false));
    g.fillAll();

    // the cover itself, cropped square
    const auto art = tall ? juce::Rectangle<float> (130.0f, 210.0f, 820.0f, 820.0f) : juce::Rectangle<float> (120.0f, 110.0f, 690.0f, 690.0f);
    juce::DropShadow (juce::Colours::black.withAlpha (0.7f), 70, { 0, 30 }).drawForRectangle (g, art.toNearestInt());
    juce::Path artShape;
    artShape.addRoundedRectangle (art, 30.0f);
    g.saveState();
    g.reduceClipRegion (artShape);
    if (cover.isValid())
    {
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.setOpacity (1.0f);
        g.drawImage (cover, art, juce::RectanglePlacement::fillDestination);
    }
    else
    {
        g.setGradientFill (juce::ColourGradient (accent.withMultipliedBrightness (0.55f), art.getX(), art.getY(),
                                                 juce::Colour (0xff0c0c0c), art.getRight(), art.getBottom(), false));
        g.fillRect (art);
        g.setColour (juce::Colours::white.withAlpha (0.92f));
        g.setFont (theme::headline (art.getHeight() * 0.62f));
        g.drawText (text.title.trim().substring (0, 1).toUpperCase(), art, juce::Justification::centred);
    }
    g.restoreState();
    g.setColour (juce::Colours::white.withAlpha (0.16f));
    g.strokePath (artShape, juce::PathStrokeType (3.0f));

    // the words: beside the cover on a wide video, under it on a tall one
    const auto column = tall ? juce::Rectangle<int> (90, 1090, 900, 380) : juce::Rectangle<int> (890, 110, 920, 690);
    const auto side = tall ? juce::Justification::centred : juce::Justification::centredLeft;
    const auto title = text.title.trim().toUpperCase();
    const float titleSize = tall ? (title.length() <= 12 ? 120.0f : 92.0f)
                                 : (title.length() <= 9 ? 190.0f : title.length() <= 16 ? 150.0f : 118.0f);
    const int titleLines = tall ? 1 : (title.length() > 12 && title.containsChar (' ') ? 2 : 1);
    const int titleHeight = (int) (titleSize * 1.12f) * titleLines;
    const int labelHeight = text.label.isNotEmpty() ? 58 : 0;
    const int subtitleHeight = text.subtitle.isNotEmpty() ? (tall ? 64 : 72) : 0;
    const int chipsHeight = text.chips.isEmpty() ? 0 : 92;
    const int creditHeight = text.credit.isNotEmpty() ? 56 : 0;
    const int block = labelHeight + titleHeight + subtitleHeight + chipsHeight + creditHeight;
    int y = tall ? column.getY() : column.getY() + (column.getHeight() - block) / 2;

    if (labelHeight > 0)
    {
        g.setColour (accent);
        g.setFont (theme::bodyBold (tall ? 30.0f : 32.0f).withExtraKerningFactor (0.14f));
        g.drawText (text.label.toUpperCase(), column.withY (y).withHeight (44), side);
        y += labelHeight;
    }

    g.setColour (juce::Colours::white);
    g.setFont (theme::headline (titleSize));
    g.drawFittedText (title, column.withY (y).withHeight (titleHeight), side, titleLines, 0.6f);
    y += titleHeight;

    if (subtitleHeight > 0)
    {
        g.setColour (juce::Colours::white.withAlpha (0.88f));
        g.setFont (theme::bodyBold (tall ? 42.0f : 48.0f));
        g.drawFittedText (text.subtitle, column.withY (y).withHeight (subtitleHeight - 8), side, 1, 0.7f);
        y += subtitleHeight;
    }

    if (chipsHeight > 0)
    {
        const auto font = theme::bodyBold (30.0f).withExtraKerningFactor (0.06f);
        g.setFont (font);
        juce::Array<float> widths;
        float total = 0.0f;
        for (auto& chip : text.chips)
        {
            widths.add (juce::GlyphArrangement::getStringWidth (font, chip) + 56.0f);
            total += widths.getLast() + 16.0f;
        }
        float x = tall ? (float) column.getCentreX() - (total - 16.0f) * 0.5f : (float) column.getX();
        for (int i = 0; i < text.chips.size(); ++i)
        {
            drawChip (g, text.chips[i], { x, (float) y + 22.0f, widths[i], 58.0f });
            x += widths[i] + 16.0f;
        }
        y += chipsHeight;
    }

    if (creditHeight > 0)
    {
        g.setColour (juce::Colours::white.withAlpha (0.62f));
        g.setFont (theme::body (34.0f));
        g.drawText (text.credit, column.withY (y + 10).withHeight (44), side);
    }

    // where ffmpeg draws the moving parts: the bars that follow the music, and the line that fills as it plays.
    // On a tall video everything stays clear of the bottom, where the phone apps put their own buttons.
    frame.bands = tall ? 50 : 70;
    const int barScale = tall ? 6 : 8;
    const int barsWidth = frame.bands * 3 * barScale - barScale;
    frame.bars = { (w - barsWidth) / 2, tall ? 1500 : 858, frame.bands * 3 * barScale, 160 };
    frame.progressHeight = 8;
    frame.progressY = tall ? 1690 : h - frame.progressHeight;
    g.setColour (juce::Colours::white.withAlpha (0.14f));
    g.fillRect (0, frame.progressY, w, frame.progressHeight);
    return frame;
}

juce::Image squareCover (const juce::Image& cover, int size)
{
    if (! cover.isValid()) return {};
    const int side = juce::jmin (cover.getWidth(), cover.getHeight());
    auto middle = cover.getClippedImage ({ (cover.getWidth() - side) / 2, (cover.getHeight() - side) / 2, side, side });
    return middle.rescaled (size, size, juce::Graphics::highResamplingQuality);
}

/** A plain in-place FFT, enough for drawing bars. The length must be a power of two. */
static void fft (std::vector<std::complex<float>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const float angle = -juce::MathConstants<float>::twoPi / (float) len;
        const std::complex<float> step (std::cos (angle), std::sin (angle));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<float> w (1.0f, 0.0f);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= step;
            }
        }
    }
}

static const int kBarRows = 40, kFramesPerSecond = 30;

/** Listens to the beat and writes one small picture of the bars for every frame of the video: lows on the left,
    highs on the right, each bar jumping up with the sound and falling back smoothly. ffmpeg scales it up and colours it. */
static bool writeBars (const juce::File& audio, double startAt, double seconds, int bands, const juce::File& out)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    auto reader = std::unique_ptr<juce::AudioFormatReader> (formats.createReaderFor (audio));
    if (reader == nullptr || reader->sampleRate <= 0.0 || bands <= 0 || seconds <= 0.0) return false;

    const double rate = reader->sampleRate;
    const auto first = (juce::int64) (startAt * rate);
    const int length = (int) juce::jmin ((juce::int64) (seconds * rate) + 4096, reader->lengthInSamples - first);
    if (length <= 4096) return false;

    juce::AudioBuffer<float> sound ((int) reader->numChannels, length);
    reader->read (&sound, 0, length, first, true, true);
    std::vector<float> mono ((size_t) length, 0.0f);
    for (int c = 0; c < sound.getNumChannels(); ++c)
        for (int i = 0; i < length; ++i)
            mono[(size_t) i] += sound.getSample (c, i) / (float) sound.getNumChannels();

    const int size = 2048, frames = (int) std::ceil (seconds * kFramesPerSecond);
    std::vector<float> window ((size_t) size);
    for (int i = 0; i < size; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (size - 1));

    // band edges spaced like the ear hears, from the sub up to the air
    std::vector<double> edge ((size_t) bands + 1);
    for (int b = 0; b <= bands; ++b)
        edge[(size_t) b] = 38.0 * std::pow (13000.0 / 38.0, (double) b / (double) bands);

    std::vector<float> levels ((size_t) frames * (size_t) bands, 0.0f);
    std::vector<std::complex<float>> spectrum ((size_t) size);
    for (int f = 0; f < frames; ++f)
    {
        const auto at = (juce::int64) ((double) f * rate / kFramesPerSecond);
        for (int i = 0; i < size; ++i)
        {
            const auto index = at + i;
            spectrum[(size_t) i] = { index < length ? mono[(size_t) index] * window[(size_t) i] : 0.0f, 0.0f };
        }
        fft (spectrum);

        for (int b = 0; b < bands; ++b)
        {
            const double lo = edge[(size_t) b] * size / rate, hi = edge[(size_t) b + 1] * size / rate;
            const int from = juce::jlimit (1, size / 2 - 1, (int) std::floor (lo));
            const int to = juce::jlimit (from, size / 2 - 1, (int) std::ceil (hi) - 1);
            float peak = 0.0f;
            for (int k = from; k <= to; ++k) peak = juce::jmax (peak, std::abs (spectrum[(size_t) k]));
            // music has far more energy down low, so the highs are lifted to make the whole row move
            levels[(size_t) f * (size_t) bands + (size_t) b] = peak * (float) std::pow (edge[(size_t) b] / 38.0, 0.30);
        }
    }

    // scaled so the loudest moments of this beat nearly fill the height, whatever level it was mixed at
    auto sorted = levels;
    const auto nth = sorted.begin() + (std::ptrdiff_t) ((double) sorted.size() * 0.985);
    std::nth_element (sorted.begin(), nth, sorted.end());
    const float reference = juce::jmax (1.0e-6f, *nth);

    juce::FileOutputStream stream (out);
    if (! stream.openedOk()) return false;
    stream.setPosition (0);
    stream.truncate();

    const int width = bands * 3;
    std::vector<float> shown ((size_t) bands, 0.0f);
    std::vector<juce::uint8> picture ((size_t) width * (size_t) kBarRows);
    for (int f = 0; f < frames; ++f)
    {
        std::fill (picture.begin(), picture.end(), (juce::uint8) 0);
        for (int b = 0; b < bands; ++b)
        {
            const float target = juce::jlimit (0.0f, 1.0f, std::sqrt (levels[(size_t) f * (size_t) bands + (size_t) b] / reference) * 0.95f);
            shown[(size_t) b] = juce::jmax (target, shown[(size_t) b] * 0.80f);
            const int half = juce::jlimit (1, kBarRows / 2, juce::roundToInt (shown[(size_t) b] * (float) (kBarRows / 2)));
            for (int row = kBarRows / 2 - half; row < kBarRows / 2 + half; ++row)
            {
                picture[(size_t) row * (size_t) width + (size_t) b * 3] = 255;
                picture[(size_t) row * (size_t) width + (size_t) b * 3 + 1] = 255;
            }
        }
        if (! stream.write (picture.data(), picture.size())) return false;
    }
    stream.flush();
    return true;
}

static juce::String hex (juce::Colour c) { return "0x" + c.toDisplayString (false); }

bool renderVideo (const Frame& frame, const juce::File& audio, const juce::File& outMp4, double seconds,
                  Progress progress, juce::String& error, double startAt, double fadeOut)
{
    const auto picture = outMp4.getSiblingFile (outMp4.getFileNameWithoutExtension() + "-picture.png");
    const auto barsFile = outMp4.getSiblingFile (outMp4.getFileNameWithoutExtension() + "-bars.raw");
    struct Cleanup { juce::File a, b; ~Cleanup() { a.deleteFile(); b.deleteFile(); } } cleanup { picture, barsFile };

    picture.deleteFile();
    {
        juce::PNGImageFormat png;
        auto stream = picture.createOutputStream();
        if (stream == nullptr || ! png.writeImageToStream (frame.image, *stream)) { error = "Could not write the video picture."; return false; }
    }

    // whichever H.264 encoder this ffmpeg has, best first; MPEG-4 always exists and YouTube accepts it too.
    // Apple's encoder is handed its own pixel layout (nv12) directly.
    struct Encoder { juce::StringArray args; juce::String pixels; };
    juce::Array<Encoder> encoders;
    if (hasEncoder ("libx264"))             encoders.add ({ { "-c:v", "libx264", "-preset", "veryfast", "-crf", "20" }, "yuv420p" });
    if (hasEncoder ("h264_videotoolbox"))   encoders.add ({ { "-c:v", "h264_videotoolbox", "-allow_sw", "1", "-b:v", "4500k" }, "nv12" });
    if (hasEncoder ("h264_mf"))             encoders.add ({ { "-c:v", "h264_mf", "-b:v", "4500k" }, "nv12" });
    if (hasEncoder ("libopenh264"))         encoders.add ({ { "-c:v", "libopenh264", "-b:v", "4500k" }, "yuv420p" });
    encoders.add ({ { "-c:v", "mpeg4", "-q:v", "4" }, "yuv420p" });

    const bool wantsFade = fadeOut > 0.0 && seconds > fadeOut * 2.0;
    const auto fade = "afade=t=out:st=" + juce::String (seconds - fadeOut, 3) + ":d=" + juce::String (fadeOut, 3);
    const bool hasBars = seconds > 0.0 && frame.bands > 0 && writeBars (audio, startAt, seconds, frame.bands, barsFile);

    // First choice is the moving video. If this ffmpeg cannot make it (an older one left by an earlier install),
    // the still picture is used, and the fade is dropped last of all: a plain video beats no video.
    struct Attempt { bool moving, fade; };
    juce::Array<Attempt> attempts;
    if (hasBars) attempts.add ({ true, wantsFade });
    attempts.add ({ false, wantsFade });
    if (wantsFade) attempts.add ({ false, false });

    for (auto& attempt : attempts)
    {
        for (auto& encoder : encoders)
        {
            outMp4.deleteFile();
            const int rate = attempt.moving ? kFramesPerSecond : 2;
            juce::StringArray args { "-hide_banner", "-y", "-loop", "1", "-framerate", juce::String (rate), "-i", picture.getFullPathName() };
            if (startAt > 0.0) args.addArray ({ "-ss", juce::String (startAt, 3) });
            args.addArray ({ "-i", audio.getFullPathName() });

            if (attempt.moving)
            {
                const auto colour = hex (frame.accent);
                const auto size = juce::String (frame.bars.getWidth()) + "x" + juce::String (frame.bars.getHeight());
                args.addArray ({ "-f", "rawvideo", "-pixel_format", "gray", "-video_size",
                                 juce::String (frame.bands * 3) + "x" + juce::String (kBarRows), "-framerate", juce::String (rate),
                                 "-i", barsFile.getFullPathName() });
                juce::String graph;
                graph << "[2:v]scale=" << frame.bars.getWidth() << ":" << frame.bars.getHeight() << ":flags=neighbor[shape];"
                      << "color=c=" << colour << ":s=" << size << ":r=" << rate << ",format=rgba[paint];"
                      << "[paint][shape]alphamerge[bars];"
                      << "[0:v][bars]overlay=x=" << frame.bars.getX() << ":y=" << frame.bars.getY() << ":shortest=1[withbars];"
                      << "color=c=" << colour << ":s=" << frame.image.getWidth() << "x" << frame.progressHeight << ":r=" << rate << "[line];"
                      << "[withbars][line]overlay=x='-w+w*t/" << juce::String (seconds, 3) << "':y=" << frame.progressY
                      << ",format=" << encoder.pixels << "[v]";
                args.addArray ({ "-filter_complex", graph, "-map", "[v]", "-map", "1:a" });
            }
            else
            {
                args.addArray ({ "-map", "0:v", "-map", "1:a", "-pix_fmt", encoder.pixels });
            }

            args.addArray (encoder.args);
            args.addArray ({ "-r", juce::String (rate), "-c:a", "aac", "-b:a", "320k", "-ar", "48000" });
            if (attempt.fade) args.addArray ({ "-af", fade });
            // the length is given outright: some encoders hold frames back, and "stop with the audio" alone
            // then leaves seconds of silent picture on the end
            if (seconds > 0.0) args.addArray ({ "-t", juce::String (seconds, 3) });
            args.addArray ({ "-movflags", "+faststart", outMp4.getFullPathName() });

            error.clear();
            if (run (args, seconds, progress, error) && outMp4.getSize() > 1024)
                return true;
            if (error == "Cancelled")
                return false;
        }
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
