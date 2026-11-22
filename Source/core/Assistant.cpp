#include "Assistant.h"
#include "Texts.h"
#include <regex>

namespace sd::assistant
{
static bool match (const std::string& text, const char* pattern, std::smatch& m)
{
    return std::regex_search (text, m, std::regex (pattern, std::regex::icase));
}

static juce::String str (const std::ssub_match& s) { return juce::String (s.str()).trim(); }

static juce::String titleCase (const juce::String& s)
{
    juce::StringArray words;
    for (auto& w : juce::StringArray::fromTokens (s, " ", {}))
        if (w.isNotEmpty()) words.add (w.substring (0, 1).toUpperCase() + w.substring (1));
    return words.joinIntoString (" ");
}

/** "tomorrow at 6pm", "friday 18:30", "in 3 hours" -> a time, or a time in the past when nothing was understood. */
static juce::Time parseWhen (const std::string& text)
{
    const auto now = juce::Time::getCurrentTime();
    std::smatch m;

    if (match (text, R"rx(\bin\s+(\d+)\s*(minute|min|hour|hr|day)s?\b)rx", m))
    {
        const int n = std::stoi (m[1].str());
        const auto unit = juce::String (m[2].str()).toLowerCase();
        return now + (unit.startsWith ("min") ? juce::RelativeTime::minutes (n) : unit.startsWith ("day") ? juce::RelativeTime::days (n) : juce::RelativeTime::hours (n));
    }

    int dayOffset = -1;
    static const char* days[] = { "sunday", "monday", "tuesday", "wednesday", "thursday", "friday", "saturday" };
    const auto lower = juce::String (text).toLowerCase();
    if (lower.contains ("tomorrow")) dayOffset = 1;
    else if (lower.contains ("today") || lower.contains ("tonight")) dayOffset = 0;
    else
        for (int d = 0; d < 7; ++d)
            if (lower.contains (days[d]))
            {
                dayOffset = (d - now.getDayOfWeek() + 7) % 7;
                if (dayOffset == 0) dayOffset = 7;
            }

    int hour = -1, minute = 0;
    if (match (text, R"rx(\b(\d{1,2})(?::(\d{2}))?\s*(am|pm)\b)rx", m))
    {
        hour = std::stoi (m[1].str()) % 12;
        if (m[2].matched) minute = std::stoi (m[2].str());
        if (juce::String (m[3].str()).equalsIgnoreCase ("pm")) hour += 12;
    }
    else if (match (text, R"rx(\b(\d{1,2}):(\d{2})\b)rx", m))
    {
        hour = std::stoi (m[1].str());
        minute = std::stoi (m[2].str());
    }
    else if (lower.contains ("tonight")) hour = 20;

    if (dayOffset < 0 && hour < 0) return juce::Time();
    if (hour < 0) hour = 18;                       // a day with no time: early evening
    if (hour > 23 || minute > 59) return juce::Time();

    juce::Time when (now.getYear(), now.getMonth(), now.getDayOfMonth(), hour, minute);
    if (dayOffset > 0) when += juce::RelativeTime::days (dayOffset);
    else if (dayOffset < 0 && when < now) when += juce::RelativeTime::days (1);     // "at 6pm" after 6pm means tomorrow
    return when;
}

AssistantReply understand (const juce::String& request, const Draft& draft, const Library& library)
{
    AssistantReply reply;
    // curly quotes and other currency signs are turned into the plain ones the patterns below look for
    const auto plain = request.trim().replace (juce::String::fromUTF8 ("\xe2\x80\x9c"), "\"").replace (juce::String::fromUTF8 ("\xe2\x80\x9d"), "\"")
                              .replace (juce::String::fromUTF8 ("\xc2\xa3"), "$").replace (juce::String::fromUTF8 ("\xe2\x82\xac"), "$");
    const auto text = plain.toStdString();
    const auto lower = plain.toLowerCase();
    juce::StringArray said;
    std::smatch m;
    auto add = [&] (const juce::String& type, const juce::String& value = {}, double number = 0) { reply.actions.add ({ type, value, number }); };

    if (lower.isEmpty()) return reply;

    if (match (text, R"rx(^\s*(help|what can you do|what do you do)\b)rx", m))
    {
        reply.message = "Tell me about the beat and what to do with it, in one go if you like:\n"
                        "\"Name it Get It, Gunna type beat, 161 bpm, key E minor, $25, free for profit, post it tomorrow at 6pm\"\n\n"
                        "I can set the title, artist, BPM, key, tags, price and licence, pick a catalog, make it public, unlisted, private or scheduled, "
                        "save it, upload it, start a recording, or open the email page for a beat.";
        return reply;
    }

    if (lower.contains ("difference") && lower.contains ("free") && ! lower.contains ("name it"))
    {
        reply.message = "FREE: " + texts::licenceSummary (false) + "\n\nFREE FOR PROFIT: " + texts::licenceSummary (true);
        return reply;
    }

    // title: quoted text wins, otherwise the words after "name it" / "call it" / "title"
    if (match (text, R"rx((?:name|call|title)(?:\s+it|\s+this|\s+the\s+beat)?(?:\s+is|\s+to|\s*:)?\s+"([^"]+)")rx", m)
        || match (text, R"rx((?:name it|call it|named|called|title is|title:|title to|title)\s+([^,.;\n]+?)(?=\s*(?:,|;|\.|\n|$|\band\s+(?:set|make|post|upload|price|put|schedule)\b)))rx", m))
    {
        const auto title = str (m[1]);
        if (title.isNotEmpty()) { add ("setTitle", title); said.add ("Title: \"" + title + "\""); }
    }

    if (match (text, R"rx((?:^|[,.;]|\ba\b|\ban\b|\bas\b|\bit's\b|\bits\b)\s*((?:[A-Za-z0-9$.'&-]+\s){1,4}?)type\s+beat)rx", m))
    {
        auto artist = str (m[1]);
        for (auto* filler : { "free for profit ", "free ", "new ", "hard ", "dark " })
            if (artist.toLowerCase().startsWith (filler)) artist = artist.substring ((int) strlen (filler));
        if (artist.isNotEmpty()) { artist = titleCase (artist); add ("setArtist", artist); said.add ("Artist: " + artist); }
    }

    if (match (text, R"rx([$]\s*(\d+(?:\.\d{1,2})?))rx", m) || match (text, R"rx((\d+(?:\.\d{1,2})?)\s*(?:dollars|bucks|usd|quid|pounds))rx", m)
        || match (text, R"rx(price\s*(?:it\s*)?(?:to|at|of|is|=|:)?\s*(\d+(?:\.\d{1,2})?))rx", m))
    {
        const double price = std::stod (m[1].str());
        add ("setPrice", {}, price);
        said.add ("Price: " + texts::priceText (price));
    }

    if (match (text, R"rx(\b(\d{2,3})\s*bpm\b)rx", m) || match (text, R"rx(\bbpm\s*(?:is|to|of|=|:)?\s*(\d{2,3})\b)rx", m))
    {
        add ("setBpm", {}, std::stod (m[1].str()));
        said.add ("BPM: " + juce::String (m[1].str()));
    }

    if ((lower.contains ("key") || lower.contains (" in ")) && match (text, R"rx(\b([A-Ga-g])\s*(#|sharp|b|flat)?\s*(minor|major|min|maj)\b)rx", m))
    {
        juce::String key = juce::String (m[1].str()).toUpperCase();
        const auto accidental = juce::String (m[2].str()).toLowerCase();
        if (accidental == "#" || accidental == "sharp") key << "#";
        else if (accidental == "b" || accidental == "flat") key << "b";
        key << (juce::String (m[3].str()).toLowerCase().startsWith ("min") ? " Minor" : " Major");
        add ("setKey", key);
        said.add ("Key: " + key);
    }

    if (lower.contains ("free for profit") || match (text, R"rx(\bffp\b)rx", m))
    {
        add ("setLicence", "ffp");
        said.add ("Licence: Free For Profit");
    }
    else if (match (text, R"rx(\b(non[- ]?profit|make it free|set it (?:to )?free|just free|free only|only free|as free|licen[cs]e (?:to |is )?free)\b)rx", m))
    {
        add ("setLicence", "free");
        said.add ("Licence: Free (lease needed to release)");
    }

    if (match (text, R"rx(\b(add\s+)?tags?\s*(?:are|is|to|=|:)?\s+([^.;\n]+))rx", m) && ! lower.contains ("untagged"))
    {
        const auto tags = str (m[2]);
        add (m[1].matched ? "addTags" : "setTags", tags);
        said.add ("Tags: " + tags);
    }

    if (match (text, R"rx(\b(?:catalog|catalogue|category|folder)\s+(?:called\s+|named\s+)?"?([^",.;\n]+))rx", m))
    {
        const auto name = str (m[1]);
        add ("setCatalog", name);
        said.add (library.findCatalogByName (name) != nullptr ? "Catalog: " + name : "New catalog: " + name);
    }

    if (match (text, R"rx(\bdescription\s*(?:is|to|=|:)?\s+(.+))rx", m))
    {
        add ("setDescription", str (m[1]));
        said.add ("Description added");
    }

    bool scheduled = false;
    if (match (text, R"rx(\b(schedule|tomorrow|tonight|monday|tuesday|wednesday|thursday|friday|saturday|sunday|\d\s*(?:am|pm)|in\s+\d+\s*(?:minute|min|hour|hr|day))\b)rx", m)
        && (lower.contains ("schedule") || lower.contains ("post") || lower.contains ("upload") || lower.contains ("drop") || lower.contains ("publish")))
    {
        const auto when = parseWhen (text);
        if (when > juce::Time::getCurrentTime())
        {
            add ("schedule", {}, (double) when.toMilliseconds());
            said.add ("Goes public " + when.toString (true, true, false));
            scheduled = true;
        }
        else
            said.add ("I could not work out the time. Say it like \"tomorrow at 6pm\".");
    }

    if (! scheduled)
    {
        if (match (text, R"rx(\bunlisted\b)rx", m))       { add ("setPrivacy", "unlisted"); said.add ("Unlisted"); }
        else if (match (text, R"rx(\bprivate\b)rx", m))   { add ("setPrivacy", "private");  said.add ("Private"); }
        else if (match (text, R"rx(\bpublic\b)rx", m))    { add ("setPrivacy", "public");   said.add ("Public"); }
    }

    if (match (text, R"rx(\b(e-?mail|send it to|pitch)\b)rx", m))
    {
        juce::String who;
        if (match (text, R"rx(\b(?:to|for)\s+([A-Z][^,.;\n]+))rx", m)) who = str (m[1]);
        add ("email", who);
        said.add ("Opening the email page" + (who.isNotEmpty() ? " for " + who : juce::String()));
    }
    else if (match (text, R"rx(\b(upload|post|publish|drop it|send it|go live|put it out|schedule)\b)rx", m))
    {
        add ("upload");
        const bool hasAudio = draft.audio.existsAsFile();
        said.add (hasAudio ? "Uploading" : "There is no beat caught yet: export it or press Record first, then I will upload it");
    }
    else if (match (text, R"rx(\bsave\b)rx", m))            { add ("save"); said.add ("Saved to your beats"); }

    if (match (text, R"rx(\b(record|arm|capture|catch)\b)rx", m) && ! lower.contains ("upload"))
    {
        add ("arm");
        said.add ("Recording is armed: press play from the start of the beat");
    }

    reply.message = said.isEmpty() ? juce::String ("I did not catch that. Try something like: \"Name it Get It, Gunna type beat, $25, free for profit, upload it\". Type help to see everything I can do.")
                                   : said.joinIntoString ("\n");
    return reply;
}

AssistantReply fromJson (const juce::var& v)
{
    AssistantReply reply;
    reply.message = v["message"].toString();
    if (auto* list = v["actions"].getArray())
        for (auto& a : *list)
            reply.actions.add ({ a["type"].toString(), a["text"].toString(), (double) a.getProperty ("number", 0.0) });
    return reply;
}

juce::var draftAsJson (const Draft& d, const Library& library, const Settings& settings)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("beat", d.beat.toVar());
    o->setProperty ("hasAudio", d.audio.existsAsFile());
    o->setProperty ("hasCover", d.cover.existsAsFile());
    o->setProperty ("privacy", d.privacy);
    o->setProperty ("producer", settings.get ("producer"));
    o->setProperty ("now", juce::Time::getCurrentTime().toISO8601 (true));
    juce::Array<juce::var> cats;
    for (auto& c : library.orderedCatalogs()) cats.add (library.catalogPath (c.id));
    o->setProperty ("catalogs", cats);
    return juce::var (o);
}
}
