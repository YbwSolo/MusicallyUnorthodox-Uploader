#pragma once
#include "Library.h"
#include <juce_graphics/juce_graphics.h>

namespace sd
{
using Progress = std::function<bool (double fraction)>;         // return false to cancel

/** The video and MP3 work, done by the ffmpeg that is installed next to the plugin. */
namespace media
{
    juce::File findFFmpeg();
    bool available();

    /** The words that go on the video picture. */
    struct FrameText
    {
        juce::String title, subtitle, label, credit;        // "Get It", "Gunna type beat", "FREE FOR PROFIT", "prod. JP"
        juce::StringArray chips;                            // "161 BPM", "E MINOR"
    };

    /** The video picture, and where the moving parts are drawn over it: the bars that follow the music and the
        line that fills up as the beat plays. Its accent colour is taken from the cover so every video looks its own. */
    struct Frame
    {
        juce::Image image;
        juce::Rectangle<int> bars;
        int bands = 0, progressY = 0, progressHeight = 8;
        juce::Colour accent;
    };

    /** 1920 x 1080 for the YouTube video, or 1080 x 1920 (tall) for Shorts, TikTok and Reels. */
    Frame composeFrame (const juce::Image& cover, const FrameText&, bool tall = false);
    juce::Colour accentOf (const juce::Image& cover);

    /** Square artwork for stores (BeatStars wants a square), cropped from the middle of the cover. */
    juce::Image squareCover (const juce::Image& cover, int size);

    /** Makes the video: the picture with bars moving to the music and a line showing how far the beat has played.
        If this ffmpeg cannot do the moving parts, the video is made with the still picture instead.
        startAt and fadeOut are for clips: begin that far into the beat, and fade the last moments out. */
    bool renderVideo (const Frame&, const juce::File& audio, const juce::File& outMp4, double seconds,
                      Progress progress, juce::String& error, double startAt = 0.0, double fadeOut = 0.0);

    /** Where the loudest, fullest stretch of that length begins: usually the hook, which is what a short clip should show. */
    double bestClipStart (const juce::File& audio, double clipSeconds);
    bool encodeMp3 (const juce::File& audio, const juce::File& outMp3, double seconds, Progress progress, juce::String& error);

    double lengthInSeconds (const juce::File& audio);
}
}
