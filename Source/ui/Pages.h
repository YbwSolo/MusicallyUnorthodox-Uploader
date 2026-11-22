#pragma once
#include "Theme.h"
#include "../PluginProcessor.h"
#include "../core/Assistant.h"
#include "../core/Texts.h"

namespace sd
{
/** What every page can reach: the plugin, the shared services, and a few things only the window can do. */
struct App
{
    UploaderProcessor& processor;
    Services& services;
    std::function<void (int)> showPage;
    std::function<void (const juce::String&, bool)> toast;                          // text, isError
    std::function<void (const juce::String&, const juce::String&)> showText;        // title, long text
    std::function<void (const juce::Array<AssistantAction>&)> apply;
    std::function<bool()> saveDraft;
    std::function<void()> upload;
    std::function<void (const juce::String&)> composeEmailFor;                      // beat id

    Draft& draft()          { return processor.draft; }
    void draftChanged()     { processor.draftChanged(); }
};

enum PageIndex { pageUpload = 0, pageBeats, pageEmail, pageAssistant, pageSettings, pageCount };

/** A text box with its small caption above it. */
class Field : public juce::Component
{
public:
    Field (const juce::String& caption, const juce::String& placeholder, bool multiLine = false);
    void resized() override;
    juce::String text() const                       { return editor.getText().trim(); }
    void setText (const juce::String& t)            { if (editor.getText() != t && ! editor.hasKeyboardFocus (true)) editor.setText (t, false); }
    juce::Label label;
    juce::TextEditor editor;
};

class Page : public juce::Component
{
public:
    explicit Page (App& a) : app (a) {}
    virtual void refresh() {}                       // called when the draft, library or connections change
    virtual void shown() {}
protected:
    void paintHeader (juce::Graphics&, const juce::String& title, const juce::String& sub);
    App& app;
};

std::unique_ptr<Page> makeUploadPage (App&);
std::unique_ptr<Page> makeBeatsPage (App&);
std::unique_ptr<Page> makeEmailPage (App&);
std::unique_ptr<Page> makeAssistantPage (App&);
std::unique_ptr<Page> makeSettingsPage (App&);
void setEmailBeat (Page& emailPage, const juce::String& beatId);

void askForText (juce::Component* parent, const juce::String& title, const juce::String& message, const juce::String& initial,
                 std::function<void (juce::String)> done);
void askToConfirm (juce::Component* parent, const juce::String& title, const juce::String& message, const juce::String& yes, std::function<void()> done);
juce::String timeAgo (juce::int64 ms);
}
