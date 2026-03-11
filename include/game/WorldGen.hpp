#pragma once
#include "FastNoiseAPI.hpp"
#include <cstdio>

// Biome Types
enum class BiomeType {
    Ocean,
    Beach,
    Plains,
    Forest,
    Mountain
};

struct Tree {
    int x, y, z;
    int height;
    int trunkType; // BlockType::Wood
    int leafType;  // BlockType::Leaves
    uint8_t leafVariant;
};

struct TerrainNoise {
    FastNoiseLite continental;
    FastNoiseLite moisture;
    FastNoiseLite mountainBase; // Defines the "mass" of the mountain
    FastNoiseLite peaks;        // Defines the "jagged rocks" mountain part

    float freqContinental = 0.0005f;
    float freqMoisture = 0.005f;
    float freqMountainBase = 0.003f;
    float freqPeaks = 0.01f;

    int seed;
    bool regen = false; // Flag to indicate regeneration needed

    int SEA_LEVEL = 64;
    int SNOW_LEVEL = 110;

    explicit TerrainNoise(const int seed): seed(seed) {
        update(seed);
        regen = false; // Initial setup does not need regeneration
    }

    void update(const int newSeed = 0) {
        if (newSeed != 0) seed = newSeed;
        printf("Update World: Seed=%d, FreqCont=%.5f, FreqMoist=%.5f, FreqMountBase=%.5f, FreqPeaks=%.5f\n",
            seed, freqContinental, freqMoisture, freqMountainBase, freqPeaks);

        // Continentalness: Very smooth, defines the transitions (Plains -> Mountain)
        continental.SetSeed(seed);
        continental.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        continental.SetFrequency(freqContinental); // VERY LOW: Large biomes
        continental.SetFractalType(FastNoiseLite::FractalType_FBm);
        continental.SetFractalOctaves(2);

        // Moisture (No change)
        moisture.SetSeed(seed + 1234);
        moisture.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        moisture.SetFrequency(freqMoisture);
        moisture.SetFractalType(FastNoiseLite::FractalType_FBm);
        moisture.SetFractalOctaves(2);

        // Mountain Base: The "Body" of the mountain
        // Smooth rolling noise that creates the general elevation
        mountainBase.SetSeed(seed + 5555);
        mountainBase.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        mountainBase.SetFrequency(freqMountainBase); // Wide features
        mountainBase.SetFractalType(FastNoiseLite::FractalType_FBm);
        mountainBase.SetFractalOctaves(3); // Smooth, not jagged

        // Peaks: The "Detail"
        // Jagged, but we reduce frequency so they aren't single-block spikes
        peaks.SetSeed(seed + 4321);
        peaks.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        peaks.SetFrequency(freqPeaks); // Higher freq than base
        peaks.SetFractalType(FastNoiseLite::FractalType_Ridged);
        peaks.SetFractalOctaves(5); // Lots of detail (crunchy rocks)

        regen = true;
    }
};

struct SplinePoint {
    float noiseVal;
    float heightVal;
};

namespace WorldGen {
    // Fast small hash (splitmix64) — good for per-coordinate deterministic bits.
    static inline uint64_t splitmix64(uint64_t x) {
        x += 0x9e3779b97f4a7c15ULL;
        x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
        x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
        return x ^ (x >> 31);
    }

    // Cheap, fast deterministic 0-99 chance based on world coordinates
    static inline uint64_t ChancePercentFromCoords(int wx, int wz) {
        uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(wx)) << 32)
                     | static_cast<uint32_t>(wz);
        uint64_t h = splitmix64(key);
        return (h % 100ULL);
    }

    const std::vector<SplinePoint> TERRAIN_SPLINE = {
        { -1.0f, 30.0f },   // Deep Ocean
        { -0.2f, 62.0f },   // Shallow Water
        {  0.0f, 65.0f },   // Shoreline
        {  0.1f, 68.0f },   // Flat Plains start
        {  0.3f, 75.0f },   // Flat Plains end (Very slow rise = flat valley)
        {  0.35f, 100.0f},  // Foothills (Steep rise starts)
        {  0.55f, 160.0f},  // Cliffs (Very steep)
        {  1.0f, 220.0f}    // High Peaks
    };

    static inline float GetSplineHeight(float noiseVal) {
        // Find which two points our noise is between
        for (size_t i = 0; i < TERRAIN_SPLINE.size() - 1; ++i) {
            if (noiseVal >= TERRAIN_SPLINE[i].noiseVal && noiseVal <= TERRAIN_SPLINE[i+1].noiseVal) {
                // Linear Interpolation:
                // Percentage of how far we are between point A and point B
                float t = (noiseVal - TERRAIN_SPLINE[i].noiseVal) /
                          (TERRAIN_SPLINE[i+1].noiseVal - TERRAIN_SPLINE[i].noiseVal);

                // Lerp
                return TERRAIN_SPLINE[i].heightVal + t * (TERRAIN_SPLINE[i+1].heightVal - TERRAIN_SPLINE[i].heightVal);
            }
        }
        return TERRAIN_SPLINE.back().heightVal; // Fallback
    }
}
