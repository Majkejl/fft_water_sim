#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

/* Orbit camera that always looks at the world origin (0, 0, 0) with Z as the up axis.
   The eye position is expressed in spherical coordinates: azimuth (theta), elevation
   (phi), and distance (radius). Elevation is clamped to ±85° to avoid gimbal lock.

   Sensitivity and zoom clamp values are read from CameraConfig at startup. */
struct Camera {
    float theta  = 0.f;   /* azimuth angle, radians */
    float phi    = 0.f;   /* elevation angle, radians */
    float radius = 1.f;   /* distance from the origin */

    float orbit_sensitivity = 0.005f;
    float zoom_factor       = 0.9f;
    float zoom_min          = 0.5f;
    float zoom_max          = 300.f;

    glm::vec3 eye()  const;
    glm::mat4 view() const;

    void orbit(float dtheta, float dphi);
    void zoom(float delta);
};
