#pragma once
#include <array>
#include <glm/glm.hpp>

struct View {
    glm::mat4 view;
    glm::vec3 position;
};  

class CameraViewControl
{
public:
    enum class CameraKey { Forward, Backward, Left, Right, Up, Down, Count };

private:
    static inline const glm::vec3 WorldUp{0.0f, 1.0f, 0.0f};

    std::array<bool, static_cast<size_t>(CameraKey::Count)> m_cameraKeys{};
    glm::vec3 m_cameraPosition{0.5f, 0.5f, 0.5f};
    glm::vec3 m_cameraForward = glm::normalize(glm::vec3{0., 0.3, 0.} - m_cameraPosition);

public:
    void setCameraKey(CameraKey key, bool pressed)
    {
        m_cameraKeys[static_cast<size_t>(key)] = pressed;
    }

    void clearCameraKeys()
    {
        m_cameraKeys.fill(false);
    }

    void rotateCamera(float dx, float dy)
    {
        constexpr float sensitivity = 0.003f;
        const float pitchLimit = glm::radians(89.0f);
        const auto& forward = m_cameraForward;
        const float yaw = std::atan2(forward.z, forward.x) + dx * sensitivity;
        const float pitch = std::clamp(std::asin(std::clamp(forward.y, -1.0f, 1.0f)) - dy * sensitivity,
            -pitchLimit, pitchLimit);
        m_cameraForward = glm::vec3(
            std::cos(pitch) * std::cos(yaw), std::sin(pitch), std::cos(pitch) * std::sin(yaw));
    }

    void updateCamera(float deltaTime)
    {
        const auto axis = [this](CameraKey positive, CameraKey negative) {
            return float(m_cameraKeys[static_cast<size_t>(positive)]) -
                float(m_cameraKeys[static_cast<size_t>(negative)]);
        };
        const glm::vec3 forward = m_cameraForward;
        const glm::vec3 right = glm::normalize(glm::cross(forward, WorldUp));
        const glm::vec3 movement = forward * axis(CameraKey::Forward, CameraKey::Backward) +
            right * axis(CameraKey::Right, CameraKey::Left) + WorldUp * axis(CameraKey::Up, CameraKey::Down);
        if (glm::dot(movement, movement) > 0.0f) {
            constexpr float moveSpeed = 0.5f;
            m_cameraPosition += glm::normalize(movement) * moveSpeed * std::min(deltaTime, 0.1f);
        }
    }

    View calculateView() {
        return View {
            .view = glm::lookAt(m_cameraPosition, m_cameraPosition + m_cameraForward, WorldUp),
            .position = m_cameraPosition
        };
    }
};