#pragma once
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

namespace sd
{
struct Beat
{
    juce::String id, title, artist, key, tags, description, catalogId;
    juce::String licence { "ffp" };            // "ffp" = free for profit, "free" = free for non-profit (lease to release)
    juce::String audioFile, coverFile;         // file names inside the beat's folder
    juce::String youtubeId, youtubeUrl, beatstarsUrl;
    double bpm = 0, price = 20, seconds = 0;
    juce::int64 createdMs = 0, uploadedMs = 0;

    bool freeForProfit() const { return licence != "free"; }
    juce::var toVar() const;
    static Beat fromVar (const juce::var&);
};

struct Catalog
{
    juce::String id, name, parentId;
};

/** Beats saved from the plugin, sorted into catalogs and sub-categories. Lives in Music/MU Uploader. */
class Library : public juce::ChangeBroadcaster
{
public:
    explicit Library (juce::File rootFolder);

    const juce::Array<Beat>& allBeats() const        { return beats; }
    const juce::Array<Catalog>& allCatalogs() const  { return catalogs; }
    const Beat* find (const juce::String& id) const;

    juce::File root() const                          { return rootDir; }
    juce::File beatDir (const juce::String& id) const;
    juce::File audioOf (const Beat&) const;
    juce::File coverOf (const Beat&) const;

    /** Saves a new beat or the changes to one. A new audio or cover file is copied (or moved) into its folder. */
    Beat save (Beat beat, const juce::File& newAudio, const juce::File& newCover, bool moveAudio);
    void remove (const juce::String& id);

    Catalog addCatalog (const juce::String& name, const juce::String& parentId);
    void renameCatalog (const juce::String& id, const juce::String& name);
    void removeCatalog (const juce::String& id);
    const Catalog* findCatalog (const juce::String& id) const;
    const Catalog* findCatalogByName (const juce::String& name) const;
    juce::String catalogPath (const juce::String& id) const;      // "Trap / Dark"
    int depthOf (const juce::String& catalogId) const;
    juce::StringArray catalogAndChildren (const juce::String& id) const;

    /** Catalogs in display order: each parent followed by its children. */
    juce::Array<Catalog> orderedCatalogs() const;
    juce::Array<Beat> beatsIn (const juce::String& catalogId, const juce::String& search) const;

private:
    void load();
    void write();
    void collectChildren (const juce::String& parent, juce::Array<Catalog>& out) const;

    juce::File rootDir;
    juce::Array<Beat> beats;
    juce::Array<Catalog> catalogs;
};
}
