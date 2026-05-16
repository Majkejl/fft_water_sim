#pragma once

#include <glm/gtc/matrix_transform.hpp>
#include <cstdint>

/* Build-time constants — sizing GPU allocations. Changing these requires a full rebuild. */
static constexpr uint32_t MESH_SIZE    = 256;
static constexpr uint32_t TEXTURE_SIZE = 256;
static constexpr uint32_t TEXTURE_LOG  = 8;  /* must equal log2(TEXTURE_SIZE) */

struct OceanConfig {
    float  patch_size     = 64.f;       /* physical patch width, metres */
    float  lambda         = 30.f;       /* choppiness scale: applied to XY displacement and Jacobian */
    double wave_amplitude = 15.0;       /* JONSWAP spectral scale */
    double fetch          = 250000.0;   /* wind fetch length, metres */
    double wind_x         = 40.0;       /* wind velocity x, m/s */
    double wind_y         = 0.0;        /* wind velocity y, m/s */
    double enhancement    = 3.3;        /* JONSWAP peak enhancement gamma */
};

struct FoamConfig {
    float threshold = 0.97f;    /* Jacobian threshold; foam accumulates when J < threshold */
    float erosion   = 0.95f;  /* per-frame multiplicative decay */
    float foam_add  = 2.f;    /* accumulation rate per breaking pixel */
};

/* Build-time camera defaults — not exposed via ImGui, adjust here and rebuild. */
struct CameraConfig {
    float theta             = glm::quarter_pi<float>() + glm::pi<float>();  /* initial azimuth: opposite sun direction */
    float phi               = 0.10f;     /* initial elevation, radians */
    float radius_factor     = 1.5f;      /* initial radius = patch_size * radius_factor */
    float orbit_sensitivity = 0.005f;    /* radians per pixel */
    float zoom_factor       = 0.9f;      /* zoom multiplier per scroll tick */
    float zoom_min          = 0.5f;      /* minimum camera radius */
    float zoom_max          = 300.f;     /* maximum camera radius */
};

struct AppConfig {
    int cubemap_index = 22;
    int window_width  = 1280;
    int window_height = 720;
};

struct SimulationConfig {
    OceanConfig  ocean;
    FoamConfig   foam;
    CameraConfig camera;
    AppConfig    app;
};
