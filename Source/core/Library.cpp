#include "Library.h"

namespace sd
{
static juce::String newId() { return juce::Uuid().toString().substring (0, 12); }

juce::var Beat::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("id", id);                 o->setProperty ("title", title);
    o->setProperty ("artist", artist);         o->setProperty ("key", key);
    o->setProperty ("tags", tags);             o->setProperty ("description", description);
    o->setProperty ("catalog", catalogId);     o->setProperty ("licence", licence);
    o->setProperty ("audio", audioFile);       o->setProperty ("cover", coverFile);
    o->setProperty ("youtubeId", youtubeId);   o->setProperty ("youtubeUrl", youtubeUrl);
    o->setProperty ("beatstarsUrl", beatstarsUrl);
    o->setProperty ("bpm", bpm);               o->setProperty ("price", price);
    o->setProperty ("seconds", seconds);
    o->setProperty ("created", createdMs);     o->setProperty ("uploaded", uploadedMs);
    return juce::var (o);
}

Beat Beat::fromVar (const juce::var& v)
{
    Beat b;
    b.id = v["id"].toString();                 b.title = v["title"].toString();
    b.artist = v["artist"].toString();         b.key = v["key"].toString();
    b.tags = v["tags"].toString();             b.description = v["description"].toString();
    b.catalogId = v["catalog"].toString();     b.licence = v.getProperty ("licence", "ffp").toString();
    b.audioFile = v["audio"].toString();       b.coverFile = v["cover"].toString();
    b.youtubeId = v["youtubeId"].toString();   b.youtubeUrl = v["youtubeUrl"].toString();
    b.beatstarsUrl = v["beatstarsUrl"].toString();
    b.bpm = (double) v.getProperty ("bpm", 0.0);
    b.price = (double) v.getProperty ("price", 20.0);
    b.seconds = (double) v.getProperty ("seconds", 0.0);
    b.createdMs = (juce::int64) v.getProperty ("created", 0);
    b.uploadedMs = (juce::int64) v.getProperty ("uploaded", 0);
    return b;
}

Library::Library (juce::File rootFolder) : rootDir (std::move (rootFolder))
{
    rootDir.createDirectory();
    load();
}

void Library::load()
{
    beats.clear();
    catalogs.clear();
    auto parsed = juce::JSON::parse (rootDir.getChildFile ("library.json"));

    if (auto* list = parsed["beats"].getArray())
        for (auto& v : *list)
            beats.add (Beat::fromVar (v));

    if (auto* list = parsed["catalogs"].getArray())
        for (auto& v : *list)
            catalogs.add ({ v["id"].toString(), v["name"].toString(), v["parent"].toString() });
}

void Library::write()
{
    juce::Array<juce::var> b, c;
    for (auto& beat : beats) b.add (beat.toVar());
    for (auto& cat : catalogs)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("id", cat.id); o->setProperty ("name", cat.name); o->setProperty ("parent", cat.parentId);
        c.add (juce::var (o));
    }

    auto* rootObj = new juce::DynamicObject();
    rootObj->setProperty ("version", 1);
    rootObj->setProperty ("beats", b);
    rootObj->setProperty ("catalogs", c);

    // written to a temporary file first, so a crash half way through never leaves a broken library
    juce::TemporaryFile tmp (rootDir.getChildFile ("library.json"));
    tmp.getFile().replaceWithText (juce::JSON::toString (juce::var (rootObj)));
    tmp.overwriteTargetFileWithTemporary();
    sendChangeMessage();
}

const Beat* Library::find (const juce::String& id) const
{
    for (auto& b : beats) if (b.id == id) return &b;
    return nullptr;
}

juce::File Library::beatDir (const juce::String& id) const { return rootDir.getChildFile ("Beats").getChildFile (id); }
juce::File Library::audioOf (const Beat& b) const          { return b.audioFile.isEmpty() ? juce::File() : beatDir (b.id).getChildFile (b.audioFile); }
juce::File Library::coverOf (const Beat& b) const          { return b.coverFile.isEmpty() ? juce::File() : beatDir (b.id).getChildFile (b.coverFile); }

