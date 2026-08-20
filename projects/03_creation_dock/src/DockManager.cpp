#include "CreationDock/DockManager.h"

namespace CreationDock {

namespace {
    const char* zoneKeyFor(DockTargetZone zone)
    {
        switch (zone) {
            case DockTargetZone::Left:   return "left";
            case DockTargetZone::Right:  return "right";
            case DockTargetZone::Bottom: return "bottom";
            default:                     return "center";
        }
    }
}

DockManager::DockManager(juce::Component& topLevelWindow)
    : ownerWindow(topLevelWindow)
{
    addAndMakeVisible(container);
    addAndMakeVisible(overlay);
}

DockManager::~DockManager()
{
    floatingWindows.clear();
}

void DockManager::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff181818));
}

void DockManager::resized()
{
    container.setBounds(getLocalBounds());
    overlay.setBounds(getLocalBounds());
}

DockPanel* DockManager::registerPanel(const juce::String& id, const juce::String& title, std::unique_ptr<juce::Component> contentComponent, DockTargetZone initialZone)
{
    auto panel = std::make_unique<DockPanel>(id, title, std::move(contentComponent));
    auto* rawPtr = panel.get();

    panel->onFloatRequested = [this](DockPanel* p) {
        floatPanel(p);
    };

    panel->onTitleBarDragged = [this](DockPanel* p, const juce::MouseEvent& e) {
        handlePanelTitleDrag(p, e);
    };

    panel->onTitleBarDragEnded = [this](DockPanel* p, const juce::MouseEvent& e) {
        handlePanelDragEnded(p, e);
    };

    panel->onTitleChanged = [this, rawPtr] {
        auto zoneEnum = zoneContainingPanel(rawPtr);
        DockZone* z = container.getCenterZone();
        if (zoneEnum == DockTargetZone::Left) z = container.getLeftZone();
        else if (zoneEnum == DockTargetZone::Right) z = container.getRightZone();
        else if (zoneEnum == DockTargetZone::Bottom) z = container.getBottomZone();

        if (z->containsPanel(rawPtr)) z->refreshLayout();
    };

    registeredPanels.push_back(std::move(panel));
    defaultLayout.push_back({ id, initialZone });

    dockPanel(rawPtr, initialZone);
    return rawPtr;
}

std::unique_ptr<DockPanel> DockManager::extractPanel(DockPanel* panel)
{
    for (auto it = registeredPanels.begin(); it != registeredPanels.end(); ++it) {
        if (it->get() == panel) {
            auto extracted = std::move(*it);
            registeredPanels.erase(it);
            return extracted;
        }
    }

    if (container.getLeftZone()->containsPanel(panel)) return container.getLeftZone()->removePanel(panel);
    if (container.getRightZone()->containsPanel(panel)) return container.getRightZone()->removePanel(panel);
    if (container.getBottomZone()->containsPanel(panel)) return container.getBottomZone()->removePanel(panel);
    if (container.getCenterZone()->containsPanel(panel)) return container.getCenterZone()->removePanel(panel);

    return nullptr;
}

void DockManager::dockPanel(DockPanel* panel, DockTargetZone zone)
{
    auto extractedPanel = extractPanel(panel);
    if (!extractedPanel) return;

    DockZone* targetZone = container.getCenterZone();
    if (zone == DockTargetZone::Left) targetZone = container.getLeftZone();
    if (zone == DockTargetZone::Right) targetZone = container.getRightZone();
    if (zone == DockTargetZone::Bottom) targetZone = container.getBottomZone();

    targetZone->addPanel(std::move(extractedPanel));
    container.resized();
}

void DockManager::floatPanel(DockPanel* panel)
{
    floatPanelAt(panel, { -1, -1 });
}

void DockManager::floatPanelAt(DockPanel* panel, juce::Point<int> screenPos)
{
    auto originZone = zoneContainingPanel(panel);

    auto detached = extractPanel(panel);
    if (!detached) return;

    auto floatWin = std::make_unique<FloatingDockWindow>(panel->getTitle(), std::move(detached));

    // closeButtonPressed() fires from deep inside the native window's own message
    // handling for that same window. Destroying the FloatingDockWindow synchronously
    // from within that call stack (via handleFloatingWindowClosed) is a delete-this
    // reentrancy bug - it can corrupt the message loop / mouse listener state, which
    // is what was breaking drag-and-drop for every other panel afterward. Deferring
    // the actual teardown to the next message loop iteration avoids that.
    floatWin->onWindowClosed = [this, originZone](FloatingDockWindow* win) {
        juce::MessageManager::callAsync([this, win, originZone] {
            handleFloatingWindowClosed(win, originZone);
        });
    };

    if (screenPos.x >= 0 && screenPos.y >= 0) {
        // Keep the panel roughly under the cursor rather than re-centering, so
        // dragging a tab out past the dock area feels like it detaches in place.
        floatWin->setTopLeftPosition(screenPos.translated(-40, -10));
    }

    floatingWindows.push_back(std::move(floatWin));
    container.resized();
}

