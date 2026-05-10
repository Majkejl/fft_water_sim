#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

/* Orbit camera that always looks at the world origin (0, 0, 0) with Z as the up axis.
   The eye position is expressed in spherical coordinates: azimuth (theta), elevation
   (phi), and distance (radius). Elevation is clamped to ±85° to avoid gimbal lock.

   GLFW input callbacks live in Application — they call orbit() and zoom() directly. */
struct Camera {
    float theta  = 0.8f;   /* azimuth angle in radians — rotation in the XY plane */
    float phi    = 0.4f;   /* elevation angle in radians — angle above/below XY plane */
    float radius = 3.0f;   /* distance from the origin */

    /* Returns the eye position in world space derived from the spherical coordinates. */
    glm::vec3 eye() const;

    /* Returns a view matrix from eye() looking toward the origin (Z-up). */
    glm::mat4 view() const;

    /* Adjusts theta by dtheta and phi by dphi (both in radians). Elevation is clamped. */
    void orbit(float dtheta, float dphi);

    /* Multiplies radius by 0.9^delta (scroll-wheel zoom). Radius is clamped to [0.5, 50]. */
    void zoom(float delta);
};
