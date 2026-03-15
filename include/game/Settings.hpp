#pragma once

class Settings {
public:
    Settings();
    ~Settings();

    // Toggle between as much FPS as possible and syncing to monitor refresh rate
    bool VSync = true;

    // OpenGL clipping planes
    float GLFrom = 0.1f;
    float GLTo = 200.0f;
    bool GLGeometry = true;
    bool GLPLY = false;

    // Fog settings
    float FogStartMult = 0.35;
    float FogEndMult = 1.0;

};
