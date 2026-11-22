#include "Pages.h"

namespace sd
{
namespace
{
class BeatsPage : public Page, private juce::Thread
{
public:
    explicit BeatsPage (App& a) : Page (a), juce::Thread ("MU Uploader views"), catalogModel (*this), beatModel (*this)
    {
        addAndMakeVisible (catalogList);
        catalogList.setModel (&catalogModel);
        catalogList.setRowHeight (30);

        addAndMakeVisible (beatList);
        beatList.setModel (&beatModel);
        beatList.setRowHeight (58);

        addAndMakeVisible (search);
        theme::styleEditor (search, "Search your beats");
        search.onTextChange = [this] { rebuild(); };

        for (auto* b : { &addCatalog, &addSub, &more, &edit, &move, &copyLink, &showFiles, &email, &remove })
            addAndMakeVisible (*b);
        theme::makePrimary (edit);

        addCatalog.onClick = [this]    { newCatalog ({}); };
        addSub.onClick = [this]        { newCatalog (selectedCatalog); };
        more.onClick = [this]
        {
            juce::PopupMenu menu;
            menu.addItem (1, "Rename");
            menu.addItem (2, "Delete");
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&more).withParentComponent (getTopLevelComponent()), [this] (int choice)
            {
                auto* c = app.services.library.findCatalog (selectedCatalog);
                if (c == nullptr) return;
                if (choice == 1)
                    askForText (this, "Rename", "New name for \"" + c->name + "\"", c->name,
                                [this, id = c->id] (juce::String name) { app.services.library.renameCatalog (id, name); });
                else if (choice == 2)
                    askToConfirm (this, "Delete \"" + c->name + "\"?", "The beats inside are kept: they move up one level.", "Delete",
                                  [this, id = c->id] { selectedCatalog.clear(); app.services.library.removeCatalog (id); });
            });
        };

        edit.onClick = [this]      { if (auto* b = selectedBeat()) open (*b); };
        move.onClick = [this]      { moveSelected(); };
        copyLink.onClick = [this]
        {
            if (auto* b = selectedBeat())
            {
                const auto link = texts::listenLink (*b, app.services.settings);
                if (link.isEmpty()) { app.toast ("This beat has no link yet. Upload it first.", true); return; }
                juce::SystemClipboard::copyTextToClipboard (link);
                app.toast ("Link copied", false);
            }
        };
        showFiles.onClick = [this] { if (auto* b = selectedBeat()) app.services.library.audioOf (*b).revealToUser(); };
        email.onClick = [this]     { if (auto* b = selectedBeat()) app.composeEmailFor (b->id); };
        remove.onClick = [this]
        {
            if (auto* b = selectedBeat())
                askToConfirm (this, "Delete \"" + b->title + "\"?", "Its files go to the bin. Anything already on YouTube stays there.", "Delete",
                              [this, id = b->id] { app.services.library.remove (id); });
        };

        rebuild();
    }

    ~BeatsPage() override { stopThread (4000); }

