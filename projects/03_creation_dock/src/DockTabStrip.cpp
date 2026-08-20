#include "CreationDock/DockTabStrip.h"
#include "CreationDock/DockPanel.h"

namespace CreationDock {

DockTabStrip::DockTabStrip(DockZone& ownerZone)
    : zone(ownerZone)
{
}

void DockTabStrip::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff202020));
}

void DockTabStrip::resized()
{
    auto bounds = getLocalBounds();
    for (auto& tab : tabViews)
        tab->setBounds(bounds.removeFromLeft(tab->getPreferredWidth()));
}

DockTab* DockTabStrip::addTabFor(DockPanel& panel)
{
    auto tab = std::make_unique<DockTab>(zone, panel);
    auto* raw = tab.get();
    addAndMakeVisible(*tab);
    tabViews.push_back(std::move(tab));
    resized();
    return raw;
}

void DockTabStrip::removeTabFor(DockPanel* panel)
{
    for (auto it = tabViews.begin(); it != tabViews.end(); ++it) {
        if (&(*it)->getPanel() == panel) {
            tabViews.erase(it);
            resized();
            return;
        }
    }
}

void DockTabStrip::setActivePanel(DockPanel* panel)
{
    for (auto& tab : tabViews)
        tab->setActive(&tab->getPanel() == panel);
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
