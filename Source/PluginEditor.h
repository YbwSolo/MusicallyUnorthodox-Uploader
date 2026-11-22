#pragma once
#include "ui/Pages.h"

class UploaderEditor : public juce::AudioProcessorEditor, public juce::DragAndDropContainer,
                       private juce::ChangeListener, private juce::Timer
{
public:
    explicit UploaderEditor (UploaderProcessor&);
    ~UploaderEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void selectPage (int index)     { showPage (index); }
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

    bool shouldDropFilesWhenDraggedExternally (const juce::DragAndDropTarget::SourceDetails&, juce::StringArray& files, bool& canMove) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void showPage (int index);
    void toast (const juce::String&, bool isError);
    void showText (const juce::String& title, const juce::String& text);
    void apply (const juce::Array<sd::AssistantAction>&);
    bool saveDraft();
    void upload();

    UploaderProcessor& processor;
    sd::theme::Look look;
    sd::App app;
    juce::Image logo;
    juce::OwnedArray<juce::Component> nav;
    std::unique_ptr<sd::Page> pages[sd::pageCount];
    int current = 0;

    juce::String toastText;
    bool toastIsError = false;
    juce::uint32 toastUntil = 0;

    struct TextOverlay;
    std::unique_ptr<TextOverlay> overlay;
    juce::TooltipWindow tooltips { this, 600 };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UploaderEditor)
};