    void paint (juce::Graphics& g) override
    {
        const int total = app.services.library.allBeats().size();
        paintHeader (g, "BEATS", juce::String (total) + (total == 1 ? " beat saved" : " beats saved") + ". Sort them into catalogs and sub-categories. Drag a beat out to use its audio file.");
        theme::fillPanel (g, catalogList.getBounds().expanded (6).toFloat(), theme::panel);
        theme::fillPanel (g, beatList.getBounds().expanded (6).toFloat(), theme::panel);

        if (beats.isEmpty())
        {
            g.setColour (theme::textMute);
            g.setFont (theme::body (13.0f));
            g.drawFittedText (total == 0 ? "Nothing saved yet.\nCatch a beat on the Upload page and press Save or Upload." : "No beats here.",
                              beatList.getBounds(), juce::Justification::centred, 3);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (24, 0);
        r.removeFromTop (84);
        r.removeFromBottom (20);

        auto left = r.removeFromLeft (196);
        r.removeFromLeft (20);
        auto leftButtons = left.removeFromBottom (32);
        left.removeFromBottom (14);
        catalogList.setBounds (left.reduced (6));
        addCatalog.setBounds (leftButtons.removeFromLeft (80));
        leftButtons.removeFromLeft (6);
        addSub.setBounds (leftButtons.removeFromLeft (56));
        leftButtons.removeFromLeft (6);
        more.setBounds (leftButtons);

        search.setBounds (r.removeFromTop (34));
        r.removeFromTop (14);
        auto actions = r.removeFromBottom (32);
        r.removeFromBottom (14);
        beatList.setBounds (r.reduced (6));

        edit.setBounds (actions.removeFromLeft (112));      actions.removeFromLeft (8);
        move.setBounds (actions.removeFromLeft (76));       actions.removeFromLeft (8);
        copyLink.setBounds (actions.removeFromLeft (84));   actions.removeFromLeft (8);
        email.setBounds (actions.removeFromLeft (64));      actions.removeFromLeft (8);
        showFiles.setBounds (actions.removeFromLeft (68));  actions.removeFromLeft (8);
        remove.setBounds (actions.removeFromLeft (66));
    }

    void refresh() override { rebuild(); }

    void shown() override
    {
        // view counts for the beats that are on YouTube, fetched quietly in the background
        if (app.services.google.signedIn() && ! isThreadRunning())
            startThread();
    }

private:
    struct CatalogModel : juce::ListBoxModel
    {
        explicit CatalogModel (BeatsPage& p) : page (p) {}
        int getNumRows() override { return page.catalogs.size() + 1; }

        void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool) override
        {
            const bool all = row == 0;
            if (! all && row > page.catalogs.size()) return;
            const auto id = all ? juce::String() : page.catalogs[row - 1].id;
            const bool selected = id == page.selectedCatalog;
            const int indent = all ? 0 : 14 * page.app.services.library.depthOf (id);

            if (selected)
            {
                g.setColour (theme::yellow);
                g.fillRoundedRectangle (0.0f, 1.0f, (float) w, (float) h - 2.0f, 6.0f);
            }

            const auto colour = selected ? theme::onYellow : theme::text;
            theme::drawIcon (g, all ? theme::Icon::beats : theme::Icon::folder, juce::Rectangle<float> (8.0f + (float) indent, 7.0f, 16.0f, 16.0f), colour, 1.5f);
            g.setColour (colour);
            g.setFont (selected ? theme::bodyBold (13.0f) : theme::body (13.0f));
            g.drawText (all ? juce::String ("All beats") : page.catalogs[row - 1].name, 32 + indent, 0, w - 64 - indent, h, juce::Justification::centredLeft);

            const int count = page.app.services.library.beatsIn (id, {}).size();
            g.setColour (selected ? theme::onYellow : theme::textMute);
            g.setFont (theme::body (11.5f));
            g.drawText (juce::String (count), w - 34, 0, 26, h, juce::Justification::centredRight);
        }

        void listBoxItemClicked (int row, const juce::MouseEvent&) override
        {
            page.selectedCatalog = row == 0 || row > page.catalogs.size() ? juce::String() : page.catalogs[row - 1].id;
            page.rebuild();
        }

        BeatsPage& page;
    };

    struct BeatModel : juce::ListBoxModel
    {
        explicit BeatModel (BeatsPage& p) : page (p) {}
        int getNumRows() override { return page.beats.size(); }

