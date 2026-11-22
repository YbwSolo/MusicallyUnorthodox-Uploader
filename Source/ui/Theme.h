#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// Musically Unorthodox brand: black, white and MU yellow. Headlines in Anton, everything else in Inter.
namespace sd::theme
{
inline const juce::Colour black    { 0xff000000 };
inline const juce::Colour panel    { 0xff101010 };
inline const juce::Colour raised   { 0xff1a1a1a };
inline const juce::Colour control  { 0xff242424 };
inline const juce::Colour line     { 0xff262626 };
inline const juce::Colour lineHi   { 0xff3d3d3d };
inline const juce::Colour text     { 0xfff2f2f2 };
inline const juce::Colour textDim  { 0xff9c9c9c };
inline const juce::Colour textMute { 0xff5f5f5f };
inline const juce::Colour yellow   { 0xfffbf20d };
inline const juce::Colour onYellow { 0xff000000 };
inline const juce::Colour good     { 0xff3ddc84 };
inline const juce::Colour bad      { 0xffff3b30 };

juce::Font headline (float px);     // Anton
juce::Font body (float px);         // Inter Regular
juce::Font bodyBold (float px);     // Inter SemiBold

void fillPanel (juce::Graphics&, juce::Rectangle<float>, juce::Colour fill = panel, float radius = 8.0f);
void drawWrapped (juce::Graphics&, const juce::String&, juce::Rectangle<float>, const juce::Font&, juce::Colour, float lineSpacing = 1.35f);
float wrappedHeight (const juce::String&, float width, const juce::Font&, float lineSpacing = 1.35f);

/** Line icons drawn in code, so they stay sharp at any size and need no image files. */
enum class Icon { upload, beats, email, assistant, settings, record, folder, plus, trash, link, check, youtube, edit, copy, image, stop };
void drawIcon (juce::Graphics&, Icon, juce::Rectangle<float> area, juce::Colour, float thickness = 1.7f);

class Look : public juce::LookAndFeel_V4
{
public:
    Look();
    juce::Font getTextButtonFont (juce::TextButton&, int) override                 { return bodyBold (13.0f); }
    juce::Font getLabelFont (juce::Label&) override                                 { return body (13.0f); }
    juce::Font getComboBoxFont (juce::ComboBox&) override                           { return body (13.0f); }
    juce::Font getPopupMenuFont() override                                          { return body (13.5f); }
    juce::Font getAlertWindowMessageFont() override                                 { return body (13.5f); }
    juce::Font getAlertWindowTitleFont() override                                   { return bodyBold (15.0f); }
    juce::Font getAlertWindowFont() override                                        { return body (13.0f); }
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    void fillTextEditorBackground (juce::Graphics&, int w, int h, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int w, int h, juce::TextEditor&) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int, int, int, int, juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool over, bool down) override;
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int thumbStart, int thumbSize, bool over, bool down) override;
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    void drawProgressBar (juce::Graphics&, juce::ProgressBar&, int w, int h, double progress, const juce::String&) override;
};

/** A button marked as the main action is drawn solid yellow. */
inline void makePrimary (juce::Button& b)   { b.getProperties().set ("primary", true); }
inline void makeQuiet (juce::Button& b)     { b.getProperties().set ("quiet", true); }

void styleEditor (juce::TextEditor&, const juce::String& placeholder, bool multiLine = false);
void styleLabel (juce::Label&, const juce::String& text, bool dim = true);
}
