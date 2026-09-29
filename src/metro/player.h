// First-person walker with collision, stairs/escalators and riding on trains.
#pragma once
#include "world.h"
#include <glm/glm.hpp>

struct PlayerInput {
    float forward = 0, right = 0, up = 0; // -1..1
    bool run = false, jump = false;
};

class Player {
public:
    glm::vec3 feet{-22.0f, 0.0f, 4.6f};
    float yaw = 3.14159265f; // facing -x, towards the approaching train
    float pitch = 0.0f;
    bool noclip = false;

    void update(float dt, const PlayerInput& in, const World& w);
    void respawn(glm::vec3 p, float yawAngle);
    glm::vec3 eye() const;
    glm::vec3 forward() const;
    bool ridingTrain() const { return carrier >= 0; }

private:
    glm::vec3 hvel{0.0f};
    float vy = 0.0f;
    bool grounded = false;
    int carrier = -1;
    float conveyor = 0.0f;
    float eyeY = 1.65f;

    void moveAxis(glm::vec3 d, const World& w);
};
