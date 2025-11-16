#pragma once

class Settings {
public:
    Settings();
    ~Settings();

    bool VSync = true;
    float GLFrom = 0.1f;
    float GLTo = 100.0f;
};
