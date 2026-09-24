#include "FrustyComponent.h"
#include <FrustIDEAssets.h>

FrustyComponent::FrustyComponent()
{
    atlas = juce::ImageFileFormat::loadFrom(
        FrustIDEAssets::FrustySpriteAtlas_png,
        FrustIDEAssets::FrustySpriteAtlas_pngSize);
    setTooltip("Frusty, the FrustIDE engineer");
    setInterceptsMouseClicks(false, false);
    startTimer(120);
}

void FrustyComponent::paint(juce::Graphics& g)
{
    if (!atlas.isValid())
        return;

    constexpr int columns = 4;
    constexpr int rows = 3;
    const int cellWidth = atlas.getWidth() / columns;
    const int cellHeight = atlas.getHeight() / rows;
    const int frame = static_cast<int>(mood);
    const int sourceX = (frame % columns) * cellWidth;
    const int sourceY = (frame / columns) * cellHeight;

    const int side = juce::jmin(getWidth(), getHeight()) - 2;
    auto target = getLocalBounds().withSizeKeepingCentre(side, side);
    if (bobUp)
        target.translate(0, -1);

    g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
    g.drawImage(atlas,
                target.getX(), target.getY(), target.getWidth(), target.getHeight(),
                sourceX, sourceY, cellWidth, cellHeight, false);
}

void FrustyComponent::showMood(Mood newMood, int holdMilliseconds)
{
    mood = newMood;
    transientTicks = holdMilliseconds > 0 ? juce::jmax(1, holdMilliseconds / 120) : 0;
    idleTicks = 0;
    bobUp = false;
    repaint();
}

void FrustyComponent::timerCallback()
{
    if (transientTicks > 0)
    {
        --transientTicks;
        bobUp = !bobUp;
        if (transientTicks == 0)
        {
            mood = Mood::idle;
            bobUp = false;
        }
        repaint();
        return;
    }

    if (mood != Mood::idle)
        return;

    if (++idleTicks < nextIdleGesture)
        return;

    idleTicks = 0;
    nextIdleGesture = juce::Random::getSystemRandom().nextInt({ 110, 220 });
    mood = juce::Random::getSystemRandom().nextBool() ? Mood::glassesAdjust : Mood::sigh;
    transientTicks = 6;
    repaint();
}
