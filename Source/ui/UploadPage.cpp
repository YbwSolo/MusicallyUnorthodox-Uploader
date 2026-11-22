#include "Pages.h"

namespace sd
{
Field::Field (const juce::String& caption, const juce::String& placeholder, bool multiLine)
{
    theme::styleLabel (label, caption.toUpperCase());
    label.setBorderSize ({ 0, 1, 2, 0 });
    theme::styleEditor (editor, placeholder, multiLine);
    addAndMakeVisible (label);
    addAndMakeVisible (editor);
}

void Field::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromTop (16));
    editor.setBounds (r);
}

void Page::paintHeader (juce::Graphics& g, const juce::String& title, const juce::String& sub)
{
    g.setColour (theme::text);
    g.setFont (theme::headline (30.0f));
    g.drawText (title, 24, 16, 400, 36, juce::Justification::centredLeft);
    g.setColour (theme::textDim);
    g.setFont (theme::body (12.5f));
    g.drawText (sub, 24, 50, getWidth() - 48, 18, juce::Justification::centredLeft);
}

void askForText (juce::Component* parent, const juce::String& title, const juce::String& message, const juce::String& initial,
                 std::function<void (juce::String)> done)
{
    auto* w = new juce::AlertWindow (title, message, juce::MessageBoxIconType::NoIcon, parent);
    w->addTextEditor ("value", initial);
    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([w, done] (int result)
    {
        const auto value = w->getTextEditorContents ("value").trim();
        if (result == 1 && value.isNotEmpty()) done (value);
    }), true);
}

void askToConfirm (juce::Component* parent, const juce::String& title, const juce::String& message, const juce::String& yes, std::function<void()> done)
{
    auto* w = new juce::AlertWindow (title, message, juce::MessageBoxIconType::NoIcon, parent);
    w->addButton (yes, 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([done] (int result) { if (result == 1) done(); }), true);
}

juce::String timeAgo (juce::int64 ms)
{
    if (ms <= 0) return {};
    const auto seconds = (juce::Time::currentTimeMillis() - ms) / 1000;
    if (seconds < 90)        return "just now";
    if (seconds < 3600)      return juce::String (seconds / 60) + " min ago";
    if (seconds < 86400)     return juce::String (seconds / 3600) + " h ago";
    if (seconds < 86400 * 2) return "yesterday";
    return juce::String (seconds / 86400) + " days ago";
}

namespace
{
/** The square where the cover goes: click it or drop a picture on it. */
class CoverBox : public juce::Component
{
public:
    std::function<void()> onClick;
    void setImage (const juce::Image& i) { image = i; repaint(); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        if (image.isValid())
        {
            juce::Path clip;
            clip.addRoundedRectangle (r, 8.0f);
            g.reduceClipRegion (clip);
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            g.drawImage (image, r, juce::RectanglePlacement::fillDestination);
            return;
        }

        g.setColour (theme::control);
        g.fillRoundedRectangle (r, 8.0f);
        juce::Path outline;
        outline.addRoundedRectangle (r.reduced (1.0f), 8.0f);
        const float dashes[] = { 5.0f, 4.0f };
        juce::Path dashed;
        juce::PathStrokeType (1.2f).createDashedStroke (dashed, outline, dashes, 2);
        g.setColour (isMouseOver() ? theme::yellow : theme::lineHi);
        g.fillPath (dashed);
        theme::drawIcon (g, theme::Icon::image, r.withSizeKeepingCentre (30.0f, 30.0f).translated (0, -14.0f), theme::textDim);
        g.setColour (theme::textDim);
        g.setFont (theme::body (12.0f));
        g.drawFittedText ("Drop the cover here\nor click to choose", getLocalBounds().withTrimmedTop (getHeight() / 2 + 6).reduced (8, 0), juce::Justification::centredTop, 2);
    }

    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }
    void mouseUp (const juce::MouseEvent& e) override  { if (e.mouseWasClicked() && onClick) onClick(); }

private:
    juce::Image image;
};

