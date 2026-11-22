#include "Theme.h"
#include "BinaryData.h"

namespace sd::theme
{
static juce::Typeface::Ptr loadTypeface (const void* data, int size) { return juce::Typeface::createSystemTypefaceFor (data, (size_t) size); }
static const juce::Typeface::Ptr& antonFace()     { static juce::Typeface::Ptr t = loadTypeface (BinaryData::AntonRegular_ttf, BinaryData::AntonRegular_ttfSize); return t; }
static const juce::Typeface::Ptr& interFace()     { static juce::Typeface::Ptr t = loadTypeface (BinaryData::InterRegular_ttf, BinaryData::InterRegular_ttfSize); return t; }
static const juce::Typeface::Ptr& interBoldFace() { static juce::Typeface::Ptr t = loadTypeface (BinaryData::InterSemiBold_ttf, BinaryData::InterSemiBold_ttfSize); return t; }

juce::Font headline (float px) { return juce::Font (juce::FontOptions (antonFace()).withHeight (px)); }
juce::Font body (float px)     { return juce::Font (juce::FontOptions (interFace()).withHeight (px)); }
juce::Font bodyBold (float px) { return juce::Font (juce::FontOptions (interBoldFace()).withHeight (px)); }

void fillPanel (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour fill, float radius)
{
    g.setColour (fill);
    g.fillRoundedRectangle (r, radius);
    g.setColour (line);
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
}

static juce::AttributedString attributed (const juce::String& s, const juce::Font& f, juce::Colour c, float lineSpacing)
{
    juce::AttributedString a;
    a.setJustification (juce::Justification::topLeft);
    a.setWordWrap (juce::AttributedString::byWord);
    a.setLineSpacing (f.getHeight() * (lineSpacing - 1.0f));
    a.append (s, f, c);
    return a;
}

void drawWrapped (juce::Graphics& g, const juce::String& s, juce::Rectangle<float> r, const juce::Font& f, juce::Colour c, float lineSpacing)
{
    juce::TextLayout layout;
    layout.createLayout (attributed (s, f, c, lineSpacing), r.getWidth());
    layout.draw (g, r);
}

float wrappedHeight (const juce::String& s, float width, const juce::Font& f, float lineSpacing)
{
    juce::TextLayout layout;
    layout.createLayout (attributed (s, f, juce::Colours::white, lineSpacing), width);
    return layout.getHeight();
}

void drawIcon (juce::Graphics& g, Icon icon, juce::Rectangle<float> area, juce::Colour colour, float thickness)
{
    // every icon is drawn on a 24 x 24 grid and scaled into the square it is given
    const float side = juce::jmin (area.getWidth(), area.getHeight());
    const auto box = juce::Rectangle<float> (side, side).withCentre (area.getCentre());
    const auto t = juce::AffineTransform::scale (side / 24.0f).translated (box.getX(), box.getY());
    juce::Path p, solid;

    switch (icon)
    {
        case Icon::upload:    p.startNewSubPath (12, 16); p.lineTo (12, 4); p.startNewSubPath (7, 9); p.lineTo (12, 4); p.lineTo (17, 9);
                              p.startNewSubPath (4, 15); p.lineTo (4, 20); p.lineTo (20, 20); p.lineTo (20, 15); break;
        case Icon::beats:     p.startNewSubPath (9, 18); p.lineTo (9, 5); p.lineTo (19, 3); p.lineTo (19, 16);
                              p.addEllipse (4, 15, 5, 5); p.addEllipse (14, 13, 5, 5); break;
        case Icon::email:     p.addRoundedRectangle (3, 5, 18, 14, 2); p.startNewSubPath (3.5f, 7); p.lineTo (12, 13); p.lineTo (20.5f, 7); break;
        case Icon::assistant: p.startNewSubPath (12, 3); p.lineTo (14, 10); p.lineTo (21, 12); p.lineTo (14, 14); p.lineTo (12, 21); p.lineTo (10, 14);
                              p.lineTo (3, 12); p.lineTo (10, 10); p.closeSubPath(); break;
        case Icon::settings:  p.addEllipse (9, 9, 6, 6);
                              for (int i = 0; i < 8; ++i)
                              {
                                  const float a = juce::MathConstants<float>::twoPi * (float) i / 8.0f;
                                  p.startNewSubPath (12 + std::cos (a) * 7.2f, 12 + std::sin (a) * 7.2f);
                                  p.lineTo (12 + std::cos (a) * 9.6f, 12 + std::sin (a) * 9.6f);
                              }
                              break;
        case Icon::record:    solid.addEllipse (6, 6, 12, 12); break;
        case Icon::stop:      solid.addRoundedRectangle (7, 7, 10, 10, 1.5f); break;
        case Icon::folder:    p.startNewSubPath (3, 7); p.lineTo (3, 19); p.lineTo (21, 19); p.lineTo (21, 8); p.lineTo (12, 8); p.lineTo (10, 5); p.lineTo (3, 5); p.closeSubPath(); break;
        case Icon::plus:      p.startNewSubPath (12, 5); p.lineTo (12, 19); p.startNewSubPath (5, 12); p.lineTo (19, 12); break;
        case Icon::trash:     p.startNewSubPath (4, 7); p.lineTo (20, 7); p.startNewSubPath (9, 7); p.lineTo (9, 4); p.lineTo (15, 4); p.lineTo (15, 7);
                              p.startNewSubPath (6, 7); p.lineTo (7, 20); p.lineTo (17, 20); p.lineTo (18, 7); break;
        case Icon::link:      p.addRoundedRectangle (3, 9, 10, 6, 3); p.addRoundedRectangle (11, 9, 10, 6, 3); break;
        case Icon::check:     p.startNewSubPath (5, 12.5f); p.lineTo (10, 17); p.lineTo (19, 7); break;
        case Icon::youtube:   p.addRoundedRectangle (3, 6, 18, 12, 4); solid.addTriangle (10, 9.2f, 10, 14.8f, 15, 12); break;
        case Icon::edit:      p.startNewSubPath (4, 20); p.lineTo (5, 15.5f); p.lineTo (16, 4.5f); p.lineTo (19.5f, 8); p.lineTo (8.5f, 19); p.closeSubPath(); break;
        case Icon::copy:      p.addRoundedRectangle (8, 8, 12, 12, 2); p.startNewSubPath (5, 15); p.lineTo (5, 5); p.lineTo (15, 5); break;
        case Icon::image:     p.addRoundedRectangle (3, 4, 18, 16, 2); p.addEllipse (7, 8, 3, 3); p.startNewSubPath (4, 18); p.lineTo (10, 12); p.lineTo (14, 16); p.lineTo (17, 13); p.lineTo (20.5f, 17); break;
    }

    g.setColour (colour);
    if (! p.isEmpty())     g.strokePath (p, juce::PathStrokeType (thickness * 24.0f / side, juce::PathStrokeType::curved, juce::PathStrokeType::rounded), t);
    if (! solid.isEmpty()) g.fillPath (solid, t);
}

Look::Look()
{
    setColour (juce::ResizableWindow::backgroundColourId, black);
    setColour (juce::TextEditor::textColourId, text);
    setColour (juce::TextEditor::backgroundColourId, control);
    setColour (juce::TextEditor::highlightColourId, yellow.withAlpha (0.35f));
    setColour (juce::TextEditor::highlightedTextColourId, text);
    setColour (juce::CaretComponent::caretColourId, yellow);
    setColour (juce::Label::textColourId, text);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::backgroundColourId, control);
    setColour (juce::ComboBox::arrowColourId, textDim);
    setColour (juce::PopupMenu::backgroundColourId, raised);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, yellow);
    setColour (juce::PopupMenu::highlightedTextColourId, onYellow);
    setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::ScrollBar::thumbColourId, lineHi);
    setColour (juce::AlertWindow::backgroundColourId, raised);
    setColour (juce::AlertWindow::textColourId, text);
    setColour (juce::AlertWindow::outlineColourId, lineHi);
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::TextButton::textColourOnId, onYellow);
    setColour (juce::ToggleButton::textColourId, text);
    setColour (juce::TooltipWindow::backgroundColourId, raised);
    setColour (juce::TooltipWindow::textColourId, text);
    setColour (juce::TooltipWindow::outlineColourId, lineHi);
}

