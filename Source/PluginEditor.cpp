#include "PluginEditor.h"
#include "BinaryData.h"

using namespace sd;

namespace
{
const int kSidebar = 150;

class NavButton : public juce::Component
{
public:
    NavButton (juce::String t, theme::Icon i) : text (std::move (t)), icon (i) {}
    std::function<void()> onClick;
    bool selected = false;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (10.0f, 2.0f);
        if (selected)
        {
            g.setColour (theme::yellow);
            g.fillRoundedRectangle (r, 8.0f);
        }
        else if (isMouseOver())
        {
            g.setColour (theme::raised);
            g.fillRoundedRectangle (r, 8.0f);
        }

        const auto colour = selected ? theme::onYellow : (isMouseOver() ? theme::text : theme::textDim);
        theme::drawIcon (g, icon, juce::Rectangle<float> (r.getX() + 12.0f, r.getCentreY() - 9.0f, 18.0f, 18.0f), colour, 1.7f);
        g.setColour (colour);
        g.setFont (theme::bodyBold (13.5f));
        g.drawText (text, r.withTrimmedLeft (42.0f), juce::Justification::centredLeft);
    }

    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }
    void mouseUp (const juce::MouseEvent& e) override  { if (e.mouseWasClicked() && onClick) onClick(); }

private:
    juce::String text;
    theme::Icon icon;
};
}

/** Long text shown over the page: the full YouTube post, for checking before it goes out. */
struct UploaderEditor::TextOverlay : juce::Component
{
    TextOverlay (const juce::String& t, const juce::String& text) : title (t)
    {
        addAndMakeVisible (box);
        theme::styleEditor (box, {}, true);
        box.setReadOnly (true);
        box.setText (text, false);
        addAndMakeVisible (copy);
        addAndMakeVisible (close);
        theme::makePrimary (close);
        copy.onClick = [this] { juce::SystemClipboard::copyTextToClipboard (box.getText()); };
        close.onClick = [this] { setVisible (false); };
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black.withAlpha (0.78f));
        theme::fillPanel (g, panelArea.toFloat(), theme::panel, 12.0f);
        g.setColour (theme::text);
        g.setFont (theme::headline (22.0f));
        g.drawText (title.toUpperCase(), panelArea.reduced (22, 0).withHeight (56), juce::Justification::centredLeft);
    }

    void resized() override
    {
        panelArea = getLocalBounds().withSizeKeepingCentre (juce::jmin (640, getWidth() - 60), getHeight() - 80);
        auto r = panelArea.reduced (22, 0);
        r.removeFromTop (56);
        r.removeFromBottom (20);
        auto buttons = r.removeFromBottom (32);
        r.removeFromBottom (14);
        box.setBounds (r);
        close.setBounds (buttons.removeFromRight (100));
        buttons.removeFromRight (8);
        copy.setBounds (buttons.removeFromRight (90));
    }

    void mouseUp (const juce::MouseEvent& e) override { if (! panelArea.contains (e.getPosition())) setVisible (false); }

    juce::String title;
    juce::TextEditor box;
    juce::TextButton copy { "Copy" }, close { "Close" };
    juce::Rectangle<int> panelArea;
};