class UploadPage : public Page, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    explicit UploadPage (App& a) : Page (a)
    {
        for (auto* f : { &title, &artist, &bpm, &key, &price, &tags, &link, &notes, &when })
        {
            addAndMakeVisible (*f);
            f->editor.onTextChange = [this] { readFields(); };
        }
        bpm.editor.setInputRestrictions (3, "0123456789");
        price.editor.setInputRestrictions (7, "0123456789.");

        theme::styleLabel (catalogLabel, "CATALOG");
        theme::styleLabel (licenceLabel, "LICENCE");
        theme::styleLabel (privacyLabel, "VISIBILITY");
        for (auto* c : { &catalogLabel, &licenceLabel, &privacyLabel }) addAndMakeVisible (*c);

        addAndMakeVisible (catalog);
        catalog.onChange = [this]
        {
            const int index = catalog.getSelectedItemIndex();
            app.draft().beat.catalogId = index <= 0 ? juce::String() : catalogIds[index - 1];
        };

        for (auto* b : { &ffp, &freeOnly })
        {
            addAndMakeVisible (*b);
            b->setClickingTogglesState (false);
        }
        ffp.onClick = [this]      { app.draft().beat.licence = "ffp";  app.draftChanged(); };
        freeOnly.onClick = [this] { app.draft().beat.licence = "free"; app.draftChanged(); };

        addAndMakeVisible (privacy);
        privacy.addItemList ({ "Public", "Unlisted", "Private", "Schedule" }, 1);
        privacy.onChange = [this]
        {
            auto& d = app.draft();
            const int i = privacy.getSelectedItemIndex();
            d.schedule = i == 3;
            if (i >= 0 && i < 3) d.privacy = juce::StringArray { "public", "unlisted", "private" }[i];
            app.draftChanged();
        };

        addAndMakeVisible (toYouTube);
        addAndMakeVisible (toBeatStars);
        addAndMakeVisible (toShort);
        toShort.setTooltip ("A 45 second upright clip of the fullest part of the beat. With YouTube ticked it is also posted as a Short.");
        toShort.onClick = [this] { app.draft().toShort = toShort.getToggleState(); };
        toYouTube.onClick = [this]   { app.draft().toYouTube = toYouTube.getToggleState(); };
        toBeatStars.onClick = [this] { app.draft().toBeatStars = toBeatStars.getToggleState(); };

        addAndMakeVisible (cover);
        cover.onClick = [this] { chooseFile (true); };

        for (auto* b : { &record, &choose, &useTake, &preview, &save, &upload, &openLink, &openPack, &openClip, &startNew })
            addAndMakeVisible (*b);
        theme::makePrimary (upload);
        theme::makeQuiet (startNew);
        record.onClick = [this]
        {
            auto& c = app.processor.capture;
            if (c.state() == Capture::State::idle) c.arm(); else c.stop();
            refresh();
        };
        choose.onClick = [this]   { chooseFile (false); };
        theme::makePrimary (useTake);
        useTake.onClick = [this]  { app.processor.useWaitingTake(); };
        openClip.onClick = [this]
        {
            const auto clip = Publisher::clipFile (app.services.library, app.draft().beat);
            if (clip.existsAsFile()) clip.revealToUser();
        };
        preview.onClick = [this]
        {
            readFields();
            const auto& b = app.draft().beat;
            app.showText ("What goes on YouTube", texts::youtubeTitle (b) + "\n\n" + texts::youtubeDescription (b, app.services.settings)
                                                   + "\n\nTags: " + texts::youtubeTags (b).joinIntoString (", "));
        };
        save.onClick = [this]     { readFields(); if (app.saveDraft()) app.toast ("Saved to your beats", false); };
        upload.onClick = [this]
        {
            readFields();
            if (app.services.publisher.running()) app.services.publisher.cancel(); else app.upload();
            refresh();
        };
        openLink.onClick = [this]
        {
            const auto url = app.draft().beat.youtubeUrl;
            if (url.isNotEmpty()) juce::URL (url).launchInDefaultBrowser();
        };
        openPack.onClick = [this]
        {
            // BeatStars has no way for an app to upload, so the files are laid out ready to drag onto its upload page
            const auto folder = Publisher::packFolder (app.services.library, app.draft().beat);
            if (folder.isDirectory()) folder.revealToUser();
            juce::URL ("https://studio.beatstars.com").launchInDefaultBrowser();
        };
        startNew.onClick = [this] { app.processor.newDraft(); loadedId = "-"; app.draftChanged(); };

        addAndMakeVisible (progress);
        progress.setPercentageDisplay (false);
        startTimerHz (12);
        refresh();
    }

    void paint (juce::Graphics& g) override
    {
        paintHeader (g, "UPLOAD", "Put this plugin last on the master. Play the beat through, or export it, and it is caught, ready to post.");

        // the capture card
        theme::fillPanel (g, captureArea.toFloat(), theme::panel);
        auto& c = app.processor.capture;
        const auto& d = app.draft();
        const auto state = c.state();
        juce::String line1, line2;
        juce::Colour dot = theme::textMute;

        const bool listens = app.services.settings.get ("listen", "1") == "1";
        const auto& waiting = app.processor.waitingTake;

        if (state == Capture::State::recording && c.kind() == Capture::Kind::heard)
        {
            line1 = "Listening  " + texts::lengthText (c.secondsRecorded());
            line2 = "Let it play to the end. It is caught when you stop.";
            dot = theme::yellow;
        }
        else if (state == Capture::State::recording)
        {
            line1 = (c.kind() == Capture::Kind::exported ? "Catching the export  " : "Recording  ") + texts::lengthText (c.secondsRecorded());
            line2 = "It finishes on its own when the export or the playback stops.";
            dot = theme::bad;
        }
        else if (state == Capture::State::armed)
        {
            line1 = "Armed";
            line2 = "Press play from the start of the beat and let it run to the end.";
            dot = theme::yellow;
        }
        else if (d.audio.existsAsFile())
        {
            line1 = "Beat caught  " + texts::lengthText (d.beat.seconds);
            line2 = waiting.file.existsAsFile() ? "A newer play-through (" + texts::lengthText (waiting.seconds) + ") is waiting."
                  : d.beat.id.isNotEmpty()      ? juce::String ("Saved in your beats. Play it through again for a new take.")
                                                : juce::String ("Fill in the details below, then upload. Play it again to replace it.");
            dot = theme::good;
        }
        else if (listens)
        {
            line1 = "Play your beat from the top and it is caught";
            line2 = "Stop at the end and it appears here. Exporting the beat works too.";
        }
        else
        {
            line1 = "Export your beat and it is caught here";
            line2 = "Or press Record and play it from the top, all the way through.";
        }

        auto text = captureArea.reduced (18, 0).withTrimmedRight (useTake.isVisible() ? 392 : 240);
        g.setColour (dot);
        g.fillEllipse ((float) text.getX(), (float) captureArea.getCentreY() - 5.0f, 10.0f, 10.0f);
        text.removeFromLeft (24);
        g.setColour (theme::text);
        g.setFont (theme::bodyBold (15.0f));
        g.drawText (line1, text.withHeight (22).withY (captureArea.getY() + 16), juce::Justification::centredLeft);
        g.setColour (theme::textDim);
        g.setFont (theme::body (12.0f));
        g.drawText (line2, text.withHeight (18).withY (captureArea.getY() + 38), juce::Justification::centredLeft);

        // level, so it is obvious the master is reaching the plugin
        const auto meter = juce::Rectangle<float> ((float) captureArea.getX() + 10.0f, (float) captureArea.getBottom() - 5.0f, (float) captureArea.getWidth() - 20.0f, 2.0f);
        g.setColour (theme::line);
        g.fillRect (meter);
        g.setColour (shownLevel > 0.98f ? theme::bad : theme::yellow);
        g.fillRect (meter.withWidth (meter.getWidth() * juce::jlimit (0.0f, 1.0f, shownLevel)));

        g.setColour (theme::textDim);
        g.setFont (theme::body (12.0f));
        theme::drawWrapped (g, texts::licenceSummary (d.beat.freeForProfit()), licenceNote.toFloat().withTrimmedTop (1.0f), theme::body (12.0f), theme::textDim, 1.3f);

        if (statusText.isNotEmpty())
        {
            g.setColour (statusIsError ? theme::bad : theme::text);
            g.setFont (theme::bodyBold (12.5f));
            g.drawFittedText (statusText, statusArea, juce::Justification::centredLeft, 2);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (24, 0);
        r.removeFromTop (78);

        captureArea = r.removeFromTop (72);
        auto buttons = captureArea.reduced (14, 20).removeFromRight (388);
        useTake.setBounds (buttons.removeFromLeft (142));
        buttons.removeFromLeft (10);
        record.setBounds (buttons.removeFromLeft (112));
        buttons.removeFromLeft (10);
        choose.setBounds (buttons);
        r.removeFromTop (14);

        auto right = r.removeFromRight (176);
        r.removeFromRight (18);
        cover.setBounds (right.removeFromTop (176));
        right.removeFromTop (10);
        preview.setBounds (right.removeFromTop (32));
        right.removeFromTop (12);
        toYouTube.setBounds (right.removeFromTop (26));
        toBeatStars.setBounds (right.removeFromTop (26));
        toShort.setBounds (right.removeFromTop (26));

        auto row = r.removeFromTop (50);
        title.setBounds (row.removeFromLeft (row.getWidth() * 55 / 100));
        row.removeFromLeft (12);
        artist.setBounds (row);
        r.removeFromTop (10);

        row = r.removeFromTop (50);
        bpm.setBounds (row.removeFromLeft (76));     row.removeFromLeft (12);
        key.setBounds (row.removeFromLeft (112));    row.removeFromLeft (12);
        price.setBounds (row.removeFromLeft (84));   row.removeFromLeft (12);
        catalogLabel.setBounds (row.removeFromTop (16));
        catalog.setBounds (row);
        r.removeFromTop (10);

        row = r.removeFromTop (50);
        tags.setBounds (row.removeFromLeft (row.getWidth() * 55 / 100));
        row.removeFromLeft (12);
        link.setBounds (row);
        r.removeFromTop (10);

        row = r.removeFromTop (50);
        licenceLabel.setBounds (row.removeFromTop (16));
        ffp.setBounds (row.removeFromLeft (150));
        row.removeFromLeft (8);
        freeOnly.setBounds (row.removeFromLeft (84));
        row.removeFromLeft (14);
        licenceNote = row;
        r.removeFromTop (10);

        notes.setBounds (r.removeFromTop (74));
        r.removeFromTop (12);

        // the bottom strip spans the full width under both columns
        auto bottom = getLocalBounds().reduced (24, 0).removeFromBottom (112);
        auto actions = bottom.removeFromTop (50);
        auto vis = actions.removeFromLeft (132);
        privacyLabel.setBounds (vis.removeFromTop (16));
        privacy.setBounds (vis);
        actions.removeFromLeft (12);
        when.setBounds (actions.removeFromLeft (170));
        auto buttonsRow = actions.withTrimmedTop (16);
        upload.setBounds (buttonsRow.removeFromRight (150));
        buttonsRow.removeFromRight (10);
        save.setBounds (buttonsRow.removeFromRight (86));
        buttonsRow.removeFromRight (10);
        startNew.setBounds (buttonsRow.removeFromRight (92));

        bottom.removeFromTop (12);
        statusRowArea = bottom.removeFromTop (30);
        layoutStatus();
    }

    void refresh() override
    {
        auto& d = app.draft();
        const auto& b = d.beat;

        if (loadedId != b.id + "|" + d.audio.getFullPathName())
        {
            // a different beat was loaded: push its details into the boxes even where the cursor is
            loadedId = b.id + "|" + d.audio.getFullPathName();
            for (auto* f : { &title, &artist, &bpm, &key, &price, &tags, &link, &notes, &when }) f->editor.giveAwayKeyboardFocus();
        }

        title.setText (b.title);
        artist.setText (b.artist);
        bpm.setText (b.bpm > 0 ? juce::String (juce::roundToInt (b.bpm)) : juce::String());
        key.setText (b.key);
        price.setText (texts::priceText (b.price).substring (1));
        tags.setText (b.tags);
        link.setText (b.beatstarsUrl);
        notes.setText (b.description);
        when.setText (d.publishAt.formatted ("%Y-%m-%d %H:%M"));
        when.setVisible (d.schedule);

        catalogIds.clear();
        catalog.clear (juce::dontSendNotification);
        catalog.addItem ("No catalog", 1);
        int selected = 1, id = 2;
        for (auto& c : app.services.library.orderedCatalogs())
        {
            catalogIds.add (c.id);
            catalog.addItem (juce::String::repeatedString ("    ", app.services.library.depthOf (c.id)) + c.name, id);
            if (c.id == b.catalogId) selected = id;
            ++id;
        }
        catalog.setSelectedId (selected, juce::dontSendNotification);

        ffp.setToggleState (b.freeForProfit(), juce::dontSendNotification);
        freeOnly.setToggleState (! b.freeForProfit(), juce::dontSendNotification);
        privacy.setSelectedItemIndex (d.schedule ? 3 : juce::jmax (0, juce::StringArray { "public", "unlisted", "private" }.indexOf (d.privacy)), juce::dontSendNotification);
        toYouTube.setToggleState (d.toYouTube, juce::dontSendNotification);
        toBeatStars.setToggleState (d.toBeatStars, juce::dontSendNotification);
        toShort.setToggleState (d.toShort, juce::dontSendNotification);
        useTake.setVisible (app.processor.waitingTake.file.existsAsFile() && ! p0busy());

        if (shownCover != d.cover)
        {
            shownCover = d.cover;
            cover.setImage (juce::ImageFileFormat::loadFrom (d.cover));
        }

        auto& p = app.services.publisher;
        const bool mine = p.lastBeatId().isNotEmpty() && p.lastBeatId() == b.id;
        const bool busy = p.running();
        const auto state = app.processor.capture.state();
        record.setButtonText (state == Capture::State::idle ? "Record" : "Stop");
        upload.setButtonText (busy ? "Cancel" : (d.schedule ? "Schedule" : "Upload"));
        upload.setEnabled (busy || d.audio.existsAsFile());
        if (captureArea.getWidth() > 0) repaint (captureArea);
        save.setEnabled (d.audio.existsAsFile() && ! busy);
        progress.setVisible (busy);
        shownProgress = p.progress();

        statusIsError = false;
        statusText.clear();
        if (busy)                                   statusText = p.stage();
        else if (mine && p.lastError().isNotEmpty()) { statusText = p.lastError(); statusIsError = true; }
        else if (mine && p.lastUrl().isNotEmpty())   statusText = p.stage() + ": " + p.lastUrl();
        else if (mine && p.lastPack().isDirectory()) statusText = "The BeatStars files are ready to drag onto its upload page.";
        else if (mine && ! p.running() && p.stage() == "Ready" && Publisher::clipFile (app.services.library, b).existsAsFile()) statusText = "The vertical clip is ready.";
        else if (b.youtubeUrl.isNotEmpty())          statusText = "On YouTube: " + b.youtubeUrl;
        else if (d.toYouTube && ! app.services.google.signedIn()) statusText = "YouTube is not connected yet. Connect it in Settings.";

        openLink.setVisible (! busy && b.youtubeUrl.isNotEmpty());
        openPack.setVisible (! busy && b.id.isNotEmpty() && Publisher::packFolder (app.services.library, b).isDirectory());
        openClip.setVisible (! busy && b.id.isNotEmpty() && Publisher::clipFile (app.services.library, b).existsAsFile());
        layoutStatus();
        repaint();
    }

    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray& files, int, int) override
    {
        for (auto& path : files) take (juce::File (path));
    }