void Look::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool primary = (bool) b.getProperties()["primary"] || b.getToggleState();
    const bool quiet = (bool) b.getProperties()["quiet"];
    const float alpha = b.isEnabled() ? 1.0f : 0.4f;

    if (primary)
    {
        g.setColour ((down ? yellow.darker (0.25f) : over ? yellow.brighter (0.15f) : yellow).withMultipliedAlpha (alpha));
        g.fillRoundedRectangle (r, 7.0f);
    }
    else
    {
        g.setColour ((down ? control.brighter (0.12f) : over ? control.brighter (0.06f) : (quiet ? juce::Colours::transparentBlack : control)).withMultipliedAlpha (alpha));
        g.fillRoundedRectangle (r, 7.0f);
        if (! quiet)
        {
            g.setColour ((over ? lineHi : line).withMultipliedAlpha (alpha));
            g.drawRoundedRectangle (r, 7.0f, 1.0f);
        }
    }
}

void Look::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    const bool primary = (bool) b.getProperties()["primary"] || b.getToggleState();
    g.setFont (bodyBold (13.0f));
    g.setColour ((primary ? onYellow : text).withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.45f));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (8, 0), juce::Justification::centred, 1);
}

void Look::fillTextEditorBackground (juce::Graphics& g, int w, int h, juce::TextEditor&)
{
    g.setColour (control);
    g.fillRoundedRectangle (0, 0, (float) w, (float) h, 7.0f);
}

