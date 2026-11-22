#pragma once
#include "Library.h"
#include "Settings.h"

namespace sd
{
struct Contact;

/** The words the plugin writes for you: YouTube title, description and tags, and the emails. */
namespace texts
{
    juce::String youtubeTitle (const Beat&);
    juce::String youtubeDescription (const Beat&, const Settings&);
    juce::StringArray youtubeTags (const Beat&);
    juce::String shortTitle (const Beat&);
    juce::String shortDescription (const Beat&, const Settings&, const juce::String& fullVideoUrl);
    juce::String licenceName (const Beat&);                 // "FREE FOR PROFIT" / "FREE"
    juce::String licenceSummary (bool freeForProfit);       // one line, shown under the switch
    juce::String buyLink (const Beat&, const Settings&);
    juce::String listenLink (const Beat&, const Settings&);
    juce::String priceText (double price);
    juce::String lengthText (double seconds);

    juce::String emailSubject (const Beat&, const Settings&, int style);
    juce::String emailBody (const Beat&, const Settings&, const juce::String& greetingName, const juce::String& company, int style);
    juce::StringArray emailStyles();
}
}