void DockManager::handlePanelTitleDrag(DockPanel* panel, const juce::MouseEvent& e)
{
    auto screenPos = e.getEventRelativeTo(&ownerWindow).getScreenPosition();
    overlay.updateTargetFromMouse(screenPos, getLocalBounds());
}

void DockManager::handlePanelDragEnded(DockPanel* panel, const juce::MouseEvent& e)
{
    // A plain click (mouseDown+mouseUp with no movement) still fires this via
    // DockTab, but the overlay was never updated for it - treating that as a
    // "dropped outside everything" would float the panel on every ordinary
    // tab click. Only act if an actual drag happened.
    if (!e.mouseWasDraggedSinceMouseDown()) {
        overlay.hideOverlay();
        return;
    }

    auto targetZone = overlay.getActiveZone();
    overlay.hideOverlay();

    if (targetZone == DockTargetZone::None) {
        auto screenPos = e.getEventRelativeTo(&ownerWindow).getScreenPosition();
        floatPanelAt(panel, screenPos);
        return;
    }

    dockPanel(panel, targetZone);
}

void DockManager::handleFloatingWindowClosed(FloatingDockWindow* window, DockTargetZone redockZone)
{
    for (auto it = floatingWindows.begin(); it != floatingWindows.end(); ++it) {
        if (it->get() == window) {
            auto detached = window->detachPanel();
            floatingWindows.erase(it);

            if (detached) {
                auto* rawPanel = detached.get();
                registeredPanels.push_back(std::move(detached));
                dockPanel(rawPanel, redockZone);
            }
            break;
        }
    }
}

DockTargetZone DockManager::zoneContainingPanel(DockPanel* panel) const
{
    if (container.getLeftZone()->containsPanel(panel)) return DockTargetZone::Left;
    if (container.getRightZone()->containsPanel(panel)) return DockTargetZone::Right;
    if (container.getBottomZone()->containsPanel(panel)) return DockTargetZone::Bottom;
    return DockTargetZone::CenterTab;
}

DockPanel* DockManager::findPanelById(const juce::String& id) const
{
    for (auto& p : registeredPanels)
        if (p->getPanelID() == id) return p.get();

    DockZone* zones[] = { container.getLeftZone(), container.getCenterZone(), container.getRightZone(), container.getBottomZone() };
    for (auto* zone : zones) {
        for (int i = 0; i < zone->getNumPanels(); ++i) {
            auto* p = zone->getPanel(i);
            if (p != nullptr && p->getPanelID() == id) return p;
        }
    }

    for (auto& win : floatingWindows) {
        if (auto* p = win->getPanel())
            if (p->getPanelID() == id) return p;
    }

    return nullptr;
}

void DockManager::resetLayout()
{
    while (!floatingWindows.empty()) {
        auto detached = floatingWindows.front()->detachPanel();
        floatingWindows.erase(floatingWindows.begin());
        if (detached) registeredPanels.push_back(std::move(detached));
    }

    for (auto& entry : defaultLayout) {
        if (auto* panel = findPanelById(entry.first))
            dockPanel(panel, entry.second);
    }

    container.setLeftWidth(DockContainer::defaultLeftWidth);
    container.setRightWidth(DockContainer::defaultRightWidth);
    container.setBottomHeight(DockContainer::defaultBottomHeight);
}