UploaderEditor::UploaderEditor (UploaderProcessor& p)
    : juce::AudioProcessorEditor (&p), processor (p), app { p, p.services(), {}, {}, {}, {}, {}, {}, {} }
{
    setLookAndFeel (&look);
    logo = juce::ImageCache::getFromMemory (BinaryData::mu_logo_png, BinaryData::mu_logo_pngSize);

    app.showPage = [this] (int i)                                           { showPage (i); };
    app.toast = [this] (const juce::String& t, bool e)                      { toast (t, e); };
    app.showText = [this] (const juce::String& t, const juce::String& x)    { showText (t, x); };
    app.apply = [this] (const juce::Array<AssistantAction>& a)              { apply (a); };
    app.saveDraft = [this]                                                  { return saveDraft(); };
    app.upload = [this]                                                     { upload(); };
    app.composeEmailFor = [this] (const juce::String& id)                   { setEmailBeat (*pages[pageEmail], id); showPage (pageEmail); };

    pages[pageUpload] = makeUploadPage (app);
    pages[pageBeats] = makeBeatsPage (app);
    pages[pageEmail] = makeEmailPage (app);
    pages[pageAssistant] = makeAssistantPage (app);
    pages[pageSettings] = makeSettingsPage (app);

    const std::pair<const char*, theme::Icon> items[] = { { "Upload", theme::Icon::upload }, { "Beats", theme::Icon::beats }, { "Email", theme::Icon::email },
                                                           { "Assistant", theme::Icon::assistant }, { "Settings", theme::Icon::settings } };
    for (int i = 0; i < pageCount; ++i)
    {
        addChildComponent (*pages[i]);
        auto* b = new NavButton (items[i].first, items[i].second);
        b->onClick = [this, i] { showPage (i); };
        nav.add (b);
        addAndMakeVisible (b);
    }

    processor.addChangeListener (this);
    app.services.library.addChangeListener (this);
    app.services.contacts.addChangeListener (this);
    app.services.google.addChangeListener (this);
    app.services.publisher.addChangeListener (this);
    app.services.updater.addChangeListener (this);

    setSize (920, 640);
    showPage (pageUpload);
    startTimerHz (4);
    app.services.updater.autoCheck();
}

UploaderEditor::~UploaderEditor()
{
    processor.removeChangeListener (this);
    app.services.library.removeChangeListener (this);
    app.services.contacts.removeChangeListener (this);
    app.services.google.removeChangeListener (this);
    app.services.publisher.removeChangeListener (this);
    app.services.updater.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

void UploaderEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::black);

    auto side = getLocalBounds().removeFromLeft (kSidebar);
    g.setColour (theme::panel);
    g.fillRect (side);
    g.setColour (theme::line);
    g.fillRect (side.getRight() - 1, 0, 1, getHeight());

    // brand: the MU wordmark with the product name under it
    if (logo.isValid())
        g.drawImage (logo, juce::Rectangle<float> (18.0f, 22.0f, (float) kSidebar - 36.0f, 24.0f), juce::RectanglePlacement::xLeft | juce::RectanglePlacement::yMid);
    g.setColour (theme::yellow);
    g.setFont (theme::headline (17.0f));
    g.drawText ("UPLOADER", 18, 50, kSidebar - 36, 22, juce::Justification::centredLeft);

    // connection status at the bottom of the sidebar
    auto& google = app.services.google;
    auto status = side.removeFromBottom (58).reduced (18, 0);
    g.setColour (google.signedIn() ? theme::good : theme::textMute);
    g.fillEllipse ((float) status.getX(), (float) status.getY() + 6.0f, 8.0f, 8.0f);
    g.setColour (theme::textDim);
    g.setFont (theme::bodyBold (11.5f));
    g.drawText ("YouTube", status.getX() + 16, status.getY(), status.getWidth() - 16, 20, juce::Justification::centredLeft);
    g.setColour (theme::textMute);
    g.setFont (theme::body (11.0f));
    g.drawText (google.signedIn() ? google.channelName() : juce::String ("Not connected"), status.getX() + 16, status.getY() + 18, status.getWidth() - 16, 18, juce::Justification::centredLeft);

}

void UploaderEditor::paintOverChildren (juce::Graphics& g)
{
    if (toastText.isNotEmpty())
    {
        const auto font = theme::bodyBold (13.0f);
        const float w = juce::jmin ((float) getWidth() - (float) kSidebar - 80.0f, juce::GlyphArrangement::getStringWidth (font, toastText) + 40.0f);
        const auto r = juce::Rectangle<float> (w, 38.0f).withCentre ({ (float) kSidebar + (float) (getWidth() - kSidebar) * 0.5f, (float) getHeight() - 150.0f });
        g.setColour (toastIsError ? theme::bad : theme::yellow);
        g.fillRoundedRectangle (r, 19.0f);
        g.setColour (toastIsError ? juce::Colours::white : theme::onYellow);
        g.setFont (font);
        g.drawFittedText (toastText, r.toNearestInt().reduced (14, 0), juce::Justification::centred, 1);
    }
}

