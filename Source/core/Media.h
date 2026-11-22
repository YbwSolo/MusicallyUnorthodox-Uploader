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

    /** The 1920x1080 picture shown for the whole video: the cover on a blurred copy of itself, with the title. */
    juce::Image composeFrame (const juce::Image& cover, const juce::String& title, const juce::String& subtitle);

    /** Square artwork for stores (BeatStars wants a square), cropped from the middle of the cover. */
    juce::Image squareCover (const juce::Image& cover, int size);

    bool renderVideo (const juce::File& frame, const juce::File& audio, const juce::File& outMp4, double seconds,
                      Progress progress, juce::String& error);
    bool encodeMp3 (const juce::File& audio, const juce::File& outMp3, double seconds, Progress progress, juce::String& error);

    double lengthInSeconds (const juce::File& audio);
}
}
