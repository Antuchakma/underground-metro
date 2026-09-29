#include "player.h"
#include <algorithm>
#include <cmath>

using namespace glm;

namespace {
constexpr float RADIUS = 0.25f;
constexpr float HEIGHT = 1.8f;
constexpr float STEP = 0.35f;
constexpr float EYE = 1.65f;
constexpr float GRAVITY = 18.0f;
}

vec3 Player::forward() const {
    return vec3(std::cos(pitch) * std::cos(yaw), std::sin(pitch), std::cos(pitch) * std::sin(yaw));
}

vec3 Player::eye() const { return vec3(feet.x, eyeY, feet.z); }

void Player::respawn(vec3 p, float yawAngle) {
    feet = p;
    yaw = yawAngle;
    pitch = 0.0f;
    hvel = vec3(0.0f);
    vy = 0.0f;
    carrier = -1;
    conveyor = 0.0f;
    eyeY = p.y + EYE;
}

void Player::moveAxis(vec3 d, const World& w) {
    int n = std::max(1, int(std::ceil(length(d) / 0.05f)));
    vec3 s = d / float(n);
    for (int i = 0; i < n; ++i) {
        vec3 np = feet + s;
        if (w.blocked(np, RADIUS, HEIGHT, STEP)) break;
        feet = np;
    }
}

void Player::update(float dt, const PlayerInput& in, const World& w) {
    vec3 fwd(std::cos(yaw), 0, std::sin(yaw));
    vec3 right(-std::sin(yaw), 0, std::cos(yaw));

    if (noclip) {
        vec3 v = forward() * in.forward + right * in.right + vec3(0, 1, 0) * in.up;
        feet += v * (in.run ? 25.0f : 8.0f) * dt;
        eyeY = feet.y + EYE;
        vy = 0.0f;
        carrier = -1;
        return;
    }

    // Carried along by whatever we are standing on
    if (carrier >= 0) feet.x += w.trains[carrier].delta;
    if (conveyor != 0.0f) moveAxis(vec3(conveyor * dt, 0, 0), w);

    vec3 wish = fwd * in.forward + right * in.right;
    if (length(wish) > 1.0f) wish = normalize(wish);
    wish *= in.run ? 6.5f : 3.2f;
    float k = 1.0f - std::exp(-(grounded ? 12.0f : 2.5f) * dt);
    hvel += (wish - hvel) * k;

    vec3 d = hvel * dt;
    moveAxis(vec3(d.x, 0, 0), w);
    moveAxis(vec3(0, 0, d.z), w);
    w.pushOut(feet, RADIUS, HEIGHT, STEP);

    GroundInfo g = w.ground(feet, RADIUS, STEP);
    if (in.jump && grounded) {
        vy = 5.2f;
        grounded = false;
    }
    if (grounded && vy <= 0.0f && g.found && feet.y - g.height < 0.5f) {
        feet.y = g.height; // walk up steps / stick to ramps going down
        vy = 0.0f;
    } else {
        vy -= GRAVITY * dt;
        feet.y += vy * dt;
        if (g.found && feet.y <= g.height) {
            feet.y = g.height;
            vy = 0.0f;
            grounded = true;
        } else {
            grounded = false;
        }
    }
    if (grounded && g.found) { carrier = g.carrier; conveyor = g.conveyor; }
    else { carrier = -1; conveyor = 0.0f; }

    if (feet.y < -30.0f) respawn({-22.0f, 0.0f, 4.6f}, 3.14159265f);

    // Smooth the camera height over stair steps; snap on big jumps (teleports, train wrap-around)
    float target = feet.y + EYE;
    if (std::fabs(target - eyeY) > 1.0f) eyeY = target;
    else eyeY += (target - eyeY) * (1.0f - std::exp(-18.0f * dt));
}