void Look::drawTextEditorOutline (juce::Graphics& g, int w, int h, juce::TextEditor& e)
{
    g.setColour (e.hasKeyboardFocus (true) ? yellow : line);
    g.drawRoundedRectangle (0.5f, 0.5f, (float) w - 1.0f, (float) h - 1.0f, 7.0f, 1.0f);
}

void Look::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    g.setColour (control);
    g.fillRoundedRectangle (0, 0, (float) w, (float) h, 7.0f);
    g.setColour (box.hasKeyboardFocus (true) ? yellow : line);
    g.drawRoundedRectangle (0.5f, 0.5f, (float) w - 1.0f, (float) h - 1.0f, 7.0f, 1.0f);

    juce::Path arrow;
    const float cx = (float) w - 16.0f, cy = (float) h * 0.5f;
    arrow.startNewSubPath (cx - 4.0f, cy - 2.0f); arrow.lineTo (cx, cy + 2.5f); arrow.lineTo (cx + 4.0f, cy - 2.0f);
    g.setColour (textDim);
    g.strokePath (arrow, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void Look::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (10, 1, box.getWidth() - 34, box.getHeight() - 2);
    label.setFont (body (13.0f));
}

void Look::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool)
{
    const auto box = juce::Rectangle<float> (2.0f, (float) b.getHeight() * 0.5f - 9.0f, 18.0f, 18.0f);
    g.setColour (b.getToggleState() ? yellow : control);
    g.fillRoundedRectangle (box, 5.0f);
    g.setColour (b.getToggleState() ? yellow : (over ? lineHi : line));
    g.drawRoundedRectangle (box, 5.0f, 1.0f);
    if (b.getToggleState()) drawIcon (g, Icon::check, box.reduced (2.5f), onYellow, 2.2f);

    g.setColour (text.withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.45f));
    g.setFont (body (13.0f));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().withTrimmedLeft (28), juce::Justification::centredLeft, 1);
}

void Look::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int thumbStart, int thumbSize, bool over, bool down)
{
    auto thumb = vertical ? juce::Rectangle<float> ((float) x + 2.0f, (float) thumbStart, (float) w - 4.0f, (float) thumbSize)
                          : juce::Rectangle<float> ((float) thumbStart, (float) y + 2.0f, (float) thumbSize, (float) h - 4.0f);
    g.setColour (over || down ? textMute : lineHi);
    g.fillRoundedRectangle (thumb, 3.0f);
}

void Look::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (raised);
    g.setColour (lineHi);
    g.drawRect (0, 0, w, h, 1);
}

void Look::drawProgressBar (juce::Graphics& g, juce::ProgressBar&, int w, int h, double progress, const juce::String&)
{
    g.setColour (control);
    g.fillRoundedRectangle (0, 0, (float) w, (float) h, (float) h * 0.5f);
    g.setColour (yellow);
    g.fillRoundedRectangle (0, 0, juce::jmax ((float) h, (float) w * (float) juce::jlimit (0.0, 1.0, progress)), (float) h, (float) h * 0.5f);
}

void styleEditor (juce::TextEditor& e, const juce::String& placeholder, bool multiLine)
{
    e.setMultiLine (multiLine, true);
    e.setReturnKeyStartsNewLine (multiLine);
    e.setFont (body (13.5f));
    e.setTextToShowWhenEmpty (placeholder, textMute);
    e.setIndents (10, multiLine ? 8 : 0);
    e.setJustification (multiLine ? juce::Justification::topLeft : juce::Justification::centredLeft);
    e.setSelectAllWhenFocused (false);
}

void styleLabel (juce::Label& l, const juce::String& caption, bool dim)
{
    l.setText (caption, juce::dontSendNotification);
    l.setFont (dim ? bodyBold (11.0f) : body (13.0f));
    l.setColour (juce::Label::textColourId, dim ? textDim : theme::text);
    l.setInterceptsMouseClicks (false, false);
    l.setBorderSize ({ 0, 1, 2, 0 });
}
}