juce::var DockManager::captureLayout() const
{
    auto zoneToVar = [](DockZone* zone) {
        juce::Array<juce::var> ids;
        for (int i = 0; i < zone->getNumPanels(); ++i)
            if (auto* p = zone->getPanel(i))
                ids.add(p->getPanelID());

        auto* obj = new juce::DynamicObject();
        obj->setProperty("panels", ids);
        obj->setProperty("currentTab", zone->getCurrentTabIndex());
        return juce::var(obj);
    };

    auto* zones = new juce::DynamicObject();
    zones->setProperty("left", zoneToVar(container.getLeftZone()));
    zones->setProperty("center", zoneToVar(container.getCenterZone()));
    zones->setProperty("right", zoneToVar(container.getRightZone()));
    zones->setProperty("bottom", zoneToVar(container.getBottomZone()));

    juce::Array<juce::var> floating;
    for (auto& win : floatingWindows) {
        if (auto* panel = win->getPanel()) {
            auto bounds = win->getBounds();
            auto* fobj = new juce::DynamicObject();
            fobj->setProperty("id", panel->getPanelID());
            fobj->setProperty("x", bounds.getX());
            fobj->setProperty("y", bounds.getY());
            fobj->setProperty("w", bounds.getWidth());
            fobj->setProperty("h", bounds.getHeight());
            floating.add(juce::var(fobj));
        }
    }

    auto* root = new juce::DynamicObject();
    root->setProperty("leftWidth", container.getLeftWidth());
    root->setProperty("rightWidth", container.getRightWidth());
    root->setProperty("bottomHeight", container.getBottomHeight());
    root->setProperty("zones", juce::var(zones));
    root->setProperty("floating", floating);

    return juce::var(root);
}

void DockManager::applyLayout(const juce::var& layout)
{
    if (!layout.isObject()) return;

    auto zonesVar = layout.getProperty("zones", {});

    auto applyZone = [this](const juce::var& zoneVar, DockTargetZone target) {
        if (!zoneVar.isObject()) return;

        if (auto* panelIds = zoneVar.getProperty("panels", {}).getArray()) {
            for (auto& idVar : *panelIds)
                if (auto* panel = findPanelById(idVar.toString()))
                    dockPanel(panel, target);
        }
    };

    applyZone(zonesVar.getProperty("left", {}), DockTargetZone::Left);
    applyZone(zonesVar.getProperty("right", {}), DockTargetZone::Right);
    applyZone(zonesVar.getProperty("bottom", {}), DockTargetZone::Bottom);
    applyZone(zonesVar.getProperty("center", {}), DockTargetZone::CenterTab);

    DockZone* namedZones[] = { container.getLeftZone(), container.getRightZone(), container.getBottomZone(), container.getCenterZone() };
    DockTargetZone namedTargets[] = { DockTargetZone::Left, DockTargetZone::Right, DockTargetZone::Bottom, DockTargetZone::CenterTab };
    for (int i = 0; i < 4; ++i) {
        auto savedZone = zonesVar.getProperty(zoneKeyFor(namedTargets[i]), {});
        if (savedZone.isObject())
            namedZones[i]->setCurrentTabIndex((int) savedZone.getProperty("currentTab", 0));
    }

    if (layout.hasProperty("leftWidth")) container.setLeftWidth((int) layout.getProperty("leftWidth", DockContainer::defaultLeftWidth));
    if (layout.hasProperty("rightWidth")) container.setRightWidth((int) layout.getProperty("rightWidth", DockContainer::defaultRightWidth));
    if (layout.hasProperty("bottomHeight")) container.setBottomHeight((int) layout.getProperty("bottomHeight", DockContainer::defaultBottomHeight));

    if (auto* floatingArr = layout.getProperty("floating", {}).getArray()) {
        for (auto& entryVar : *floatingArr) {
            auto id = entryVar.getProperty("id", {}).toString();
            auto* panel = findPanelById(id);
            if (panel == nullptr) continue;

            auto detached = extractPanel(panel);
            if (!detached) continue;

            juce::Rectangle<int> bounds(
                (int) entryVar.getProperty("x", 100),
                (int) entryVar.getProperty("y", 100),
                (int) entryVar.getProperty("w", 600),
                (int) entryVar.getProperty("h", 450));

            auto floatWin = std::make_unique<FloatingDockWindow>(panel->getTitle(), std::move(detached));
            floatWin->onWindowClosed = [this](FloatingDockWindow* win) {
                juce::MessageManager::callAsync([this, win] {
                    handleFloatingWindowClosed(win, DockTargetZone::CenterTab);
                });
            };
            floatWin->setBounds(bounds);
            floatingWindows.push_back(std::move(floatWin));
        }
    }

    container.resized();
}

void DockManager::saveLayoutToFile(const juce::File& file) const
{
    file.getParentDirectory().createDirectory();
    file.replaceWithText(juce::JSON::toString(captureLayout()));
}

bool DockManager::loadLayoutFromFile(const juce::File& file)
{
    if (!file.existsAsFile()) return false;

    auto parsed = juce::JSON::parse(file.loadFileAsString());
    if (!parsed.isObject()) return false;

    applyLayout(parsed);
    return true;
}

} // namespace CreationDock
