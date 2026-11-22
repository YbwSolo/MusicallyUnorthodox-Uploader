#include "Pages.h"

namespace sd
{
namespace
{
class SettingsPage : public Page
{
public:
    explicit SettingsPage (App& a) : Page (a)
    {
        struct Binding { Field* field; const char* key; };
        for (auto b : { Binding { &producer, "producer" }, Binding { &email, "email" }, Binding { &instagram, "instagram" },
                        Binding { &storeLink, "storeLink" }, Binding { &defaultPrice, "defaultPrice" },
                        Binding { &clientId, "google.clientId" }, Binding { &clientSecret, "google.clientSecret" }, Binding { &aiEndpoint, "ai.endpoint" } })
        {
            addAndMakeVisible (*b.field);
            b.field->editor.setText (app.services.settings.get (b.key, juce::String (b.key) == "defaultPrice" ? "20" : ""), false);
            b.field->editor.onTextChange = [this, b] { app.services.settings.set (b.key, b.field->text()); };
        }
        defaultPrice.editor.setInputRestrictions (7, "0123456789.");
        clientSecret.editor.setPasswordCharacter ((juce::juce_wchar) 0x2022);

        theme::styleLabel (licenceLabel, "NEW BEATS START AS");
        addAndMakeVisible (licenceLabel);
        addAndMakeVisible (defaultLicence);
        defaultLicence.addItemList ({ "Free for profit", "Free (lease needed to release)" }, 1);
        defaultLicence.setSelectedItemIndex (app.services.settings.get ("defaultLicence", "ffp") == "free" ? 1 : 0, juce::dontSendNotification);
        defaultLicence.onChange = [this] { app.services.settings.set ("defaultLicence", defaultLicence.getSelectedItemIndex() == 1 ? "free" : "ffp"); };

        addAndMakeVisible (catchExports);
        catchExports.setToggleState (app.services.settings.get ("catchExports", "1") == "1", juce::dontSendNotification);
        catchExports.onClick = [this]
        {
            app.services.settings.set ("catchExports", catchExports.getToggleState() ? "1" : "0");
            app.processor.capture.setCatchExports (catchExports.getToggleState());
        };

        addAndMakeVisible (listen);
        listen.setToggleState (app.services.settings.get ("listen", "1") == "1", juce::dontSendNotification);
        listen.onClick = [this]
        {
            app.services.settings.set ("listen", listen.getToggleState() ? "1" : "0");
            app.processor.capture.setListen (listen.getToggleState());
        };

        addAndMakeVisible (packByDefault);
        packByDefault.setToggleState (app.services.settings.get ("defaultBeatStars", "0") == "1", juce::dontSendNotification);
        packByDefault.onClick = [this] { app.services.settings.set ("defaultBeatStars", packByDefault.getToggleState() ? "1" : "0"); };

        for (auto* b : { &connect, &openFolder, &advanced }) addAndMakeVisible (*b);
        connect.onClick = [this]
        {
            auto& g = app.services.google;
            if (g.busy())           g.cancel();
            else if (g.signedIn())  g.signOut();
            else                    g.signIn();
            refresh();
        };
        openFolder.onClick = [this] { app.services.library.root().revealToUser(); };
        advanced.onClick = [this]   { showAdvanced = ! showAdvanced; resized(); refresh(); };
        refresh();
    }

