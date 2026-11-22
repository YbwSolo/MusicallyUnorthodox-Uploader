#include "Texts.h"

namespace sd::texts
{
static juce::String producer (const Settings& s)  { return s.get ("producer").trim(); }
static juce::String year()                         { return juce::String (juce::Time::getCurrentTime().getYear()); }
static juce::String clean (juce::String s)         { return s.replaceCharacters ("<>", "()").trim(); }      // YouTube refuses < and >

juce::String priceText (double price)
{
    return "$" + (std::abs (price - std::round (price)) < 0.005 ? juce::String ((int) std::round (price)) : juce::String (price, 2));
}

juce::String lengthText (double seconds)
{
    const int total = (int) std::round (seconds);
    return juce::String (total / 60) + ":" + juce::String (total % 60).paddedLeft ('0', 2);
}

juce::String licenceName (const Beat& b) { return b.freeForProfit() ? "FREE FOR PROFIT" : "FREE"; }

juce::String licenceSummary (bool freeForProfit)
{
    return freeForProfit ? "Artists can release on every platform with credit. They cannot claim Content ID."
                         : "Free to record on. To release it on streaming platforms they have to buy a lease.";
}

juce::String buyLink (const Beat& b, const Settings& s)
{
    return b.beatstarsUrl.trim().isNotEmpty() ? b.beatstarsUrl.trim() : s.get ("storeLink").trim();
}

juce::String listenLink (const Beat& b, const Settings& s)
{
    return b.youtubeUrl.isNotEmpty() ? b.youtubeUrl : buyLink (b, s);
}

juce::String youtubeTitle (const Beat& b)
{
    const auto name = b.title.trim().isEmpty() ? juce::String ("Untitled") : b.title.trim();
    auto title = "[" + licenceName (b) + "] " + (b.artist.trim().isNotEmpty() ? b.artist.trim() + " Type Beat " : juce::String ("Type Beat "))
               + year() + " - \"" + name + "\"";
    return clean (title).substring (0, 100);
}

juce::String youtubeDescription (const Beat& b, const Settings& s)
{
    const auto who = producer (s);
    const auto credit = who.isNotEmpty() ? "prod. " + who : juce::String ("the producer");
    const auto link = buyLink (b, s);
    juce::StringArray lines;

    lines.add ("\"" + b.title.trim() + "\"" + (who.isNotEmpty() ? " (prod. " + who + ")" : juce::String()));
    if (link.isNotEmpty())
        lines.add ("Untagged download and leases from " + priceText (b.price) + ": " + link);
    lines.add ({});

    juce::StringArray facts;
    if (b.bpm > 0)                  facts.add ("BPM: " + juce::String (juce::roundToInt (b.bpm)));
    if (b.key.trim().isNotEmpty())  facts.add ("Key: " + b.key.trim());
    if (! facts.isEmpty())          { lines.add (facts.joinIntoString ("  |  ")); lines.add ({}); }

    if (b.freeForProfit())
    {
        lines.add ("FREE FOR PROFIT");
        lines.add ("You can record on this beat and release your song on every platform (Spotify, Apple Music, YouTube, SoundCloud and the rest) without paying, as long as you:");
        lines.add ("- credit " + credit + " in the title of your song");
        lines.add ("- do not register the beat, or a song that uses it, with Content ID, and do not claim the instrumental as your own");
        if (link.isNotEmpty())
            lines.add ("If you want it untagged, as a WAV or with stems, or your distributor asks for a licence, lease it here: " + link);
    }
    else
    {
        lines.add ("FREE FOR NON-PROFIT USE");
        lines.add ("You can download this beat and record on it for free, and post the song where it earns nothing.");
        lines.add (link.isNotEmpty() ? "To release it on Spotify, Apple Music or any other platform, or to earn from it anywhere, you need a lease: " + link
                                     : "To release it on Spotify, Apple Music or any other platform, or to earn from it anywhere, you need a lease. Message me to get one.");
        lines.add ("Credit " + credit + " in the title. Do not claim Content ID.");
    }

    if (b.description.trim().isNotEmpty()) { lines.add ({}); lines.add (b.description.trim()); }

    juce::StringArray contact;
    if (s.get ("email").trim().isNotEmpty())      contact.add ("Email: " + s.get ("email").trim());
    if (s.get ("instagram").trim().isNotEmpty())  contact.add ("Instagram: @" + s.get ("instagram").trim().trimCharactersAtStart ("@"));
    if (! contact.isEmpty()) { lines.add ({}); lines.addArray (contact); }

    lines.add ({});
    lines.add (youtubeTitle (b));
    juce::StringArray hashtags;
    for (auto& t : youtubeTags (b))
    {
        if (hashtags.size() >= 3) break;
        hashtags.add ("#" + t.removeCharacters (" -'\"."));
    }
    lines.add (hashtags.joinIntoString (" "));
    return clean (lines.joinIntoString ("\n")).substring (0, 4900);
}

juce::String shortTitle (const Beat& b)
{
    const auto name = b.title.trim().isEmpty() ? juce::String ("Untitled") : b.title.trim();
    const auto tail = juce::String (" #shorts #typebeat");
    auto title = "\"" + name + "\" " + (b.artist.trim().isNotEmpty() ? b.artist.trim() + " Type Beat" : juce::String ("Type Beat"));
    return clean (title).substring (0, 100 - tail.length()) + tail;
}

juce::String shortDescription (const Beat& b, const Settings& s, const juce::String& fullVideoUrl)
{
    const auto who = producer (s);
    const auto link = buyLink (b, s);
    juce::StringArray lines;
    lines.add ("\"" + b.title.trim() + "\"" + (who.isNotEmpty() ? " (prod. " + who + ")" : juce::String()));
    if (fullVideoUrl.isNotEmpty()) lines.add ("Full beat: " + fullVideoUrl);
    if (link.isNotEmpty())         lines.add ("Download and leases: " + link);
    lines.add (b.freeForProfit() ? "Free for profit: credit " + (who.isNotEmpty() ? "prod. " + who : juce::String ("the producer")) + " and do not claim Content ID."
                                 : juce::String ("Free for non-profit use. A lease is needed to release it."));
    return clean (lines.joinIntoString ("\n"));
}

juce::StringArray youtubeTags (const Beat& b)
{
    juce::StringArray tags;
    const auto artist = b.artist.trim().toLowerCase();
    if (artist.isNotEmpty())
    {
        tags.add (artist + " type beat");
        tags.add (artist + " type beat " + year());
        tags.add ("free " + artist + " type beat");
        tags.add (artist);
    }

    for (auto& t : juce::StringArray::fromTokens (b.tags, ",;\n", {}))
        if (t.trim().isNotEmpty())
            tags.add (t.trim().toLowerCase());

    tags.add ("type beat");
    tags.add (b.freeForProfit() ? "free for profit beat" : "free beat");
    tags.add ("type beat " + year());
    tags.add ("instrumental");
    if (b.bpm > 0) tags.add (juce::String (juce::roundToInt (b.bpm)) + " bpm");
    tags.removeDuplicates (true);

    // YouTube allows 500 characters of tags in total
    juce::StringArray kept;
    int used = 0;
    for (auto& t : tags)
    {
        const auto tag = clean (t).removeCharacters (",");
        if (used + tag.length() + 3 > 480) break;
        kept.add (tag);
        used += tag.length() + 3;
    }
    return kept;
}

juce::StringArray emailStyles() { return { "Label or A&R", "Artist", "Manager or publisher" }; }

juce::String emailSubject (const Beat& b, const Settings& s, int style)
{
    const auto who = producer (s).isNotEmpty() ? producer (s) : juce::String ("a producer");
    const auto sound = b.artist.trim().isNotEmpty() ? b.artist.trim() + " type " : juce::String();
    if (style == 1) return "Beat for you: \"" + b.title.trim() + "\" (" + sound + "beat, prod. " + who + ")";
    if (style == 2) return "Producer submission: " + who + " - \"" + b.title.trim() + "\"";
    return "Beat submission: \"" + b.title.trim() + "\" - prod. " + who;
}

juce::String emailBody (const Beat& b, const Settings& s, const juce::String& greetingName, const juce::String& company, int style)
{
    const auto who = producer (s);
    const auto link = listenLink (b, s);
    juce::StringArray facts;
    if (b.bpm > 0)                 facts.add (juce::String (juce::roundToInt (b.bpm)) + " BPM");
    if (b.key.trim().isNotEmpty()) facts.add (b.key.trim());
    const auto detail = facts.isEmpty() ? juce::String() : " (" + facts.joinIntoString (", ") + ")";
    const auto sound = b.artist.trim().isNotEmpty() ? " in the lane of " + b.artist.trim() : juce::String();
    const auto team = company.trim().isNotEmpty() ? company.trim() : juce::String ("your roster");

    juce::StringArray p;
    p.add ("Hi " + (greetingName.trim().isNotEmpty() ? greetingName.trim() : (company.trim().isNotEmpty() ? company.trim() + " team" : juce::String ("there"))) + ",");
    p.add ({});

    if (style == 1)
    {
        p.add ("I'm " + (who.isNotEmpty() ? who : juce::String ("a producer")) + ". I made a beat I think fits your sound: \"" + b.title.trim() + "\"" + detail + ".");
        p.add ({});
        p.add ("Listen: " + (link.isNotEmpty() ? link : juce::String ("[add your link]")));
        p.add ({});
        p.add ("If you want to record on it I can send the untagged WAV and stems today, and I'm happy to change the arrangement around your verses.");
    }
    else if (style == 2)
    {
        p.add ("I'm " + (who.isNotEmpty() ? who : juce::String ("a producer")) + ", and I'm looking for placements for my catalogue. One recent beat" + sound + ": \"" + b.title.trim() + "\"" + detail + ".");
        p.add ({});
        p.add ("Listen: " + (link.isNotEmpty() ? link : juce::String ("[add your link]")));
        p.add ({});
        p.add ("It is fully my own work with no uncleared samples, and the stems are ready. If it suits anyone you work with, I can send more in the same style.");
    }
    else
    {
        p.add ("My name is " + (who.isNotEmpty() ? who : juce::String ("[your name]")) + ", a producer. I'm sending one beat" + sound + " that I think could work for " + team + ": \"" + b.title.trim() + "\"" + detail + ".");
        p.add ({});
        p.add ("Listen: " + (link.isNotEmpty() ? link : juce::String ("[add your link]")));
        p.add ({});
        p.add ("It is available exclusively, with no uncleared samples, and I can send the WAV and stems straight away. If this one is not right, tell me what you are looking for and I will send something closer.");
    }

    p.add ({});
    p.add ("Thanks for listening,");
    p.add (who.isNotEmpty() ? who : juce::String ("[your name]"));
    if (s.get ("email").trim().isNotEmpty())      p.add (s.get ("email").trim());
    if (s.get ("instagram").trim().isNotEmpty())  p.add ("Instagram: @" + s.get ("instagram").trim().trimCharactersAtStart ("@"));
    return p.joinIntoString ("\n");
}
}