        void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
        {
            if (row >= page.beats.size()) return;
            const auto& b = page.beats.getReference (row);
            auto& app = page.app;

            if (selected)
            {
                g.setColour (theme::raised);
                g.fillRoundedRectangle (0.0f, 1.0f, (float) w, (float) h - 2.0f, 6.0f);
                g.setColour (theme::yellow);
                g.fillRoundedRectangle (0.0f, 10.0f, 3.0f, (float) h - 20.0f, 1.5f);
            }

            const auto art = juce::Rectangle<float> (12.0f, 7.0f, (float) h - 14.0f, (float) h - 14.0f);
            const auto thumb = page.thumbFor (b);
            if (thumb.isValid())
            {
                g.saveState();
                juce::Path clip;
                clip.addRoundedRectangle (art, 5.0f);
                g.reduceClipRegion (clip);
                g.drawImage (thumb, art, juce::RectanglePlacement::fillDestination);
                g.restoreState();
            }
            else
            {
                g.setColour (theme::control);
                g.fillRoundedRectangle (art, 5.0f);
                theme::drawIcon (g, theme::Icon::beats, art.reduced (12.0f), theme::textMute, 1.5f);
            }

            const int x = (int) art.getRight() + 12;
            g.setColour (theme::text);
            g.setFont (theme::bodyBold (14.0f));
            const int textWidth = w - x - (b.youtubeUrl.isNotEmpty() ? 196 : 120);
            g.drawText (b.title.isEmpty() ? juce::String ("Untitled") : b.title, x, 9, textWidth, 20, juce::Justification::centredLeft);

            juce::StringArray facts;
            if (b.artist.isNotEmpty()) facts.add (b.artist + " type");
            if (b.bpm > 0)             facts.add (juce::String (juce::roundToInt (b.bpm)) + " BPM");
            if (b.key.isNotEmpty())    facts.add (b.key);
            facts.add (texts::lengthText (b.seconds));
            const auto where = app.services.library.catalogPath (b.catalogId);
            if (where.isNotEmpty())    facts.add (where);
            g.setColour (theme::textDim);
            g.setFont (theme::body (12.0f));
            g.drawText (facts.joinIntoString ("   "), x, 30, w - x - 120, 18, juce::Justification::centredLeft);

            // right side: price, licence, and where it has been posted
            g.setColour (theme::text);
            g.setFont (theme::bodyBold (13.0f));
            g.drawText (texts::priceText (b.price), w - 70, 9, 58, 20, juce::Justification::centredRight);
            g.setColour (theme::textDim);
            g.setFont (theme::body (11.0f));
            g.drawText (b.freeForProfit() ? "Free for profit" : "Free", w - 110, 30, 98, 18, juce::Justification::centredRight);

            if (b.youtubeUrl.isNotEmpty())
            {
                const auto views = page.views[b.youtubeId];
                const auto label = views.isNotEmpty() ? views + " views" : juce::String ("On YouTube");
                const auto chip = juce::Rectangle<float> ((float) w - 186.0f, 10.0f, 104.0f, 20.0f);
                g.setColour (theme::yellow.withAlpha (0.14f));
                g.fillRoundedRectangle (chip, 10.0f);
                theme::drawIcon (g, theme::Icon::youtube, chip.withWidth (22.0f).reduced (4.0f).translated (4.0f, 0.0f), theme::yellow, 1.3f);
                g.setColour (theme::yellow);
                g.setFont (theme::bodyBold (10.5f));
                g.drawText (label, chip.withTrimmedLeft (26.0f).withTrimmedRight (6.0f), juce::Justification::centredLeft);
            }
        }

        void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override
        {
            if (row < page.beats.size()) page.open (page.beats.getReference (row));
        }

        juce::var getDragSourceDescription (const juce::SparseSet<int>& rows) override
        {
            if (rows.isEmpty() || rows[0] >= page.beats.size()) return {};
            return "beatfile:" + page.app.services.library.audioOf (page.beats.getReference (rows[0])).getFullPathName();
        }

        BeatsPage& page;
    };

    void run() override
    {
        juce::StringArray ids;
        {
            const juce::MessageManagerLock mm (this);
            if (! mm.lockWasGained()) return;
            for (auto& b : app.services.library.allBeats()) if (b.youtubeId.isNotEmpty()) ids.add (b.youtubeId);
        }
        if (ids.isEmpty()) return;

        auto counts = youtube::viewCounts (app.services.google, ids);
        juce::Component::SafePointer<BeatsPage> safe (this);
        juce::MessageManager::callAsync ([safe, counts] { if (safe != nullptr) { safe->views = counts; safe->beatList.repaint(); } });
    }

