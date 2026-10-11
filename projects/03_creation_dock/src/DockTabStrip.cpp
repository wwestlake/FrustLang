#include "CreationDock/DockTabStrip.h"
#include "CreationDock/DockPanel.h"

namespace CreationDock {

DockTabStrip::NavButton::NavButton(const juce::String& name, bool isLeftArrow)
    : juce::Button(name), isLeft(isLeftArrow)
{
    setTooltip(isLeft ? "Scroll tabs left" : "Scroll tabs right");
}

void DockTabStrip::NavButton::paintButton(juce::Graphics& g, bool isHighlighted, bool isDown)
{
    auto bounds = getLocalBounds();
    g.fillAll(isDown ? juce::Colour(0xff2a3440) : (isHighlighted ? juce::Colour(0xff252c34) : juce::Colour(0xff202020)));

    juce::Path arrow;
    float cx = (float)bounds.getCentreX();
    float cy = (float)bounds.getCentreY();
    float sz = 4.0f;

    if (isLeft) {
        arrow.addTriangle(cx + sz * 0.5f, cy - sz, cx - sz * 0.7f, cy, cx + sz * 0.5f, cy + sz);
    } else {
        arrow.addTriangle(cx - sz * 0.5f, cy - sz, cx + sz * 0.7f, cy, cx - sz * 0.5f, cy + sz);
    }

    g.setColour(isEnabled() ? (isHighlighted ? juce::Colour(0xffdce9ee) : juce::Colour(0xff9aa8ba))
                            : juce::Colour(0x409aa8ba));
    g.fillPath(arrow);
}

DockTabStrip::DockTabStrip(DockZone& ownerZone)
    : zone(ownerZone)
{
    addAndMakeVisible(tabContainer);
    addChildComponent(leftButton);
    addChildComponent(rightButton);

    leftButton.onClick = [this] { setScrollOffset(scrollOffset - 80); };
    rightButton.onClick = [this] { setScrollOffset(scrollOffset + 80); };
}

void DockTabStrip::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff202020));
}

int DockTabStrip::getTotalTabsWidth() const
{
    int total = 0;
    for (const auto& tab : tabViews)
        total += tab->getPreferredWidth();
    return total;
}

void DockTabStrip::setScrollOffset(int newOffset)
{
    int maxOffset = std::max(0, getTotalTabsWidth() - tabContainer.getWidth());
    int clamped = juce::jlimit(0, maxOffset, newOffset);
    if (clamped == scrollOffset) return;

    scrollOffset = clamped;
    updateLayout();
}

void DockTabStrip::updateLayout()
{
    int totalWidth = getTotalTabsWidth();
    int availWidth = getWidth();

    if (totalWidth <= availWidth || availWidth <= 40) {
        isOverflowing = false;
        leftButton.setVisible(false);
        rightButton.setVisible(false);
        tabContainer.setBounds(getLocalBounds());
        scrollOffset = 0;
    } else {
        isOverflowing = true;
        leftButton.setVisible(true);
        rightButton.setVisible(true);

        const int btnW = 18;
        auto bounds = getLocalBounds();
        rightButton.setBounds(bounds.removeFromRight(btnW));
        leftButton.setBounds(bounds.removeFromRight(btnW));
        tabContainer.setBounds(bounds);

        int maxOffset = std::max(0, totalWidth - tabContainer.getWidth());
        scrollOffset = juce::jlimit(0, maxOffset, scrollOffset);

        leftButton.setEnabled(scrollOffset > 0);
        rightButton.setEnabled(scrollOffset < maxOffset);
    }

    int x = -scrollOffset;
    for (auto& tab : tabViews) {
        int w = tab->getPreferredWidth();
        tab->setBounds(x, 0, w, tabContainer.getHeight());
        x += w;
    }
}

void DockTabStrip::resized()
{
    updateLayout();
}

void DockTabStrip::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    if (!isOverflowing) return;
    float delta = wheel.deltaX != 0.0f ? wheel.deltaX : wheel.deltaY;
    if (delta != 0.0f)
        setScrollOffset(scrollOffset - (int)(delta * 60.0f));
}

DockTab* DockTabStrip::addTabFor(DockPanel& panel)
{
    auto tab = std::make_unique<DockTab>(zone, panel);
    auto* raw = tab.get();
    tabContainer.addAndMakeVisible(*tab);
    tabViews.push_back(std::move(tab));
    updateLayout();
    return raw;
}

void DockTabStrip::removeTabFor(DockPanel* panel)
{
    for (auto it = tabViews.begin(); it != tabViews.end(); ++it) {
        if (&(*it)->getPanel() == panel) {
            tabContainer.removeChildComponent((*it).get());
            tabViews.erase(it);
            updateLayout();
            return;
        }
    }
}

void DockTabStrip::revealPanel(DockPanel* panel)
{
    if (!isOverflowing || panel == nullptr) return;

    int tabX = 0;
    DockTab* targetTab = nullptr;
    for (const auto& tab : tabViews) {
        if (&tab->getPanel() == panel) {
            targetTab = tab.get();
            break;
        }
        tabX += tab->getPreferredWidth();
    }

    if (targetTab == nullptr) return;

    int tabW = targetTab->getPreferredWidth();
    int visibleW = tabContainer.getWidth();

    if (tabX < scrollOffset) {
        setScrollOffset(tabX);
    } else if (tabX + tabW > scrollOffset + visibleW) {
        setScrollOffset(tabX + tabW - visibleW);
    }
}

void DockTabStrip::setActivePanel(DockPanel* panel)
{
    for (auto& tab : tabViews)
        tab->setActive(&tab->getPanel() == panel);

    revealPanel(panel);
}

DockPanel* DockTabStrip::getPanelAt(int index) const
{
    if (index < 0 || index >= (int) tabViews.size()) return nullptr;
    return &tabViews[(size_t) index]->getPanel();
}

int DockTabStrip::indexOfPanel(DockPanel* panel) const
{
    for (int i = 0; i < (int) tabViews.size(); ++i)
        if (&tabViews[(size_t) i]->getPanel() == panel) return i;
    return -1;
}

} // namespace CreationDock
