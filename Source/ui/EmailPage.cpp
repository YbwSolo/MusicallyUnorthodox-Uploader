#include "Pages.h"

namespace sd
{
namespace
{
class EmailPage : public Page
{
public:
    explicit EmailPage (App& a) : Page (a), model (*this)
    {
        addAndMakeVisible (list);
        list.setModel (&model);
        list.setRowHeight (48);

        addAndMakeVisible (search);
        theme::styleEditor (search, "Search contacts");
        search.onTextChange = [this] { rebuild(); };

        theme::styleLabel (beatLabel, "BEAT");
        theme::styleLabel (styleLabel, "WRITTEN FOR");
        for (auto* l : { &beatLabel, &styleLabel }) addAndMakeVisible (*l);
        addAndMakeVisible (beatBox);
        addAndMakeVisible (styleBox);
        styleBox.addItemList (texts::emailStyles(), 1);
        styleBox.setSelectedItemIndex (0, juce::dontSendNotification);
        beatBox.onChange = [this]  { write(); };
        styleBox.onChange = [this] { write(); };

        addAndMakeVisible (subject);
        addAndMakeVisible (body);
        for (auto* b : { &add, &edit, &remove, &rewrite, &copy, &send }) addAndMakeVisible (*b);
        theme::makePrimary (send);

        add.onClick = [this]     { editContact ({}); };
        edit.onClick = [this]    { if (auto* c = focused()) editContact (*c); };
        remove.onClick = [this]
        {
            if (auto* c = focused())
                askToConfirm (this, "Remove " + (c->name.isNotEmpty() ? c->name : c->email) + "?", "They are taken off your email list.", "Remove",
                              [this, id = c->id] { picked.removeString (id); app.services.contacts.remove (id); });
        };
        rewrite.onClick = [this] { write(); };
        copy.onClick = [this]
        {
            juce::SystemClipboard::copyTextToClipboard ("Subject: " + subject.text() + "\n\n" + body.editor.getText());
            app.toast ("Email copied", false);
        };
        send.onClick = [this] { openInMail(); };

        rebuild();
    }

