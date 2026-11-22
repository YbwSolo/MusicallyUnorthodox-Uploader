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

    /** The picture shown for the whole video: the cover on a blurred copy of itself, with the title.
        1920 x 1080 for the YouTube video, or 1080 x 1920 (tall) for Shorts, TikTok and Reels. */
    juce::Image composeFrame (const juce::Image& cover, const juce::String& title, const juce::String& subtitle, bool tall = false);

    /** Square artwork for stores (BeatStars wants a square), cropped from the middle of the cover. */
    juce::Image squareCover (const juce::Image& cover, int size);

    /** startAt and fadeOut are for clips: begin that far into the beat, and fade the last moments out. */
    bool renderVideo (const juce::File& frame, const juce::File& audio, const juce::File& outMp4, double seconds,
                      Progress progress, juce::String& error, double startAt = 0.0, double fadeOut = 0.0);

    /** Where the loudest, fullest stretch of that length begins: usually the hook, which is what a short clip should show. */
    double bestClipStart (const juce::File& audio, double clipSeconds);
    bool encodeMp3 (const juce::File& audio, const juce::File& outMp3, double seconds, Progress progress, juce::String& error);

    double lengthInSeconds (const juce::File& audio);
}
}