void UploaderEditor::resized()
{
    auto r = getLocalBounds();
    auto side = r.removeFromLeft (kSidebar);
    side.removeFromTop (92);
    for (auto* b : nav)
        b->setBounds (side.removeFromTop (42));

    for (auto& p : pages)
        if (p != nullptr) p->setBounds (r);
    if (overlay != nullptr) overlay->setBounds (getLocalBounds());
}

void UploaderEditor::mouseUp (const juce::MouseEvent&) { toastText.clear(); repaint(); }

void UploaderEditor::showPage (int index)
{
    current = juce::jlimit (0, (int) pageCount - 1, index);
    for (int i = 0; i < pageCount; ++i)
    {
        pages[i]->setVisible (i == current);
        static_cast<NavButton*> (nav[i])->selected = i == current;
        nav[i]->repaint();
    }
    pages[current]->refresh();
    pages[current]->shown();
}

void UploaderEditor::toast (const juce::String& text, bool isError)
{
    toastText = text;
    toastIsError = isError;
    toastUntil = juce::Time::getMillisecondCounter() + (isError ? 6000u : 3200u);
    repaint();
}

void UploaderEditor::showText (const juce::String& title, const juce::String& text)
{
    overlay = std::make_unique<TextOverlay> (title, text);
    addAndMakeVisible (*overlay);
    overlay->setBounds (getLocalBounds());
}

void UploaderEditor::timerCallback()
{
    if (toastText.isNotEmpty() && juce::Time::getMillisecondCounter() > toastUntil)
    {
        toastText.clear();
        repaint();
    }
}

void UploaderEditor::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &app.services.publisher && ! app.services.publisher.running())
    {
        auto& p = app.services.publisher;
        if (p.lastError().isNotEmpty() && p.lastError() != "Cancelled") toast (p.lastError(), true);
        else if (p.lastUrl().isNotEmpty())                              toast (p.stage(), false);

        // the upload just saved the link on the beat: show it on the draft too
        if (auto* saved = app.services.library.find (app.draft().beat.id))
            app.draft().beat = *saved;
    }

    if (source == &app.services.updater)
    {
        // say once that a new version is there; installing it is one button in Settings
        const auto update = app.services.updater.getState();
        if (update.phase == UpdateState::Phase::available && toldAboutBuild != update.latest.build && current != pageSettings)
        {
            toldAboutBuild = update.latest.build;
            toast ("Version " + update.latest.version + " is ready. Install it in Settings.", false);
        }
    }

    if (source == &app.services.google && app.services.google.lastError().isNotEmpty())
        toast (app.services.google.lastError(), true);

    pages[current]->refresh();
    repaint (0, 0, kSidebar, getHeight());
}

bool UploaderEditor::shouldDropFilesWhenDraggedExternally (const juce::DragAndDropTarget::SourceDetails& details, juce::StringArray& files, bool& canMove)
{
    const auto description = details.description.toString();
    if (! description.startsWith ("beatfile:")) return false;
    const juce::File file (description.fromFirstOccurrenceOf ("beatfile:", false, false));
    if (! file.existsAsFile()) return false;
    files.add (file.getFullPathName());
    canMove = false;
    return true;
}

bool UploaderEditor::saveDraft()
{
    auto& d = app.draft();
    if (! d.audio.existsAsFile()) { toast ("There is no beat yet. Export it, or press Record and play it through.", true); return false; }
    if (d.beat.title.trim().isEmpty()) { toast ("Give the beat a name first.", true); return false; }

    // a take caught by the plugin is moved into the library; a file you chose yourself is copied and left where it was
    const bool ours = d.audio.isAChildOf (Settings::libraryDir().getChildFile ("Captures"));
    d.beat = app.services.library.save (d.beat, d.audio, d.cover, ours);
    d.audio = app.services.library.audioOf (d.beat);
    if (d.beat.coverFile.isNotEmpty()) d.cover = app.services.library.coverOf (d.beat);
    app.draftChanged();
    return d.audio.existsAsFile();
}