    void paint (juce::Graphics& g) override
    {
        paintHeader (g, "SETTINGS", "Your details go into every YouTube description and email, so fill these in once.");

        theme::fillPanel (g, youtubeArea.toFloat(), theme::panel);
        auto& google = app.services.google;
        auto text = youtubeArea.reduced (16, 0);
        theme::drawIcon (g, theme::Icon::youtube, juce::Rectangle<float> ((float) text.getX(), (float) youtubeArea.getCentreY() - 12.0f, 24.0f, 24.0f),
                         google.signedIn() ? theme::yellow : theme::textDim);
        text.removeFromLeft (38);
        g.setColour (theme::text);
        g.setFont (theme::bodyBold (14.0f));
        g.drawText (google.busy() ? juce::String ("Waiting for you to approve in the browser")
                                  : (google.signedIn() ? "YouTube connected: " + google.channelName() : juce::String ("YouTube is not connected")),
                    text.withHeight (20).withY (youtubeArea.getY() + 14), juce::Justification::centredLeft);
        g.setColour (google.lastError().isNotEmpty() ? theme::bad : theme::textDim);
        g.setFont (theme::body (12.0f));
        g.drawText (google.lastError().isNotEmpty() ? google.lastError()
                                                    : juce::String ("You sign in on Google's own page. The plugin can upload videos and read your channel, nothing else."),
                    text.withHeight (18).withY (youtubeArea.getY() + 36).withTrimmedRight (150), juce::Justification::centredLeft);

        g.setColour (theme::textMute);
        g.setFont (theme::body (11.5f));
        g.drawText (juce::String ("Version ") + JucePlugin_VersionString + (media::available() ? "" : "   The video tool is missing: run the installer again."),
                    footerArea, juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (24, 0);
        r.removeFromTop (84);
        r.removeFromBottom (16);
        footerArea = r.removeFromBottom (18);

        youtubeArea = r.removeFromTop (68);
        connect.setBounds (youtubeArea.reduced (16, 18).removeFromRight (124));
        r.removeFromTop (18);

        auto row = r.removeFromTop (50);
        const int third = (row.getWidth() - 24) / 3;
        producer.setBounds (row.removeFromLeft (third));     row.removeFromLeft (12);
        email.setBounds (row.removeFromLeft (third));        row.removeFromLeft (12);
        instagram.setBounds (row);
        r.removeFromTop (12);

        row = r.removeFromTop (50);
        storeLink.setBounds (row.removeFromLeft (row.getWidth() - third - 12));
        row.removeFromLeft (12);
        defaultPrice.setBounds (row);
        r.removeFromTop (12);

        row = r.removeFromTop (50);
        auto licenceArea = row.removeFromLeft (third + 60);
        licenceLabel.setBounds (licenceArea.removeFromTop (16));
        defaultLicence.setBounds (licenceArea);
        row.removeFromLeft (20);
        openFolder.setBounds (row.removeFromRight (150).withTrimmedTop (16));
        r.removeFromTop (10);

        listen.setBounds (r.removeFromTop (26));
        catchExports.setBounds (r.removeFromTop (26));
        packByDefault.setBounds (r.removeFromTop (26));
        r.removeFromTop (6);

        advanced.setBounds (r.removeFromTop (30).removeFromLeft (128));
        r.removeFromTop (8);
        for (auto* f : { &clientId, &clientSecret, &aiEndpoint }) f->setVisible (showAdvanced);
        if (showAdvanced)
        {
            row = r.removeFromTop (50);
            clientId.setBounds (row.removeFromLeft (row.getWidth() / 2 - 6));
            row.removeFromLeft (12);
            clientSecret.setBounds (row);
            r.removeFromTop (10);
            aiEndpoint.setBounds (r.removeFromTop (50));
        }
    }

    void refresh() override
    {
        auto& g = app.services.google;
        connect.setButtonText (g.busy() ? "Cancel" : (g.signedIn() ? "Disconnect" : "Connect YouTube"));
        if (g.signedIn() || g.busy()) connect.getProperties().remove ("primary"); else theme::makePrimary (connect);
        advanced.setButtonText (showAdvanced ? "Hide advanced" : "Advanced");
        repaint();
    }

private:
    Field producer { "Producer name", "The name on your beats" }, email { "Contact email", "you@example.com" }, instagram { "Instagram", "@yourname" },
          storeLink { "Your BeatStars store link", "https://www.beatstars.com/yourname  (used as the buy link until a beat has its own)" },
          defaultPrice { "Usual price $", "20" },
          clientId { "Google client ID", "Only needed until the plugin ships with its own" },
          clientSecret { "Google client secret", "" },
          aiEndpoint { "Online assistant address", "Leave empty to use the built-in assistant" };
    juce::Label licenceLabel;
    juce::ComboBox defaultLicence;
    juce::ToggleButton listen { "Catch the beat while it plays (no export or record needed)" },
                       catchExports { "Catch the beat automatically when the DAW exports it" },
                       packByDefault { "Always pack the files for BeatStars when uploading" };
    juce::TextButton connect { "Connect YouTube" }, openFolder { "Open beats folder" }, advanced { "Advanced" };
    juce::Rectangle<int> youtubeArea, footerArea;
    bool showAdvanced = false;
};
}

std::unique_ptr<Page> makeSettingsPage (App& app) { return std::make_unique<SettingsPage> (app); }
}
