#pragma once
#include "GoogleAuth.h"
#include "Media.h"

namespace sd
{
struct YouTubeRequest
{
    juce::String title, description;
    juce::StringArray tags;
    juce::String privacy { "public" };          // public, unlisted, private
    juce::Time publishAt;                       // set (in the future) to schedule; the video stays private until then
};

namespace youtube
{
    /** Uploads the video. Returns the video id, or an empty string with the reason in error. */
    juce::String upload (GoogleAuth&, const juce::File& video, const YouTubeRequest&, Progress, juce::String& error);

    /** View counts for videos already uploaded: id -> views. */
    juce::StringPairArray viewCounts (GoogleAuth&, const juce::StringArray& videoIds);
}
}
