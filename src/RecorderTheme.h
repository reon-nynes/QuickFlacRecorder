// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "RecordingPresentation.h"

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
inline constexpr int contentWidth = 600;
inline constexpr int contentHeight = 400;
inline constexpr int timerAreaHeight = 60;
inline constexpr int recordWidth = 140;
inline constexpr int recordHeight = 64;
inline constexpr float bodySize = 15.0f;
inline constexpr float labelSize = 14.0f;
inline constexpr float secondarySize = 13.0f;
inline constexpr float timerSize = 32.0f;
inline constexpr float actionSize = 22.0f;

inline juce::Font body(float size = bodySize)
{
   #if JUCE_WINDOWS
    return juce::Font(juce::FontOptions("Segoe UI", size, juce::Font::plain)
                     .withFallbacks({ "Microsoft YaHei UI", "Microsoft YaHei" }));
   #elif JUCE_MAC
    return juce::Font(juce::FontOptions(size).withFallbacks({ "PingFang SC" }));
   #else
    return juce::Font(juce::FontOptions(size).withFallbacks({ "Noto Sans CJK SC", "WenQuanYi Micro Hei" }));
   #endif
}

inline juce::Font data(float size = secondarySize)
{
    return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), size,
                                     juce::Font::plain).withFallbacks({ "Microsoft YaHei UI", "PingFang SC", "Noto Sans CJK SC" }));
}

class RecordingButton final : public juce::TextButton
{
public:
    void setAction(RecordingAction selected) { action = selected; repaint(); }
    RecordingAction getAction() const noexcept { return action; }
private:
    RecordingAction action = RecordingAction::record;
};

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
        return body(height > 40 ? actionSize : bodySize);
    }
    juce::Font getComboBoxFont(juce::ComboBox&) override { return body(); }
    juce::Font getPopupMenuFont() override { return body(); }
    juce::Font getAlertWindowMessageFont() override { return body(); }
    juce::Font getAlertWindowTitleFont() override { return body(bodySize + 2.0f); }
    void drawComboBox(juce::Graphics& g, int width, int height, bool down,
                      int buttonX, int buttonY, int buttonWidth, int buttonHeight,
                      juce::ComboBox& box) override
    {
        juce::LookAndFeel_V4::drawComboBox(g, width, height, down,
                                          buttonX, buttonY, buttonWidth, buttonHeight, box);
        if (box.hasKeyboardFocus(true))
        {
            g.setColour(meter);
            g.drawRoundedRectangle(box.getLocalBounds().toFloat().reduced(1.0f), controlRadius, 2.0f);
        }
    }
    void drawButtonText(juce::Graphics& g, juce::TextButton& button,
                        bool highlighted, bool down) override
    {
        const auto* actionButton = dynamic_cast<RecordingButton*>(&button);
        if (actionButton == nullptr)
        {
            juce::LookAndFeel_V4::drawButtonText(g, button, highlighted, down);
            return;
        }
        const auto label = button.getButtonText();
        const auto action = actionButton->getAction();
        const auto hasIcon = action != RecordingAction::finalizing;
        const auto font = body(actionSize);
        const auto textWidth = juce::GlyphArrangement::getStringWidthInt(font, label);
        const int iconSize = 14, iconGap = 10;
        const auto groupWidth = textWidth + (hasIcon ? iconSize + iconGap : 0);
        const auto x = (button.getWidth() - groupWidth) / 2;
        if (hasIcon)
        {
            g.setColour(button.isEnabled() ? (action == RecordingAction::record ? record : text) : muted);
            const auto y = static_cast<float>((button.getHeight() - iconSize) / 2);
            if (action == RecordingAction::record) g.fillEllipse(static_cast<float>(x), y, iconSize, iconSize);
            else g.fillRect(static_cast<float>(x), y, static_cast<float>(iconSize), static_cast<float>(iconSize));
        }
        g.setFont(font);
        g.setColour(button.isEnabled() ? text : muted);
        g.drawText(label, x + (hasIcon ? iconSize + iconGap : 0), 0, textWidth, button.getHeight(),
                   juce::Justification::centred);
    }
    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& colour,
                              bool highlighted, bool down) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
        auto fill = colour;
        if (down) fill = fill.darker(0.12f);
        else if (highlighted) fill = fill.brighter(0.12f);
        if (! button.isEnabled()) fill = fill.withAlpha(0.35f);
        g.setColour(fill);
        g.fillRoundedRectangle(bounds, controlRadius);
        g.setColour(button.hasKeyboardFocus(true) ? meter : border);
        g.drawRoundedRectangle(bounds, controlRadius, button.hasKeyboardFocus(true) ? 2.0f : 1.0f);
    }
};
}