void UploaderEditor::upload()
{
    auto& d = app.draft();
    auto& s = app.services;

    if (! d.toYouTube && ! d.toBeatStars && ! d.toShort) { toast ("Tick where it should go first.", true); return; }
    if (d.toYouTube && ! s.google.signedIn()) { toast ("Connect YouTube in Settings first.", true); showPage (pageSettings); return; }
    if ((d.toYouTube || d.toShort) && ! media::available()) { toast ("The video tool is missing. Run the installer again.", true); return; }
    if (d.schedule && d.publishAt < juce::Time::getCurrentTime() + juce::RelativeTime::minutes (15))
    {
        toast ("Pick a time at least 15 minutes from now.", true);
        return;
    }
    if (! saveDraft()) return;

    Publisher::Job job;
    job.beatId = d.beat.id;
    job.youtube = d.toYouTube;
    job.beatstarsPack = d.toBeatStars;
    job.request.title = texts::youtubeTitle (d.beat);
    job.request.description = texts::youtubeDescription (d.beat, s.settings);
    job.request.tags = texts::youtubeTags (d.beat);
    job.request.privacy = d.privacy;
    if (d.schedule) job.request.publishAt = d.publishAt;

    job.clip = d.toShort;
    job.shortRequest = job.request;
    job.shortRequest.title = texts::shortTitle (d.beat);
    job.shortRequest.description = texts::shortDescription (d.beat, s.settings, "{full}");

    if (job.youtube || job.clip)
    {
        const auto producer = s.settings.get ("producer").trim();
        media::FrameText words;
        words.title = d.beat.title;
        words.subtitle = d.beat.artist.trim().isNotEmpty() ? d.beat.artist.trim() + " type beat" : juce::String ("Type beat");
        words.label = texts::licenceName (d.beat);
        if (producer.isNotEmpty())           words.credit = "prod. " + producer;
        if (d.beat.bpm > 0)                  words.chips.add (juce::String (juce::roundToInt (d.beat.bpm)) + " BPM");
        if (d.beat.key.trim().isNotEmpty())  words.chips.add (d.beat.key.trim().toUpperCase());

        const auto cover = juce::ImageFileFormat::loadFrom (d.cover);
        if (job.youtube) job.frame = media::composeFrame (cover, words);
        if (job.clip)    job.tallFrame = media::composeFrame (cover, words, true);
    }

    if (! s.publisher.start (job))
        toast ("Another upload is still running.", true);
}

void UploaderEditor::apply (const juce::Array<AssistantAction>& actions)
{
    auto& d = app.draft();
    auto& lib = app.services.library;
    bool wantsUpload = false, wantsSave = false;

    for (auto& a : actions)
    {
        if (a.type == "setTitle")            d.beat.title = a.text;
        else if (a.type == "setArtist")      d.beat.artist = a.text;
        else if (a.type == "setPrice")       d.beat.price = a.number;
        else if (a.type == "setBpm")         d.beat.bpm = a.number;
        else if (a.type == "setKey")         d.beat.key = a.text;
        else if (a.type == "setLicence")     d.beat.licence = a.text == "free" ? "free" : "ffp";
        else if (a.type == "setTags")        d.beat.tags = a.text;
        else if (a.type == "addTags")        d.beat.tags = d.beat.tags.isEmpty() ? a.text : d.beat.tags + ", " + a.text;
        else if (a.type == "setDescription") d.beat.description = a.text;
        else if (a.type == "setPrivacy")     { d.privacy = a.text; d.schedule = false; }
        else if (a.type == "schedule")       { d.schedule = true; d.publishAt = juce::Time ((juce::int64) a.number); }
        else if (a.type == "setCatalog")
        {
            auto* existing = lib.findCatalogByName (a.text);
            d.beat.catalogId = existing != nullptr ? existing->id : lib.addCatalog (a.text, {}).id;
        }
        else if (a.type == "upload")         wantsUpload = true;
        else if (a.type == "save")           wantsSave = true;
        else if (a.type == "arm")            processor.capture.arm();
        else if (a.type == "email")
        {
            if (d.beat.id.isEmpty() && d.audio.existsAsFile() && d.beat.title.isNotEmpty()) saveDraft();
            setEmailBeat (*pages[pageEmail], d.beat.id);
            showPage (pageEmail);
        }
    }

    app.draftChanged();
    if (wantsUpload && d.audio.existsAsFile()) upload();
    else if (wantsSave)                        saveDraft();
}
