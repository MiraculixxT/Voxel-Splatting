#pragma once
#include "FastNoiseAPI.hpp"

// Define world constants
constexpr int SEA_LEVEL = 64;
constexpr int SNOW_LEVEL = 110;

// Biome Types
enum class BiomeType {
    Ocean,
    Beach,
    Plains,
    Forest,
    Mountain
};

struct TerrainNoise {
    FastNoiseLite continental;
    FastNoiseLite moisture;
    FastNoiseLite mountainBase; // Defines the "mass" of the mountain
    FastNoiseLite peaks;        // Defines the "jagged rocks" mountain part

    float freqContinental = 0.005f;
    float freqMoisture = 0.005f;
    float freqMountainBase = 0.003f;
    float freqPeaks = 0.01f;

    int seed;
    bool regen = false; // Flag to indicate regeneration needed

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
