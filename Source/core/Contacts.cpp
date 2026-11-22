#include "Contacts.h"
#include "BinaryData.h"

namespace sd
{
static Contact fromVar (const juce::var& v, bool builtIn)
{
    Contact c;
    c.id = v["id"].toString();           c.name = v["name"].toString();
    c.company = v["company"].toString(); c.email = v["email"].toString();
    c.kind = v["kind"].toString();       c.notes = v["notes"].toString();
    c.source = v["source"].toString();   c.builtIn = builtIn;
    c.lastSentMs = (juce::int64) v.getProperty ("lastSent", 0);
    if (c.id.isEmpty()) c.id = "s-" + juce::String::toHexString (c.email.toLowerCase().hashCode64());
    return c;
}

Contacts::Contacts (juce::File storeFile) : file (std::move (storeFile)) { load(); }

void Contacts::load()
{
    list.clear();
    const auto saved = juce::JSON::parse (file);
    juce::StringArray hidden;
    if (auto* h = saved["hidden"].getArray()) for (auto& v : *h) hidden.add (v.toString());

    if (auto* mine = saved["contacts"].getArray())
        for (auto& v : *mine) list.add (fromVar (v, false));

    // the starter list is read fresh from the plugin each time, so an update can correct an address
    const auto starter = juce::JSON::parse (juce::String::fromUTF8 (BinaryData::contacts_starter_json, BinaryData::contacts_starter_jsonSize));
    if (auto* shipped = starter["contacts"].getArray())
        for (auto& v : *shipped)
        {
            auto c = fromVar (v, true);
            if (hidden.contains (c.id)) continue;
            if (auto* sent = saved["sent"].getDynamicObject()) c.lastSentMs = (juce::int64) sent->getProperty (c.id);
            list.add (c);
        }
}

void Contacts::write()
{
    const auto previous = juce::JSON::parse (file);
    juce::Array<juce::var> mine;
    auto* sent = new juce::DynamicObject();

    for (auto& c : list)
    {
        if (c.builtIn) { if (c.lastSentMs > 0) sent->setProperty (c.id, c.lastSentMs); continue; }
        auto* o = new juce::DynamicObject();
        o->setProperty ("id", c.id);           o->setProperty ("name", c.name);
        o->setProperty ("company", c.company); o->setProperty ("email", c.email);
        o->setProperty ("kind", c.kind);       o->setProperty ("notes", c.notes);
        o->setProperty ("lastSent", c.lastSentMs);
        mine.add (juce::var (o));
    }

    auto* root = new juce::DynamicObject();
    root->setProperty ("contacts", mine);
    root->setProperty ("sent", juce::var (sent));
    root->setProperty ("hidden", previous["hidden"].isArray() ? previous["hidden"] : juce::var (juce::Array<juce::var>()));
    file.replaceWithText (juce::JSON::toString (juce::var (root)));
    sendChangeMessage();
}

juce::Array<Contact> Contacts::matching (const juce::String& search) const
{
    const auto needle = search.trim();
    juce::Array<Contact> out;
    for (auto& c : list)
        if (needle.isEmpty() || c.name.containsIgnoreCase (needle) || c.company.containsIgnoreCase (needle)
            || c.email.containsIgnoreCase (needle) || c.kind.containsIgnoreCase (needle))
            out.add (c);
    return out;
}

const Contact* Contacts::find (const juce::String& id) const
{
    for (auto& c : list) if (c.id == id) return &c;
    return nullptr;
}

void Contacts::save (Contact c)
{
    c.builtIn = false;
    if (c.id.isEmpty()) c.id = juce::Uuid().toString().substring (0, 12);
    bool replaced = false;
    for (auto& existing : list)
        if (existing.id == c.id && ! existing.builtIn) { existing = c; replaced = true; }
    if (! replaced) list.insert (0, c);
    write();
}

void Contacts::remove (const juce::String& id)
{
    for (int i = list.size(); --i >= 0;)
    {
        if (list.getReference (i).id != id) continue;
        if (list.getReference (i).builtIn)
        {
            auto saved = juce::JSON::parse (file);
            juce::Array<juce::var> hidden;
            if (auto* h = saved["hidden"].getArray()) hidden = *h;
            hidden.add (id);
            if (auto* o = saved.getDynamicObject()) o->setProperty ("hidden", hidden);
            else { auto* fresh = new juce::DynamicObject(); fresh->setProperty ("hidden", hidden); saved = juce::var (fresh); }
            file.replaceWithText (juce::JSON::toString (saved));
        }
        list.remove (i);
    }
    write();
}

void Contacts::markSent (const juce::StringArray& ids)
{
    for (auto& c : list)
        if (ids.contains (c.id)) c.lastSentMs = juce::Time::currentTimeMillis();
    write();
}
}