    void rebuild()
    {
        auto& lib = app.services.library;
        if (selectedCatalog.isNotEmpty() && lib.findCatalog (selectedCatalog) == nullptr) selectedCatalog.clear();
        const auto keep = selectedBeat() != nullptr ? selectedBeat()->id : juce::String();

        catalogs = lib.orderedCatalogs();
        beats = lib.beatsIn (selectedCatalog, search.getText());
        catalogList.updateContent();
        beatList.updateContent();
        catalogList.repaint();
        beatList.repaint();

        beatList.deselectAllRows();
        for (int i = 0; i < beats.size(); ++i)
            if (beats.getReference (i).id == keep) beatList.selectRow (i);

        const bool hasCatalog = selectedCatalog.isNotEmpty();
        addSub.setEnabled (hasCatalog);
        more.setEnabled (hasCatalog);
        repaint();
    }

    const Beat* selectedBeat() const
    {
        const int row = beatList.getSelectedRow();
        return row >= 0 && row < beats.size() ? &beats.getReference (row) : nullptr;
    }

    juce::Image thumbFor (const Beat& b)
    {
        auto it = thumbs.find (b.id + b.coverFile);
        if (it != thumbs.end()) return it->second;
        auto image = juce::ImageFileFormat::loadFrom (app.services.library.coverOf (b));
        if (image.isValid()) image = media::squareCover (image, 96);
        thumbs[b.id + b.coverFile] = image;
        return image;
    }

    void open (const Beat& b)
    {
        auto& d = app.draft();
        const auto keepPrivacy = d.privacy;
        app.processor.newDraft();
        d.beat = b;
        d.audio = app.services.library.audioOf (b);
        d.cover = app.services.library.coverOf (b);
        d.privacy = keepPrivacy;
        app.draftChanged();
        app.showPage (pageUpload);
    }

    void newCatalog (const juce::String& parent)
    {
        const auto* p = app.services.library.findCatalog (parent);
        askForText (this, p != nullptr ? "New sub-category in \"" + p->name + "\"" : juce::String ("New catalog"),
                    p != nullptr ? "For example: Dark, Guitar, 140 BPM" : "For example: Trap, R&B, Drill, Sold", {},
                    [this, parent] (juce::String name) { selectedCatalog = app.services.library.addCatalog (name, parent).id; rebuild(); });
    }

    void moveSelected()
    {
        auto* b = selectedBeat();
        if (b == nullptr) return;
        auto& lib = app.services.library;
        juce::PopupMenu menu;
        menu.addItem (1, "No catalog", true, b->catalogId.isEmpty());
        const auto ordered = lib.orderedCatalogs();
        for (int i = 0; i < ordered.size(); ++i)
            menu.addItem (i + 2, juce::String::repeatedString ("    ", lib.depthOf (ordered[i].id)) + ordered[i].name, true, ordered[i].id == b->catalogId);

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&move).withParentComponent (getTopLevelComponent()),
                            [this, id = b->id, ordered] (int choice)
        {
            if (choice <= 0) return;
            if (auto* beat = app.services.library.find (id))
            {
                auto updated = *beat;
                updated.catalogId = choice == 1 ? juce::String() : ordered[choice - 2].id;
                app.services.library.save (updated, {}, {}, false);
            }
        });
    }

    juce::Array<Catalog> catalogs;
    juce::Array<Beat> beats;
    juce::String selectedCatalog;
    juce::StringPairArray views;
    std::map<juce::String, juce::Image> thumbs;
    CatalogModel catalogModel;
    BeatModel beatModel;
    juce::ListBox catalogList, beatList;
    juce::TextEditor search;
    juce::TextButton addCatalog { "+ Catalog" }, addSub { "+ Sub" }, more { "Edit" },
                     edit { "Open / upload" }, move { "Move to" }, copyLink { "Copy link" }, showFiles { "Files" }, email { "Email" }, remove { "Delete" };
};
}

std::unique_ptr<Page> makeBeatsPage (App& app) { return std::make_unique<BeatsPage> (app); }
}
