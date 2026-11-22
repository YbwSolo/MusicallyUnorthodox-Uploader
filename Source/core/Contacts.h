#pragma once
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

namespace sd
{
struct Contact
{
    juce::String id, name, company, email, kind, notes, source;
    bool builtIn = false;
    juce::int64 lastSentMs = 0;
};

/** The email list: a starter list that ships with the plugin, plus anyone the producer adds. */
class Contacts : public juce::ChangeBroadcaster
{
public:
    explicit Contacts (juce::File storeFile);

    const juce::Array<Contact>& all() const { return list; }
    juce::Array<Contact> matching (const juce::String& search) const;
    const Contact* find (const juce::String& id) const;

    void save (Contact c);
    void remove (const juce::String& id);
    void markSent (const juce::StringArray& ids);

private:
    void load();
    void write();
    juce::File file;
    juce::Array<Contact> list;
};
}