    void paint (juce::Graphics& g) override
    {
        paintHeader (g, "EMAIL", "Pick who to send to and the email is written for you. It opens in your own mail app, ready to send.");
        theme::fillPanel (g, list.getBounds().expanded (6).toFloat(), theme::panel);

        if (contacts.isEmpty())
        {
            g.setColour (theme::textMute);
            g.setFont (theme::body (13.0f));
            g.drawFittedText ("No contacts yet.\nPress Add for every label, A&R, manager or artist whose submission email you find.",
                              list.getBounds().reduced (16, 0), juce::Justification::centred, 5);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (24, 0);
        r.removeFromTop (84);
        r.removeFromBottom (20);

        auto left = r.removeFromLeft (268);
        r.removeFromLeft (20);
        search.setBounds (left.removeFromTop (34));
        left.removeFromTop (14);
        auto leftButtons = left.removeFromBottom (32);
        left.removeFromBottom (14);
        list.setBounds (left.reduced (6));
        add.setBounds (leftButtons.removeFromLeft (84));     leftButtons.removeFromLeft (8);
        edit.setBounds (leftButtons.removeFromLeft (70));    leftButtons.removeFromLeft (8);
        remove.setBounds (leftButtons.removeFromLeft (84));

        auto row = r.removeFromTop (50);
        auto beatArea = row.removeFromLeft (row.getWidth() * 58 / 100);
        beatLabel.setBounds (beatArea.removeFromTop (16));
        beatBox.setBounds (beatArea);
        row.removeFromLeft (12);
        styleLabel.setBounds (row.removeFromTop (16));
        styleBox.setBounds (row);
        r.removeFromTop (10);

        subject.setBounds (r.removeFromTop (50));
        r.removeFromTop (10);
        auto buttons = r.removeFromBottom (32);
        r.removeFromBottom (14);
        body.setBounds (r);

        send.setBounds (buttons.removeFromRight (190));     buttons.removeFromRight (8);
        copy.setBounds (buttons.removeFromRight (70));      buttons.removeFromRight (8);
        rewrite.setBounds (buttons.removeFromRight (104));
    }

    void refresh() override { rebuild(); }

    void shown() override
    {
        rebuild();
        if (wanted.isNotEmpty()) { selectBeat (wanted); wanted.clear(); }
        write();
    }

    void setBeat (const juce::String& id) { wanted = id; }

private:
    struct Model : juce::ListBoxModel
    {
        explicit Model (EmailPage& p) : page (p) {}
        int getNumRows() override { return page.contacts.size(); }

        void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
        {
            if (row >= page.contacts.size()) return;
            const auto& c = page.contacts.getReference (row);
            const bool on = page.picked.contains (c.id);

            if (selected)
            {
                g.setColour (theme::raised);
                g.fillRoundedRectangle (0.0f, 1.0f, (float) w, (float) h - 2.0f, 6.0f);
            }

            const auto box = juce::Rectangle<float> (10.0f, (float) h * 0.5f - 9.0f, 18.0f, 18.0f);
            g.setColour (on ? theme::yellow : theme::control);
            g.fillRoundedRectangle (box, 5.0f);
            g.setColour (on ? theme::yellow : theme::lineHi);
            g.drawRoundedRectangle (box, 5.0f, 1.0f);
            if (on) theme::drawIcon (g, theme::Icon::check, box.reduced (2.5f), theme::onYellow, 2.2f);

            const auto headline = c.name.isNotEmpty() ? (c.company.isNotEmpty() ? c.name + ", " + c.company : c.name) : (c.company.isNotEmpty() ? c.company : c.email);
            g.setColour (theme::text);
            g.setFont (theme::bodyBold (13.0f));
            g.drawText (headline, 38, 6, w - 46, 18, juce::Justification::centredLeft);
            g.setColour (theme::textDim);
            g.setFont (theme::body (11.5f));
            const auto sent = c.lastSentMs > 0 ? "   sent " + timeAgo (c.lastSentMs) : juce::String();
            g.drawText (c.email + sent, 38, 25, w - 46, 16, juce::Justification::centredLeft);
        }

        void listBoxItemClicked (int row, const juce::MouseEvent&) override
        {
            if (row >= page.contacts.size()) return;
            const auto id = page.contacts.getReference (row).id;
            if (page.picked.contains (id)) page.picked.removeString (id); else page.picked.add (id);
            page.updateSend();
            page.write();
            page.list.repaint();
        }

        EmailPage& page;
    };

    void rebuild()
    {
        contacts = app.services.contacts.matching (search.getText());
        for (int i = picked.size(); --i >= 0;)
            if (app.services.contacts.find (picked[i]) == nullptr) picked.remove (i);
        list.updateContent();
        list.repaint();

        const auto current = currentBeatId();
        beatIds.clear();
        beatBox.clear (juce::dontSendNotification);
        int id = 1;
        for (auto& b : app.services.library.allBeats())
        {
            beatIds.add (b.id);
            beatBox.addItem ((b.title.isEmpty() ? juce::String ("Untitled") : b.title) + (b.youtubeUrl.isEmpty() ? "   (not uploaded yet)" : juce::String()), id++);
        }
        beatBox.setTextWhenNothingSelected (beatIds.isEmpty() ? "Save a beat first" : "Choose a beat");
        selectBeat (current.isNotEmpty() ? current : (beatIds.isEmpty() ? juce::String() : beatIds[0]));
        updateSend();
        repaint();
    }

    juce::String currentBeatId() const
    {
        const int i = beatBox.getSelectedItemIndex();
        return i >= 0 && i < beatIds.size() ? beatIds[i] : juce::String();
    }

    void selectBeat (const juce::String& id)
    {
        const int index = beatIds.indexOf (id);
        if (index >= 0) beatBox.setSelectedItemIndex (index, juce::dontSendNotification);
    }

    const Contact* focused() const
    {
        const int row = list.getSelectedRow();
        return row >= 0 && row < contacts.size() ? &contacts.getReference (row) : nullptr;
    }

    void updateSend()
    {
        send.setButtonText (picked.size() > 1 ? "Open in mail app (" + juce::String (picked.size()) + ")" : juce::String ("Open in mail app"));
        send.setEnabled (! picked.isEmpty() && currentBeatId().isNotEmpty());
    }

    /** Writes the subject and the email. One person gets their own name; several get a greeting that suits everyone. */
    void write()
    {
        auto* beat = app.services.library.find (currentBeatId());
        if (beat == nullptr) { subject.editor.setText ({}, false); body.editor.setText ({}, false); return; }

        juce::String name, company;
        if (picked.size() == 1)
            if (auto* c = app.services.contacts.find (picked[0]))
            {
                name = c->name.upToFirstOccurrenceOf (" ", false, false);
                company = c->company;
            }

        const int style = juce::jmax (0, styleBox.getSelectedItemIndex());
        subject.editor.setText (texts::emailSubject (*beat, app.services.settings, style), false);
        body.editor.setText (texts::emailBody (*beat, app.services.settings, name, company, style), false);
    }

    void openInMail()
    {
        juce::StringArray addresses;
        for (auto& id : picked)
            if (auto* c = app.services.contacts.find (id))
                if (c->email.contains ("@")) addresses.add (c->email.trim());
        if (addresses.isEmpty()) { app.toast ("Tick at least one contact with an email address.", true); return; }

        // one person is addressed directly; a group goes in BCC so nobody sees who else got it
        auto esc = [] (const juce::String& s) { return juce::URL::addEscapeChars (s, true).replace ("+", "%20"); };
        juce::String link = "mailto:" + (addresses.size() == 1 ? addresses[0] : juce::String()) + "?subject=" + esc (subject.text())
                          + "&body=" + esc (body.editor.getText());
        if (addresses.size() > 1) link << "&bcc=" + esc (addresses.joinIntoString (","));

        if (juce::URL (link).launchInDefaultBrowser())
        {
            app.services.contacts.markSent (picked);
            app.toast ("Opened in your mail app. Check it over and press send.", false);
        }
        else
            app.toast ("No mail app answered. Use Copy and paste it into your email.", true);
    }

    void editContact (const Contact& existing)
    {
        auto* w = new juce::AlertWindow (existing.id.isEmpty() ? "Add a contact" : "Edit contact", "Only add addresses that are meant for music submissions.",
                                         juce::MessageBoxIconType::NoIcon, this);
        w->addTextEditor ("name", existing.name, "Name (optional)");
        w->addTextEditor ("company", existing.company, "Label or company");
        w->addTextEditor ("email", existing.email, "Email");
        w->addTextEditor ("kind", existing.kind, "What they are (A&R, artist, manager...)");
        w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, existing] (int result)
        {
            if (result != 1) return;
            auto c = existing;
            c.name = w->getTextEditorContents ("name").trim();
            c.company = w->getTextEditorContents ("company").trim();
            c.email = w->getTextEditorContents ("email").trim();
            c.kind = w->getTextEditorContents ("kind").trim();
            if (! c.email.contains ("@")) { app.toast ("That email address does not look right.", true); return; }
            if (existing.builtIn) c.id.clear();             // an edited starter contact becomes one of your own
            app.services.contacts.save (c);
        }), true);
    }

    juce::Array<Contact> contacts;
    juce::StringArray picked, beatIds;
    juce::String wanted;
    Model model;
    juce::ListBox list;
    juce::TextEditor search;
    juce::Label beatLabel, styleLabel;
    juce::ComboBox beatBox, styleBox;
    Field subject { "Subject", "" }, body { "Email", "", true };
    juce::TextButton add { "Add" }, edit { "Edit" }, remove { "Remove" }, rewrite { "Write it again" }, copy { "Copy" }, send { "Open in mail app" };

};
}

std::unique_ptr<Page> makeEmailPage (App& app) { return std::make_unique<EmailPage> (app); }
void setEmailBeat (Page& page, const juce::String& id) { if (auto* p = dynamic_cast<EmailPage*> (&page)) p->setBeat (id); }
}
