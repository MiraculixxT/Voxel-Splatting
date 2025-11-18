#include "render/core/GUIRenderer.hpp"

#include <cstdio>
#include "imgui.h"
#include "game/Settings.hpp"
#include "game/World.hpp"
#include "GLFW/glfw3.h"
#include "render/core/Camera.hpp"
#include "render/gl/GLChunkRenderer.hpp"
#include "utils/Formatting.hpp"

void GUIRenderer::RenderStatsOverview(const int& vertexCount) {
    constexpr ImGuiWindowFlags window_flags =
        ImGuiWindowFlags_NoDecoration |       // No title bar, borders, etc.
        ImGuiWindowFlags_AlwaysAutoResize | // Auto-resize to fit content
        ImGuiWindowFlags_NoSavedSettings |  // Don't save position/size
        ImGuiWindowFlags_NoFocusOnAppearing | // Don't steal focus
        ImGuiWindowFlags_NoNav |            // Disable keyboard/gamepad navigation
        ImGuiWindowFlags_NoMove;            // Can't be moved

    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.35f);

    ImGui::Begin("Stats Overview", nullptr, window_flags);

    const std::string vertCountStr = VSUtils::NumberFormatThousands(vertexCount);
    ImGui::Text("FPS: %.1f | Frame Time: %.3fms | Verts: %s", ImGui::GetIO().Framerate, 1000.0f / ImGui::GetIO().Framerate, vertCountStr.c_str());

    ImGui::End();
}

void GUIRenderer::RenderSettingsScreen(Settings& settings, Camera& camera, GLChunkRenderer* chunkRenderer, World& world) {
    constexpr ImGuiWindowFlags window_flags =
        ImGuiWindowFlags_NoCollapse |       // Prevent collapsing
        ImGuiWindowFlags_AlwaysAutoResize; // Auto-resize to fit content

    const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
    ImVec2 center_pos = main_viewport->GetCenter();
    ImGui::SetNextWindowPos(center_pos, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    ImGui::Begin("Settings", nullptr, window_flags);

    if (ImGui::Button("Re-build Chunk")) {
        chunkRenderer->RemoveAllMeshes();
        for (auto [cx, column] : world.getChunks()) {
            for (auto& [cy, chunk] : column) {
                chunk.BuildMesh(world);
                chunkRenderer->UploadMesh(cx, cy, chunk.GetMeshVertices());
                //printf("DEBUG: Re-built chunk (%d, %d) with %d vertices\n", cx, cy, chunk.GetVertexCount());
            }
        }
    }

    ImGui::Text("----- GRAPHICS -----");
    if (ImGui::Checkbox("VSync", &settings.VSync))
        glfwSwapInterval(settings.VSync ? 1 : 0);

    ImGui::Text("----- LAYERING -----");
    ImGui::SliderFloat("GL From", &settings.GLFrom, 0.01f, 50.0f);
    ImGui::SliderFloat("GL To", &settings.GLTo, settings.GLFrom, 500.0f);
    ImGui::SliderFloat("Fog Start", &settings.FogStartMult, 0.0f, 1.0f);
    ImGui::SliderFloat("Fog End", &settings.FogEndMult, 1.0f, 5.0f);

    ImGui::Text("----- CAMERA -----");
    ImGui::Text("Pos: (%.1f, %.1f, %.1f)", camera.Position.x, camera.Position.y, camera.Position.z);
    ImGui::SliderFloat("Speed", &camera.MovementSpeed, 1.0f, 20.0f);
    ImGui::SliderFloat("FOV", &camera.Zoom, 1.0f, 90.0f);

    ImGui::End();
}