private:
    void timerCallback() override
    {
        const float level = app.processor.capture.level();
        const auto state = app.processor.capture.state();
        if (std::abs (level - shownLevel) > 0.002f || state != lastState || state == Capture::State::recording)
        {
            shownLevel = level;
            if (state != lastState) { lastState = state; refresh(); }
            repaint (captureArea);
        }
    }

    bool p0busy() const { return app.services.publisher.running(); }

    void layoutStatus()
    {
        auto row = statusRowArea;
        if (openClip.isVisible()) { openClip.setBounds (row.removeFromRight (104)); row.removeFromRight (8); }
        if (openPack.isVisible()) { openPack.setBounds (row.removeFromRight (128)); row.removeFromRight (8); }
        if (openLink.isVisible()) { openLink.setBounds (row.removeFromRight (132)); row.removeFromRight (12); }
        if (progress.isVisible()) { progress.setBounds (row.removeFromLeft (180).withSizeKeepingCentre (180, 8)); row.removeFromLeft (12); }
        statusArea = row;
    }

    void readFields()
    {
        auto& d = app.draft();
        d.beat.title = title.text();
        d.beat.artist = artist.text();
        d.beat.bpm = bpm.text().getDoubleValue();
        d.beat.key = key.text();
        d.beat.price = price.text().getDoubleValue();
        d.beat.tags = tags.text();
        d.beat.beatstarsUrl = link.text();
        d.beat.description = notes.text();

        const auto parts = juce::StringArray::fromTokens (when.text(), " -:/T", {});
        if (parts.size() >= 5)
        {
            juce::Time t (parts[0].getIntValue(), parts[1].getIntValue() - 1, parts[2].getIntValue(), parts[3].getIntValue(), parts[4].getIntValue());
            if (t.toMilliseconds() > 0) d.publishAt = t;
        }
    }

    void take (const juce::File& file)
    {
        auto& d = app.draft();
        if (file.hasFileExtension ("png;jpg;jpeg;webp;gif"))
        {
            if (juce::ImageFileFormat::loadFrom (file).isValid()) d.cover = file;
            else app.toast ("That picture could not be opened. Use a PNG or JPG.", true);
        }
        else if (file.hasFileExtension ("wav;mp3;aiff;aif;flac"))
        {
            const double seconds = media::lengthInSeconds (file);
            if (seconds <= 0.0) { app.toast ("That audio file could not be read.", true); return; }
            if (d.beat.id.isNotEmpty()) app.processor.newDraft();
            app.processor.draftWasHeard = false;
            app.draft().audio = file;
            app.draft().beat.seconds = seconds;
            if (app.draft().beat.title.isEmpty()) app.draft().beat.title = file.getFileNameWithoutExtension();
        }
        app.draftChanged();
    }

    void chooseFile (bool picture)
    {
        chooser = std::make_unique<juce::FileChooser> (picture ? "Choose the cover" : "Choose a beat", juce::File(),
                                                       picture ? "*.png;*.jpg;*.jpeg" : "*.wav;*.mp3;*.aiff;*.aif;*.flac");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) take (fc.getResult()); });
    }

    Field title { "Beat name", "Get It" }, artist { "Type beat artist", "Gunna" }, bpm { "BPM", "161" }, key { "Key", "E Minor" },
          price { "Price $", "20" }, tags { "Tags", "wunna, melodic, guitar" },
          link { "This beat on BeatStars", "Paste its link once it is up" },
          notes { "Extra lines for the description", "Anything you want added under the licence text (optional)", true },
          when { "Goes public at", "2026-12-01 18:00" };
    juce::Label catalogLabel, licenceLabel, privacyLabel;
    juce::ComboBox catalog, privacy;
    juce::StringArray catalogIds;
    juce::TextButton ffp { "FREE FOR PROFIT" }, freeOnly { "FREE" };
    juce::ToggleButton toYouTube { "Post to YouTube" }, toBeatStars { "Pack for BeatStars" }, toShort { "Vertical clip + Short" };
    CoverBox cover;
    juce::TextButton useTake { "Use the new take" }, openClip { "Show clip" };
    juce::TextButton record { "Record" }, choose { "Choose a file" }, preview { "Preview the post" }, save { "Save" }, upload { "Upload" },
                     openLink { "Open on YouTube" }, openPack { "Open BeatStars" }, startNew { "New beat" };
    double shownProgress = 0.0;
    juce::ProgressBar progress { shownProgress };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> captureArea, licenceNote, statusArea, statusRowArea;
    juce::String statusText, loadedId;
    juce::File shownCover;
    bool statusIsError = false;
    float shownLevel = 0.0f;
    Capture::State lastState = Capture::State::idle;
};
}

std::unique_ptr<Page> makeUploadPage (App& app) { return std::make_unique<UploadPage> (app); }
}
