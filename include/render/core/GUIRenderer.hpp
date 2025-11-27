#pragma once
#include "Camera.hpp"
#include "game/Player.hpp"
#include "game/Settings.hpp"
#include "game/World.hpp"
#include "render/gl/GLChunkRenderer.hpp"

namespace GUIRenderer {
    /**
     * Top left corner.
     * Shows an overview of stats such as FPS, vertex count, etc.
     */
    void RenderStatsOverview(std::size_t vertexCount, const Camera& camera, const Settings& settings);

    /**
     * Renders the settings screen.
     */
    void RenderSettingsScreen(Settings& settings, Camera& camera, GLChunkRenderer* chunkRenderer, World& world, Player* player);
}
