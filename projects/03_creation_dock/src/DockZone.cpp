#include "CreationDock/DockZone.h"

namespace CreationDock {

DockZone::DockZone()
{
    addAndMakeVisible(tabStrip);
}

void DockZone::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff181818));
}

void DockZone::resized()
{
    auto bounds = getLocalBounds();
    tabStrip.setBounds(bounds.removeFromTop(DockTabStrip::height));

    if (activePanel != nullptr)
        activePanel->setBounds(bounds);
}

void DockZone::addPanel(std::unique_ptr<DockPanel> panel)
{
    auto* rawPanel = panel.get();

    addChildComponent(*rawPanel);
    ownedPanels.push_back(std::move(panel));

    tabStrip.addTabFor(*rawPanel);
    setActivePanel(rawPanel);
}

std::unique_ptr<DockPanel> DockZone::removePanel(DockPanel* panel)
{
    for (auto it = ownedPanels.begin(); it != ownedPanels.end(); ++it) {
        if (it->get() != panel) continue;

        auto extracted = std::move(*it);
        ownedPanels.erase(it);

        tabStrip.removeTabFor(panel);
        removeChildComponent(extracted.get());

        if (activePanel == panel) {
            activePanel = nullptr;
            if (!ownedPanels.empty())
                setActivePanel(ownedPanels.back().get());
        }

        resized();
        return extracted;
    }
    return nullptr;
}

int DockZone::getNumPanels() const
{
    return (int) ownedPanels.size();
}

DockPanel* DockZone::getPanel(int index) const
{
    if (index < 0 || index >= (int) ownedPanels.size()) return nullptr;
    return ownedPanels[(size_t) index].get();
}

bool DockZone::containsPanel(DockPanel* panel) const
{
    for (auto& p : ownedPanels)
        if (p.get() == panel) return true;
    return false;
}

int DockZone::getCurrentTabIndex() const
{
    return tabStrip.indexOfPanel(activePanel);
}

void DockZone::setCurrentTabIndex(int index)
{
    if (auto* panel = tabStrip.getPanelAt(index))
        setActivePanel(panel);
}

void DockZone::setActivePanel(DockPanel* panel)
{
    if (!containsPanel(panel) || activePanel == panel) return;

    activePanel = panel;
    tabStrip.setActivePanel(panel);

    for (auto& p : ownedPanels)
        p->setVisible(p.get() == activePanel);

    resized();
}

void DockZone::refreshLayout()
{
    tabStrip.resized();
    tabStrip.repaint();
}

} // namespace CreationDock
