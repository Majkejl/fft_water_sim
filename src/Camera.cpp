#include "Camera.h"

glm::vec3 Camera::eye() const
{
    return {
        radius * std::cos(phi) * std::cos(theta),
        radius * std::cos(phi) * std::sin(theta),
        radius * std::sin(phi)
    };
}

glm::mat4 Camera::view() const
{
    return glm::lookAt(eye(), glm::vec3(0.f), glm::vec3(0.f, 0.f, 1.f));
}

void Camera::orbit(float dtheta, float dphi)
{
    theta += dtheta;
    phi    = glm::clamp(phi + dphi, glm::radians(-85.f), glm::radians(85.f));
}

void Camera::zoom(float delta)
{
    radius = glm::clamp(radius * std::pow(0.9f, delta), 0.5f, 50.f);
}
