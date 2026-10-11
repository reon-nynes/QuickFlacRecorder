// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace qfr::theme
{
// Canonical native runtime tokens, mirrored and explained in DESIGN.md.
inline const juce::Colour background { 0xff20272e };
inline const juce::Colour surface { 0xff2a333c };
inline const juce::Colour border { 0xff44515d };
inline const juce::Colour text { 0xffeff2f6 };
inline const juce::Colour muted { 0xffb3bdc8 };
inline const juce::Colour record { 0xffd96863 };
inline const juce::Colour meter { 0xff63d8c2 };
inline const juce::Colour warning { 0xffe5b864 };
inline const juce::Colour danger { 0xffff8681 };
inline constexpr float controlRadius = 4.0f;
inline constexpr int margin = 14;

inline juce::Font body(float size = 13.0f)
{
   #if JUCE_WINDOWS
    return juce::Font(juce::FontOptions("Segoe UI", size, juce::Font::plain));
   #else
    return juce::Font(juce::FontOptions(size));
   #endif
}

inline juce::Font data(float size = 12.0f)
{
    return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), size,
                                     juce::Font::plain));
}

class LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    LookAndFeel()
    {
        setColour(juce::TextEditor::backgroundColourId, surface);
        setColour(juce::TextEditor::textColourId, text);
        setColour(juce::TextEditor::outlineColourId, border);
        setColour(juce::TextEditor::focusedOutlineColourId, meter);
        setColour(juce::TextEditor::highlightColourId, meter.withAlpha(0.3f));
        setColour(juce::TextEditor::highlightedTextColourId, text);
        setColour(juce::ComboBox::backgroundColourId, surface);
        setColour(juce::ComboBox::textColourId, text);
        setColour(juce::ComboBox::outlineColourId, border);
        setColour(juce::ComboBox::focusedOutlineColourId, meter);
        setColour(juce::TextButton::buttonColourId, surface);
        setColour(juce::TextButton::textColourOffId, text);
        setColour(juce::Label::textColourId, text);
        setColour(juce::PopupMenu::backgroundColourId, surface);
        setColour(juce::PopupMenu::textColourId, text);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, border);
        setColour(juce::PopupMenu::highlightedTextColourId, text);
        setColour(juce::AlertWindow::backgroundColourId, background);
        setColour(juce::AlertWindow::textColourId, text);
        setColour(juce::AlertWindow::outlineColourId, border);
    }

    juce::Font getTextButtonFont(juce::TextButton&, int height) override
    {
        return body(height > 40 ? 22.0f : 13.0f);
    }
    juce::Font getComboBoxFont(juce::ComboBox&) override { return body(); }
    juce::Font getPopupMenuFont() override { return body(); }
    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& colour,
                              bool highlighted, bool down) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
        auto fill = colour;
        if (highlighted) fill = fill.brighter(0.12f);
        if (down) fill = fill.darker(0.12f);
        if (! button.isEnabled()) fill = fill.withAlpha(0.35f);
        g.setColour(fill);
        g.fillRoundedRectangle(bounds, controlRadius);
        g.setColour(button.hasKeyboardFocus(true) ? meter : border);
        g.drawRoundedRectangle(bounds, controlRadius, button.hasKeyboardFocus(true) ? 2.0f : 1.0f);
    }
};
}