Beat Library::save (Beat beat, const juce::File& newAudio, const juce::File& newCover, bool moveAudio)
{
    if (beat.id.isEmpty())
    {
        beat.id = newId();
        beat.createdMs = juce::Time::currentTimeMillis();
    }

    auto dir = beatDir (beat.id);
    dir.createDirectory();

    if (newAudio.existsAsFile() && newAudio != audioOf (beat))
    {
        auto target = dir.getChildFile ("master" + newAudio.getFileExtension().toLowerCase());
        target.deleteFile();
        if (moveAudio ? newAudio.moveFileTo (target) : newAudio.copyFileTo (target))
            beat.audioFile = target.getFileName();
    }

    if (newCover.existsAsFile() && newCover != coverOf (beat))
    {
        auto target = dir.getChildFile ("cover" + newCover.getFileExtension().toLowerCase());
        target.deleteFile();
        if (newCover.copyFileTo (target))
            beat.coverFile = target.getFileName();
    }

    bool replaced = false;
    for (auto& b : beats)
        if (b.id == beat.id) { b = beat; replaced = true; }
    if (! replaced)
        beats.insert (0, beat);

    write();
    return beat;
}

void Library::remove (const juce::String& id)
{
    for (int i = beats.size(); --i >= 0;)
        if (beats.getReference (i).id == id)
            beats.remove (i);

    beatDir (id).moveToTrash();
    write();
}

Catalog Library::addCatalog (const juce::String& name, const juce::String& parentId)
{
    Catalog c { newId(), name.trim(), parentId };
    catalogs.add (c);
    write();
    return c;
}

void Library::renameCatalog (const juce::String& id, const juce::String& name)
{
    for (auto& c : catalogs) if (c.id == id) c.name = name.trim();
    write();
}

void Library::removeCatalog (const juce::String& id)
{
    auto* cat = findCatalog (id);
    if (cat == nullptr) return;
    auto parent = cat->parentId;

    // nothing inside is lost: beats and sub-categories move up one level
    for (auto& b : beats)    if (b.catalogId == id) b.catalogId = parent;
    for (auto& c : catalogs) if (c.parentId == id)  c.parentId = parent;
    for (int i = catalogs.size(); --i >= 0;)
        if (catalogs.getReference (i).id == id) catalogs.remove (i);
    write();
}

const Catalog* Library::findCatalog (const juce::String& id) const
{
    for (auto& c : catalogs) if (c.id == id) return &c;
    return nullptr;
}

const Catalog* Library::findCatalogByName (const juce::String& name) const
{
    for (auto& c : catalogs) if (c.name.equalsIgnoreCase (name.trim())) return &c;
    return nullptr;
}

juce::String Library::catalogPath (const juce::String& id) const
{
    juce::StringArray parts;
    auto current = id;
    for (int guard = 0; guard < 16 && current.isNotEmpty(); ++guard)
    {
        auto* c = findCatalog (current);
        if (c == nullptr) break;
        parts.insert (0, c->name);
        current = c->parentId;
    }
    return parts.joinIntoString (" / ");
}

int Library::depthOf (const juce::String& catalogId) const
{
    int depth = 0;
    auto current = catalogId;
    while (depth < 16)
    {
        auto* c = findCatalog (current);
        if (c == nullptr || c->parentId.isEmpty()) break;
        current = c->parentId;
        ++depth;
    }
    return depth;
}

void Library::collectChildren (const juce::String& parent, juce::Array<Catalog>& out) const
{
    juce::Array<Catalog> level;
    for (auto& c : catalogs) if (c.parentId == parent) level.add (c);
    std::sort (level.begin(), level.end(), [] (const Catalog& a, const Catalog& b) { return a.name.compareIgnoreCase (b.name) < 0; });

    for (auto& c : level)
    {
        out.add (c);
        if (out.size() < 2000) collectChildren (c.id, out);
    }
}

juce::Array<Catalog> Library::orderedCatalogs() const
{
    juce::Array<Catalog> out;
    collectChildren ({}, out);
    return out;
}

juce::StringArray Library::catalogAndChildren (const juce::String& id) const
{
    juce::StringArray ids { id };
    juce::Array<Catalog> kids;
    collectChildren (id, kids);
    for (auto& k : kids) ids.add (k.id);
    return ids;
}

juce::Array<Beat> Library::beatsIn (const juce::String& catalogId, const juce::String& search) const
{
    auto ids = catalogId.isEmpty() ? juce::StringArray() : catalogAndChildren (catalogId);
    auto needle = search.trim();
    juce::Array<Beat> out;

    for (auto& b : beats)
    {
        if (catalogId.isNotEmpty() && ! ids.contains (b.catalogId)) continue;
        if (needle.isNotEmpty() && ! (b.title.containsIgnoreCase (needle) || b.artist.containsIgnoreCase (needle)
                                      || b.tags.containsIgnoreCase (needle) || b.key.containsIgnoreCase (needle)))
            continue;
        out.add (b);
    }
    return out;
}
}
