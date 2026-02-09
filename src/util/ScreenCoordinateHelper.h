#pragma once
#include <GfxRenderer.h>
#include <HalDisplay.h>

// Converts physical (panel) coordinates to logical coordinates for the current orientation.
// (Inverse of GfxRenderer's rotateCoordinates)
inline void physicalToLogical(int phyX, int phyY, GfxRenderer::Orientation orientation, int* x, int* y) {
    switch (orientation) {
        case GfxRenderer::Portrait:
            *x = HalDisplay::DISPLAY_HEIGHT - 1 - phyY;
            *y = phyX;
            break;
        case GfxRenderer::LandscapeClockwise:
            *x = HalDisplay::DISPLAY_WIDTH - 1 - phyX;
            *y = HalDisplay::DISPLAY_HEIGHT - 1 - phyY;
            break;
        case GfxRenderer::PortraitInverted:
            *x = phyY;
            *y = HalDisplay::DISPLAY_WIDTH - 1 - phyX;
            break;
        case GfxRenderer::LandscapeCounterClockwise:
        default:
            *x = phyX;
            *y = phyY;
            break;
    }
}
