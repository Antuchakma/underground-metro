#include "world.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <ctime>

using namespace glm;
using namespace dim;

namespace {

constexpr float PI = 3.14159265358979f;
constexpr float TAU = 2.0f * PI;

// Trains
constexpr int CARS = 5;
constexpr float CAR_PITCH = 19.6f;               // car length 19 m + 0.6 m gangway
constexpr float DOOR_X[3] = {-6.2f, 0.0f, 6.2f}; // door centres within a car
constexpr float T_VMAX = 16.0f, T_ACC = 1.1f, T_DEC = 1.2f;
constexpr float T_WRAP = 265.0f;                 // trains re-enter from the far tunnel here
constexpr float T_DWELL = 24.0f, T_LAYOVER = 14.0f;

// Escalator bank (runs along +x from the platform up to the concourse)
constexpr float EX0 = -8.5f, EI0 = -7.0f, EI1 = 7.0f, EX1 = 8.5f;
constexpr float HOLE_X0 = -2.0f, HOLE_X1 = 7.0f, HOLE_Z = 2.6f;
constexpr float ESC_SPEED = 0.5f;

const vec3 WARM(1.0f, 0.93f, 0.82f);
const vec3 COOL(0.92f, 0.96f, 1.0f);
const vec3 LINE_BLUE(0.08f, 0.32f, 0.8f);

const Material mFloor = material({0.55f, 0.53f, 0.50f}, PAT_FLOOR_TILE, 80, 0.6f);
const Material mConcFloor = material({0.62f, 0.60f, 0.57f}, PAT_FLOOR_TILE, 80, 0.6f);
const Material mWallTile = material({0.86f, 0.86f, 0.83f}, PAT_WALL_TILE, 60, 0.45f);
const Material mAccent = material(LINE_BLUE, PAT_WALL_TILE, 60, 0.45f);
const Material mConcrete = material({0.42f, 0.41f, 0.39f}, PAT_CONCRETE, 8, 0.05f);
const Material mTunnel = material({0.30f, 0.29f, 0.28f}, PAT_CONCRETE, 8, 0.05f);
const Material mCeiling = material({0.55f, 0.57f, 0.60f}, PAT_METAL, 24, 0.15f);
const Material mCeilingLight = material({0.74f, 0.75f, 0.77f}, PAT_PLAIN, 16, 0.1f);
const Material mBeam = material({0.25f, 0.27f, 0.30f}, PAT_METAL, 32, 0.3f);
const Material mSteel = material({0.62f, 0.64f, 0.67f}, PAT_METAL, 64, 0.7f);
const Material mCladding = material({0.50f, 0.53f, 0.57f}, PAT_METAL, 64, 0.6f);
const Material mDarkSteel = material({0.12f, 0.13f, 0.15f}, PAT_METAL, 48, 0.4f);
const Material mRail = material({0.50f, 0.47f, 0.44f}, PAT_METAL, 96, 1.0f);
const Material mSleeper = material({0.38f, 0.36f, 0.33f}, PAT_CONCRETE, 8, 0.05f);
const Material mBallast = material({0.36f, 0.33f, 0.30f}, PAT_BALLAST, 8, 0.05f);
const Material mTactile = material({0.95f, 0.72f, 0.08f}, PAT_TACTILE, 32, 0.3f);
const Material mYellow = material({0.95f, 0.75f, 0.10f}, PAT_PLAIN, 32, 0.3f);
const Material mStripes = material({0.95f, 0.75f, 0.10f}, PAT_STRIPES, 16, 0.2f);
const Material mWhite = material({0.9f, 0.9f, 0.86f}, PAT_PLAIN, 16, 0.2f);
const Material mGlass = material({0.55f, 0.70f, 0.75f}, PAT_GLASS, 128, 1.0f);
const Material mRedGlass = material({0.9f, 0.15f, 0.12f}, PAT_GLASS, 128, 1.0f);
const Material mRubber = material({0.04f, 0.04f, 0.045f}, PAT_PLAIN, 24, 0.3f);
const Material mWood = material({0.50f, 0.32f, 0.17f}, PAT_PLAIN, 24, 0.2f);
const Material mColumn = material({0.80f, 0.80f, 0.78f}, PAT_METAL, 48, 0.5f);
const Material mStep = material({0.45f, 0.46f, 0.48f}, PAT_GROOVES, 48, 0.6f);
const Material mStone = material({0.62f, 0.60f, 0.57f}, PAT_CONCRETE, 32, 0.25f);
const Material mFixture = emissive({1.0f, 0.95f, 0.88f}, 6.0f);
const Material mLed = emissive({0.2f, 0.7f, 1.0f}, 3.0f);

mat4 boxXf(vec3 c, vec3 s) { return scale(translate(mat4(1.0f), c), s); }

// Bar following the line (x0,y0)-(x1,y1), spanning [yb, yt] above that line and [z0, z1] across.
void slopedBar(MeshBuilder& b, float x0, float y0, float x1, float y1, float yb, float yt,
               float z0, float z1, const Material& m) {
    if (z0 > z1) std::swap(z0, z1);
    vec3 c[8];
    for (int i = 0; i < 8; ++i) {
        float x = (i & 1) ? x1 : x0;
        float y = ((i & 1) ? y1 : y0) + ((i & 2) ? yt : yb);
        float z = (i & 4) ? z1 : z0;
        c[i] = vec3(x, y, z);
    }
    b.hexa(c, m);
}

struct Rng {
    uint32_t s;
    float next() { s = s * 1664525u + 1013904223u; return (s >> 8) * (1.0f / 16777216.0f); }
};

// Blocky glyph-like strokes that read as text from a distance.
void fakeText(MeshBuilder& b, vec3 start, vec3 right, vec3 normal, float h, int chars,
              uint32_t seed, const Material& m) {
    Rng r{seed};
    const vec3 up(0, 1, 0);
    float cw = h * 0.6f, gap = h * 0.28f, t = h * 0.16f, u = 0.0f;
    auto stroke = [&](float u0, float u1, float v0, float v1) {
        vec3 a = start + right * u0 + up * v0 + normal * 0.004f;
        vec3 c = start + right * u1 + up * v1 + normal * 0.014f;
        b.aabb(a, c, m);
    };
    for (int i = 0; i < chars; ++i) {
        if (i > 0 && i < chars - 1 && r.next() < 0.14f) { u += cw; continue; }
        int mask = 0;
        while (__builtin_popcount(mask) < 2) mask = int(r.next() * 32.0f) & 31;
        if (mask & 1) stroke(u, u + t, 0, h);
        if (mask & 2) stroke(u + cw - t, u + cw, 0, h);
        if (mask & 4) stroke(u, u + cw, h - t, h);
        if (mask & 8) stroke(u, u + cw, h * 0.5f - t * 0.5f, h * 0.5f + t * 0.5f);
        if (mask & 16) stroke(u, u + cw, 0, t);
        u += cw + gap;
    }
}

// A sign panel with fake text on the face pointing along `normal` (and optionally the back).
void sign(MeshBuilder& b, vec3 c, vec3 right, vec3 normal, float w, float h, vec3 bg,
          uint32_t seed, int chars, bool bothSides) {
    vec3 half = abs(right) * (w * 0.5f) + vec3(0, h * 0.5f, 0) + abs(normal) * 0.05f;
    b.aabb(c - half, c + half, material(bg, PAT_PLAIN, 32, 0.3f));
    float th = h * 0.42f;
    float tw = chars * th * 0.88f;
    Material txt = emissive({1.0f, 1.0f, 0.95f}, 2.2f);
    for (int side = 0; side < (bothSides ? 2 : 1); ++side) {
        vec3 n = side ? -normal : normal;
        vec3 rt = side ? -right : right;
        vec3 start = c + n * 0.05f - rt * (tw * 0.5f) - vec3(0, th * 0.5f, 0);
        fakeText(b, start, rt, n, th, chars, seed + side * 77, txt);
    }
}

float smooth01(float x) { x = clamp(x, 0.0f, 1.0f); return x * x * (3.0f - 2.0f * x); }

bool overlapXZ(const Box& b, vec3 p, float r) {
    return p.x + r > b.mn.x && p.x - r < b.mx.x && p.z + r > b.mn.z && p.z - r < b.mx.z;
}

} // namespace

// ---------------------------------------------------------------------------------------------
// Ramps and trains
// ---------------------------------------------------------------------------------------------

float Ramp::heightAt(float x) const {
    if (x <= ix0) return y0;
    if (x >= ix1) return y1;
    if (stepRun > 0.0f) {
        float rise = stepRun * (y1 - y0) / (ix1 - ix0);
        int i = int(std::floor((x - ix0) / stepRun));
        return std::min(y1, y0 + (i + 1) * rise);
    }
    return y0 + (x - ix0) / (ix1 - ix0) * (y1 - y0);
}

float Ramp::conveyorAt(float x) const {
    if (speed == 0.0f) return 0.0f;
    if (x > ix0 && x < ix1) {
        float run = ix1 - ix0, rise = y1 - y0;
        return speed * run / std::sqrt(run * run + rise * rise);
    }
    return speed;
}

float Train::eta() const {
    switch (state) {
    case DWELL: return 0.0f;
    case APPROACH: return (-x * dir) / std::max(v, 9.0f);
    case LAYOVER: return timer + T_WRAP / 9.0f;
    case DEPART: return (T_WRAP - x * dir) / std::max(v, 9.0f) + T_LAYOVER + T_WRAP / 9.0f;
    }
    return 0.0f;
}

// ---------------------------------------------------------------------------------------------
// Static construction
// ---------------------------------------------------------------------------------------------

void World::deco(vec3 mn, vec3 mx, const Material& m) { staticOpaque.aabb(mn, mx, m); }
void World::collider(vec3 a, vec3 b) { staticBoxes.push_back({min(a, b), max(a, b), -1}); }
void World::solid(vec3 mn, vec3 mx, const Material& m) { deco(mn, mx, m); collider(mn, mx); }

void World::addLight(vec3 p, float radius, vec3 color, float intensity, bool down) {
    Light l;
    l.pos = p;
    l.radius = radius;
    l.color = color * intensity;
    if (down) { l.dir = vec3(0, -1, 0); l.cosOuter = -0.05f; } // ceiling fixture: lights the room, not the ceiling
    staticLights.push_back(l);
}

void World::build() {
    staticOpaque.clear();
    staticGlass.clear();
    staticBoxes.clear();
    staticLights.clear();
    ramps.clear();

    Train& a = trains[0];
    a.dir = 1.0f; a.z = TZ; a.x = -170.0f; a.v = T_VMAX; a.state = Train::APPROACH;
    a.lineColor = LINE_BLUE;
    Train& b = trains[1];
    b.dir = -1.0f; b.z = -TZ; b.x = T_WRAP; b.v = 0.0f; b.state = Train::LAYOVER; b.timer = 30.0f;
    b.lineColor = vec3(0.9f, 0.45f, 0.05f);

    // Platform screen gate openings line up with the train doors at the stopping point (x = 0).
    doorGapX.clear();
    for (int i = 0; i < CARS; ++i)
        for (float dc : DOOR_X) doorGapX.push_back((2 - i) * CAR_PITCH + dc);
    std::sort(doorGapX.begin(), doorGapX.end());
    fareGateOpen.assign(9, 0.0f);

    buildPlatformLevel();
    buildTracksAndTunnels();
    buildEscalatorBank();
    buildConcourse();
    buildExit();
}

void World::buildPlatformLevel() {
    // Island platform
    collider({-PL, BED - 0.5f, -PW}, {PL, 0, PW});
    deco({-PL, BED - 0.5f, -PW}, {PL, -0.05f, PW}, mConcrete);
    deco({-PL, -0.05f, -5.4f}, {PL, 0, 5.4f}, mFloor);

    const vec3 posterColors[6][2] = {
        {{0.9f, 0.3f, 0.2f}, {0.95f, 0.85f, 0.6f}}, {{0.15f, 0.5f, 0.85f}, {0.9f, 0.9f, 0.95f}},
        {{0.2f, 0.7f, 0.4f}, {0.1f, 0.2f, 0.15f}},  {{0.95f, 0.75f, 0.2f}, {0.3f, 0.15f, 0.4f}},
        {{0.6f, 0.2f, 0.7f}, {0.95f, 0.6f, 0.8f}},  {{0.1f, 0.1f, 0.12f}, {0.9f, 0.5f, 0.1f}},
    };

    for (float s : {-1.0f, 1.0f}) {
        deco({-PL, -0.05f, s * 5.4f}, {PL, 0, s * PW}, mTactile);
        deco({-PL, -0.3f, s * PW}, {PL, -0.05f, s * (PW + 0.01f)}, mWhite);

        // Outer walls behind the tracks
        deco({-PL, BED, s * WALLZ}, {PL, 0.3f, s * (WALLZ + 0.5f)}, mConcrete);
        deco({-PL, 0.3f, s * WALLZ}, {PL, CEIL, s * (WALLZ + 0.5f)}, mWallTile);
        collider({-PL, BED, s * WALLZ}, {PL, CEIL, s * (WALLZ + 0.5f)});
        deco({-PL, 4.2f, s * (WALLZ - 0.03f)}, {PL, 4.7f, s * WALLZ}, mAccent);

        // Illuminated advertising posters
        for (int i = 0; i < 11; ++i) {
            float x = -50.0f + i * 10.0f;
            const vec3* pc = posterColors[(i + (s > 0 ? 0 : 3)) % 6];
            deco({x - 1.6f, 1.1f, s * (WALLZ - 0.05f)}, {x + 1.6f, 3.3f, s * WALLZ}, mDarkSteel);
            deco({x - 1.5f, 1.9f, s * (WALLZ - 0.07f)}, {x + 1.5f, 3.2f, s * (WALLZ - 0.05f)}, emissive(pc[0], 0.9f));
            deco({x - 1.5f, 1.2f, s * (WALLZ - 0.07f)}, {x + 1.5f, 1.9f, s * (WALLZ - 0.05f)}, emissive(pc[1], 0.9f));
        }
        // Station name boards on the accent band
        for (float x : {-45.0f, -15.0f, 15.0f, 45.0f})
            sign(staticOpaque, {x, 4.45f, s * (WALLZ - 0.08f)}, {-s, 0, 0}, {0, 0, -s}, 5.0f, 0.9f,
                 {0.05f, 0.18f, 0.5f}, 1234u, 9, false);
    }

    // End walls with the tunnel portals
    for (float e : {-1.0f, 1.0f}) {
        float x0 = e > 0 ? PL : -PL - 0.5f, x1 = x0 + 0.5f;
        float face = e * PL, in = -e; // inward-facing side
        solid({x0, BED, -TUN_IN}, {x1, CEIL, TUN_IN}, mWallTile);
        for (float s : {-1.0f, 1.0f}) {
            solid({x0, TUN_CEIL, s * TUN_IN}, {x1, CEIL, s * (WALLZ + 0.5f)}, mWallTile);
            deco({face, TUN_CEIL, s * TUN_IN}, {face + in * 0.02f, TUN_CEIL + 0.35f, s * WALLZ}, mStripes);
        }
        // Line map
        deco({face, 1.6f, -3.0f}, {face + in * 0.03f, 3.4f, 3.0f}, emissive({0.9f, 0.9f, 0.88f}, 0.6f));
        deco({face + in * 0.03f, 2.42f, -2.6f}, {face + in * 0.045f, 2.54f, 2.6f}, emissive(LINE_BLUE, 1.2f));
        for (int k = -4; k <= 4; ++k) {
            float z = k * 0.62f;
            bool here = k == 0;
            float r = here ? 0.12f : 0.07f;
            deco({face + in * 0.045f, 2.48f - r, z - r}, {face + in * 0.06f, 2.48f + r, z + r},
                 here ? emissive({0.95f, 0.1f, 0.1f}, 2.0f) : material({0.95f, 0.95f, 0.95f}));
        }
    }

    // Ceiling slab (with the escalator opening)
    const float ZO = WALLZ + 0.5f;
    auto slab = [&](float x0, float x1, float z0, float z1) { deco({x0, CEIL, z0}, {x1, CONC - 0.1f, z1}, mCeiling); };
    slab(-PL, HOLE_X0, -ZO, ZO);
    slab(HOLE_X1, PL, -ZO, ZO);
    slab(HOLE_X0, HOLE_X1, -ZO, -HOLE_Z);
    slab(HOLE_X0, HOLE_X1, HOLE_Z, ZO);

    for (int i = -5; i <= 5; ++i) {
        float x = i * 10.0f;
        if (x > HOLE_X0 - 0.3f && x < HOLE_X1 + 0.3f) {
            deco({x - 0.2f, 6.55f, -WALLZ}, {x + 0.2f, CEIL, -HOLE_Z}, mBeam);
            deco({x - 0.2f, 6.55f, HOLE_Z}, {x + 0.2f, CEIL, WALLZ}, mBeam);
        } else {
            deco({x - 0.2f, 6.55f, -WALLZ}, {x + 0.2f, CEIL, WALLZ}, mBeam);
        }
    }

    auto fixture = [&](float x, float z, float len) {
        deco({x - len * 0.5f - 0.1f, 6.82f, z - 0.22f}, {x + len * 0.5f + 0.1f, CEIL, z + 0.22f}, mDarkSteel);
        deco({x - len * 0.5f, 6.79f, z - 0.15f}, {x + len * 0.5f, 6.82f, z + 0.15f}, mFixture);
    };
    for (int i = 0; i < 12; ++i) {
        float x = -55.0f + i * 10.0f;
        for (float s : {-1.0f, 1.0f}) {
            fixture(x, s * 4.5f, 3.0f);
            addLight({x, 6.4f, s * 4.5f}, 14.0f, WARM, 17.0f, true);
        }
    }
    for (int i = 0; i < 6; ++i) {
        float x = -45.0f + i * 20.0f;
        for (float s : {-1.0f, 1.0f}) {
            if (x == 55.0f && s < 0) continue; // this one flickers (built every frame)
            fixture(x, s * 8.5f, 3.0f);
            addLight({x, 6.4f, s * 8.5f}, 12.0f, WARM, 12.0f, true);
        }
    }

    // Columns
    for (int i = -5; i <= 5; ++i) {
        float x = i * 10.0f;
        for (float s : {-1.0f, 1.0f}) {
            float z = s * 3.4f;
            staticOpaque.cylinder(boxXf({x, 3.5f, z}, {0.8f, 7.0f, 0.8f}), mColumn, 20);
            staticOpaque.cylinder(boxXf({x, 0.12f, z}, {0.96f, 0.24f, 0.96f}), mDarkSteel, 20);
            staticOpaque.cylinder(boxXf({x, 6.85f, z}, {1.0f, 0.3f, 1.0f}), mDarkSteel, 20);
            deco({x - 0.41f, 2.2f, z - 0.41f}, {x + 0.41f, 2.5f, z + 0.41f}, mAccent);
            collider({x - 0.4f, 0, z - 0.4f}, {x + 0.4f, CEIL, z + 0.4f});
        }
    }

    // Benches and bins
    for (float x : {-45.0f, -35.0f, -25.0f, -15.0f, 15.0f, 25.0f, 35.0f, 45.0f}) {
        for (float s : {-1.0f, 1.0f}) {
            float z = s * 3.4f;
            deco({x - 1.2f, 0.42f, z - 0.45f}, {x + 1.2f, 0.48f, z + 0.45f}, mWood);
            deco({x - 1.2f, 0.48f, z - 0.04f}, {x + 1.2f, 0.95f, z + 0.04f}, mWood);
            for (float lx : {-1.0f, 1.0f})
                deco({x + lx - 0.05f, 0, z - 0.4f}, {x + lx + 0.05f, 0.42f, z + 0.4f}, mDarkSteel);
            collider({x - 1.2f, 0, z - 0.45f}, {x + 1.2f, 0.5f, z + 0.45f});
        }
    }
    for (float x : {-39.0f, -19.0f, 21.0f, 41.0f}) {
        for (float s : {-1.0f, 1.0f}) {
            float z = s * 3.4f;
            staticOpaque.cylinder(boxXf({x, 0.45f, z}, {0.5f, 0.9f, 0.5f}), mSteel, 14);
            collider({x - 0.25f, 0, z - 0.25f}, {x + 0.25f, 0.9f, z + 0.25f});
        }
    }

    // Platform screen gates: fixed panels between the door openings
    for (float s : {-1.0f, 1.0f}) {
        float z0 = s * 5.65f, z1 = s * 5.85f, zm = s * 5.75f;
        auto panel = [&](float a, float b) {
            if (b - a < 0.05f) return;
            deco({a, 0, z0}, {b, 1.0f, z1}, mSteel);
            staticGlass.quadDoubleSided({a, 1.0f, zm}, {b, 1.0f, zm}, {b, 1.45f, zm}, {a, 1.45f, zm}, mGlass);
            deco({a, 1.45f, z0}, {b, 1.53f, z1}, mDarkSteel);
            deco({a, 0, z0}, {a + 0.06f, 1.53f, z1}, mDarkSteel);
            deco({b - 0.06f, 0, z0}, {b, 1.53f, z1}, mDarkSteel);
            collider({a, 0, z0}, {b, 3.0f, z1});
        };
        float start = -PL;
        for (float gx : doorGapX) { panel(start, gx - 0.9f); start = gx + 0.9f; }
        panel(start, PL);
    }

    // Next-train boards hanging over the platform
    for (float x : {-14.0f, 14.0f}) {
        deco({x - 0.12f, 5.0f, -1.6f}, {x + 0.12f, 5.8f, 1.6f}, mDarkSteel);
        for (float z : {-1.2f, 1.2f}) deco({x - 0.02f, 5.8f, z - 0.02f}, {x + 0.02f, CEIL, z + 0.02f}, mDarkSteel);
    }

    // Ceiling fan mounts
    for (float x : {-50.0f, -40.0f, -30.0f, -20.0f, 20.0f, 30.0f, 40.0f, 50.0f}) {
        staticOpaque.cylinder(boxXf({x, 6.1f, 0}, {0.06f, 0.9f, 0.06f}), mDarkSteel, 8);
        staticOpaque.cylinder(boxXf({x, 5.72f, 0}, {0.4f, 0.26f, 0.4f}), mDarkSteel, 16);
    }

    // Signs and extra light around the escalator foot
    sign(staticOpaque, {-10.8f, 5.45f, 0}, {0, 0, 1}, {-1, 0, 0}, 3.2f, 0.55f, {0.05f, 0.18f, 0.5f}, 99u, 8, false);
    for (float z : {-1.3f, 1.3f}) deco({-10.82f, 5.72f, z - 0.02f}, {-10.78f, CEIL, z + 0.02f}, mDarkSteel);
    addLight({-6.0f, 5.5f, 0}, 10.0f, WARM, 8.0f, true);
}

void World::buildTracksAndTunnels() {
    for (float s : {-1.0f, 1.0f}) {
        float tz = s * TZ;
        deco({-TUN_END, BED - 0.5f, s * TUN_IN}, {TUN_END, BED, s * WALLZ}, mBallast);
        collider({-TUN_END, BED - 0.5f, s * TUN_IN}, {TUN_END, BED, s * WALLZ});

        for (float r : {-0.72f, 0.72f}) {
            float zc = tz + r;
            deco({-TUN_END, BED + 0.15f, zc - 0.07f}, {TUN_END, BED + 0.19f, zc + 0.07f}, mRail);
            deco({-TUN_END, BED + 0.19f, zc - 0.035f}, {TUN_END, BED + 0.32f, zc + 0.035f}, mRail);
        }
        for (float x = -TUN_END + 0.35f; x < TUN_END; x += 0.7f)
            deco({x - 0.13f, BED, tz - 1.25f}, {x + 0.13f, BED + 0.15f, tz + 1.25f}, mSleeper);

        // Third rail with its cover board
        float zr = s * (TZ + 1.55f);
        deco({-TUN_END, BED + 0.2f, zr - 0.04f}, {TUN_END, BED + 0.36f, zr + 0.04f}, mRail);
        deco({-TUN_END, BED + 0.4f, zr - 0.12f}, {TUN_END, BED + 0.44f, zr + 0.12f}, material({0.75f, 0.65f, 0.25f}));
        for (float x = -TUN_END + 2.0f; x < TUN_END; x += 5.0f)
            deco({x - 0.05f, BED, zr - 0.05f}, {x + 0.05f, BED + 0.4f, zr + 0.05f}, material({0.9f, 0.9f, 0.85f}));

        for (float e : {-1.0f, 1.0f}) {
            float x0 = e > 0 ? PL + 0.5f : -TUN_END, x1 = e > 0 ? TUN_END : -PL - 0.5f;
            solid({x0, BED, s * 5.1f}, {x1, TUN_CEIL, s * TUN_IN}, mTunnel);
            solid({x0, BED, s * WALLZ}, {x1, TUN_CEIL, s * (WALLZ + 0.5f)}, mTunnel);
            deco({x0, TUN_CEIL, s * 5.1f}, {x1, TUN_CEIL + 0.5f, s * (WALLZ + 0.5f)}, mTunnel);
            // Emergency walkway and cable trays
            deco({x0, BED, s * 9.9f}, {x1, -0.2f, s * WALLZ}, mConcrete);
            deco({x0, -0.2f, s * 9.9f}, {x1, -0.19f, s * 10.0f}, mWhite);
            deco({x0, 2.4f, s * (WALLZ - 0.25f)}, {x1, 2.46f, s * WALLZ}, mDarkSteel);
            deco({x0, 2.8f, s * (WALLZ - 0.25f)}, {x1, 2.86f, s * WALLZ}, mDarkSteel);
            // End cap
            float xc = e * TUN_END;
            solid({xc, BED, s * 5.1f}, {xc + e * 0.5f, TUN_CEIL, s * (WALLZ + 0.5f)}, mTunnel);

            // Tunnel lights (warm sodium colour)
            for (int k = 0; k <= 10; ++k) {
                float x = e * (70.0f + 25.0f * k);
                deco({x - 0.4f, 3.4f, s * (WALLZ - 0.12f)}, {x + 0.4f, 3.6f, s * WALLZ}, emissive({1.0f, 0.72f, 0.42f}, 5.0f));
                addLight({x, 3.2f, s * (WALLZ - 0.6f)}, 11.0f, {1.0f, 0.7f, 0.42f}, 8.0f);
            }
            // Jet fan housings
            for (int k = 0; k < 4; ++k) {
                float x = e * (90.0f + 60.0f * k);
                mat4 xf = translate(mat4(1.0f), vec3(x, 3.95f, tz)) * rotate(mat4(1.0f), PI * 0.5f, vec3(0, 0, 1));
                staticOpaque.cylinder(scale(xf, vec3(1.1f, 3.5f, 1.1f)), mSteel, 20);
                for (float bx : {-1.0f, 1.0f})
                    deco({x + bx - 0.05f, 4.45f, tz - 0.08f}, {x + bx + 0.05f, TUN_CEIL, tz + 0.08f}, mDarkSteel);
            }
        }
    }

    // Signal heads at the tunnel mouths
    const float sig[4][2] = {{63.0f, 10.3f}, {-63.0f, 10.3f}, {-63.0f, -10.3f}, {63.0f, -10.3f}};
    for (auto& sg : sig) {
        float x = sg[0], z = sg[1], s = z > 0 ? 1.0f : -1.0f;
        deco({x - 0.15f, 1.3f, s * 10.05f}, {x + 0.15f, 2.5f, s * 10.45f}, mDarkSteel);
        deco({x - 0.04f, -0.2f, s * 10.2f}, {x + 0.04f, 1.3f, s * 10.3f}, mDarkSteel);
    }
}

void World::buildEscalatorBank() {
    const vec2 path[4] = {{EX0, 0}, {EI0, 0}, {EI1, CONC}, {EX1, CONC}};

    // Wedge-shaped enclosure under the escalators
    for (float s : {-1.0f, 1.0f}) {
        float z = s * HOLE_Z;
        staticOpaque.quadDoubleSided({EI0, 0, z}, {EX1, 0, z}, {EX1, CONC, z}, {EI1, CONC, z}, mCladding);
        slopedBar(staticOpaque, EI0 + 1.0f, 8.0f / 14.0f, EI1, CONC, -0.45f, -0.4f, z, z + s * 0.02f, mLed);
    }
    staticOpaque.quadDoubleSided({EX1, 0, -HOLE_Z}, {EX1, 0, HOLE_Z}, {EX1, CEIL, HOLE_Z}, {EX1, CEIL, -HOLE_Z}, mCladding);
    collider({EX1, -1.0f, -HOLE_Z}, {EX1 + 0.3f, CEIL, HOLE_Z});
    deco({EX1, 1.0f, -2.0f}, {EX1 + 0.04f, 3.0f, 2.0f}, emissive({0.2f, 0.55f, 0.9f}, 0.9f));
    deco({EX1 + 0.04f, 1.6f, -1.6f}, {EX1 + 0.05f, 2.6f, 0.2f}, emissive({0.95f, 0.95f, 0.9f}, 1.0f));

    struct Lane { float z0, z1, speed; bool stairs; };
    const Lane lanes[3] = {{-2.3f, -1.1f, ESC_SPEED, false}, {-0.6f, 0.6f, 0.0f, true}, {1.1f, 2.3f, -ESC_SPEED, false}};
    for (const Lane& l : lanes) {
        Ramp r{EX0, EX1, l.z0, l.z1, EI0, EI1, 0.0f, CONC, l.speed, l.stairs ? 0.5f : 0.0f};
        ramps.push_back(r);
        if (l.stairs) {
            const float rise = CONC / 28.0f;
            for (int i = 0; i < 28; ++i) {
                float xa = EI0 + i * 0.5f, top = (i + 1) * rise;
                deco({xa, 0, l.z0}, {xa + 0.5f, top, l.z1}, mStone);
                deco({xa, top - 0.01f, l.z0}, {xa + 0.05f, top + 0.004f, l.z1}, mYellow);
            }
        } else {
            deco({EX0 - 0.1f, 0, l.z0}, {EX0 + 0.35f, 0.04f, l.z1}, mYellow);
            deco({EX1 - 0.35f, CONC, l.z0}, {EX1 + 0.1f, CONC + 0.04f, l.z1}, mYellow);
        }
    }

    // Decks between the lanes and along the outside
    const float decks[4][2] = {{-2.6f, -2.3f}, {-1.1f, -0.6f}, {0.6f, 1.1f}, {2.3f, 2.6f}};
    for (auto& d : decks) {
        for (int sgm = 0; sgm < 3; ++sgm)
            slopedBar(staticOpaque, path[sgm].x, path[sgm].y, path[sgm + 1].x, path[sgm + 1].y, -0.5f, 0.12f, d[0], d[1], mCladding);
        collider({EX0, -1.0f, d[0]}, {EX1, 10.5f, d[1]});
    }

    // Glass balustrades with handrails and LED skirt lights
    for (float gz : {-2.35f, -1.05f, -0.65f, 0.65f, 1.05f, 2.35f}) {
        for (int sgm = 0; sgm < 3; ++sgm) {
            vec2 p0 = path[sgm], p1 = path[sgm + 1];
            staticGlass.quadDoubleSided({p0.x, p0.y + 0.12f, gz}, {p1.x, p1.y + 0.12f, gz},
                                        {p1.x, p1.y + 0.95f, gz}, {p0.x, p0.y + 0.95f, gz}, mGlass);
            slopedBar(staticOpaque, p0.x, p0.y, p1.x, p1.y, 0.93f, 1.02f, gz - 0.05f, gz + 0.05f, mRubber);
            slopedBar(staticOpaque, p0.x, p0.y, p1.x, p1.y, 0.12f, 0.15f, gz - 0.012f, gz + 0.012f, mLed);
        }
        deco({EX0 - 0.15f, 0.12f, gz - 0.05f}, {EX0, 1.02f, gz + 0.05f}, mRubber);
        deco({EX1, CONC + 0.12f, gz - 0.05f}, {EX1 + 0.15f, CONC + 1.02f, gz + 0.05f}, mRubber);
    }

    // Railing around the opening in the concourse floor
    const float rz = HOLE_Z + 0.15f, rx = HOLE_X0 - 0.2f;
    for (float s : {-1.0f, 1.0f}) {
        float z = s * rz;
        staticGlass.quadDoubleSided({rx, CONC + 0.1f, z}, {HOLE_X1, CONC + 0.1f, z}, {HOLE_X1, CONC + 1.05f, z}, {rx, CONC + 1.05f, z}, mGlass);
        deco({rx - 0.04f, CONC + 1.05f, z - 0.04f}, {HOLE_X1, CONC + 1.12f, z + 0.04f}, mSteel);
        deco({rx, CONC, z - 0.03f}, {HOLE_X1, CONC + 0.1f, z + 0.03f}, mSteel);
        for (float x = rx; x <= HOLE_X1 + 0.01f; x += 1.5f)
            deco({x - 0.03f, CONC, z - 0.03f}, {x + 0.03f, CONC + 1.05f, z + 0.03f}, mSteel);
        collider({rx - 0.15f, CEIL + 0.5f, s * HOLE_Z}, {HOLE_X1, CONC + 2.5f, s * (rz + 0.1f)});
    }
    staticGlass.quadDoubleSided({rx, CONC + 0.1f, -rz}, {rx, CONC + 0.1f, rz}, {rx, CONC + 1.05f, rz}, {rx, CONC + 1.05f, -rz}, mGlass);
    deco({rx - 0.04f, CONC + 1.05f, -rz}, {rx + 0.04f, CONC + 1.12f, rz}, mSteel);
    deco({rx - 0.03f, CONC, -rz}, {rx + 0.03f, CONC + 0.1f, rz}, mSteel);
    collider({rx - 0.15f, CEIL + 0.5f, -rz - 0.1f}, {rx + 0.1f, CONC + 2.5f, rz + 0.1f});
}

void World::buildConcourse() {
    auto floorPiece = [&](float x0, float x1, float z0, float z1) {
        deco({x0, CONC - 0.1f, z0}, {x1, CONC, z1}, mConcFloor);
        collider({x0, CEIL, z0}, {x1, CONC, z1});
    };
    floorPiece(-CONC_X, HOLE_X0, -WALLZ, WALLZ);
    floorPiece(HOLE_X1, CONC_X, -WALLZ, WALLZ);
    floorPiece(HOLE_X0, HOLE_X1, -WALLZ, -HOLE_Z);
    floorPiece(HOLE_X0, HOLE_X1, HOLE_Z, WALLZ);

    // Long walls
    for (float s : {-1.0f, 1.0f}) {
        float z0 = s * WALLZ, z1 = s * (WALLZ + 0.5f);
        deco({-CONC_X - 0.5f, CONC, z0}, {CONC_X + 0.5f, 10.8f, z1}, mWallTile);
        deco({-CONC_X - 0.5f, 10.8f, z0}, {CONC_X + 0.5f, 11.2f, z1}, mAccent);
        deco({-CONC_X - 0.5f, 11.2f, z0}, {CONC_X + 0.5f, CONC_CEIL, z1}, mCeilingLight);
        collider({-CONC_X - 0.5f, CONC, z0}, {CONC_X + 0.5f, CONC_CEIL + 2.0f, z1});
    }
    // West end wall with a large line map
    solid({-CONC_X - 0.5f, CONC, -WALLZ}, {-CONC_X, CONC_CEIL, WALLZ}, mWallTile);
    deco({-CONC_X, 9.0f, -5.0f}, {-CONC_X + 0.03f, 11.6f, 5.0f}, emissive({0.9f, 0.9f, 0.88f}, 0.6f));
    deco({-CONC_X + 0.03f, 10.2f, -4.4f}, {-CONC_X + 0.045f, 10.4f, 4.4f}, emissive(LINE_BLUE, 1.2f));
    deco({-CONC_X + 0.03f, 10.9f, -4.4f}, {-CONC_X + 0.045f, 11.0f, 0.5f}, emissive({0.9f, 0.45f, 0.05f}, 1.2f));
    for (int k = -6; k <= 6; ++k) {
        float z = k * 0.7f, r = k == 1 ? 0.16f : 0.09f;
        deco({-CONC_X + 0.045f, 10.3f - r, z - r}, {-CONC_X + 0.06f, 10.3f + r, z + r},
             k == 1 ? emissive({0.95f, 0.1f, 0.1f}, 2.0f) : material({0.95f, 0.95f, 0.95f}));
    }
    // East end wall with the exit opening
    solid({CONC_X, CONC, -WALLZ}, {CONC_X + 0.5f, CONC_CEIL, -3.0f}, mWallTile);
    solid({CONC_X, CONC, 3.0f}, {CONC_X + 0.5f, CONC_CEIL, WALLZ}, mWallTile);
    solid({CONC_X, 12.0f, -3.0f}, {CONC_X + 0.5f, CONC_CEIL, 3.0f}, mWallTile);
    sign(staticOpaque, {CONC_X - 0.06f, 12.5f, 0}, {0, 0, 1}, {-1, 0, 0}, 2.4f, 0.6f, {0.05f, 0.5f, 0.2f}, 7u, 4, false);

    // Ceiling grid with light panels
    deco({-CONC_X - 0.5f, CONC_CEIL, -WALLZ - 0.5f}, {CONC_X + 0.5f, CONC_CEIL + 0.5f, WALLZ + 0.5f}, mCeilingLight);
    for (int i = 0; i <= 6; ++i) {
        float x = -30.0f + i * 10.0f;
        deco({x - 0.15f, 12.75f, -WALLZ}, {x + 0.15f, CONC_CEIL, WALLZ}, mBeam);
    }
    for (float z : {-9.0f, -3.0f, 3.0f, 9.0f})
        deco({-CONC_X, 12.75f, z - 0.15f}, {CONC_X, CONC_CEIL, z + 0.15f}, mBeam);
    for (int i = 0; i < 6; ++i) {
        float x = -25.0f + i * 10.0f;
        for (float z : {-6.0f, 0.0f, 6.0f}) {
            deco({x - 0.8f, 12.95f, z - 0.8f}, {x + 0.8f, 12.99f, z + 0.8f}, emissive(COOL, 4.5f));
            addLight({x, 12.6f, z}, 15.0f, COOL, 11.0f, true);
        }
    }

    // Columns
    for (float x : {-18.0f, -6.0f, 6.0f, 20.0f}) {
        for (float s : {-1.0f, 1.0f}) {
            float z = s * 5.5f;
            staticOpaque.cylinder(boxXf({x, 10.5f, z}, {0.9f, 5.0f, 0.9f}), mColumn, 20);
            staticOpaque.cylinder(boxXf({x, CONC + 0.12f, z}, {1.05f, 0.24f, 1.05f}), mDarkSteel, 20);
            collider({x - 0.45f, CONC, z - 0.45f}, {x + 0.45f, CONC_CEIL, z + 0.45f});
        }
    }

    // Wall exhaust fan housings
    for (float x : {-22.0f, -8.0f}) {
        for (float s : {-1.0f, 1.0f}) {
            float zw = s * WALLZ, zf = s * (WALLZ - 0.35f);
            vec3 c(x, 11.6f, 0);
            deco({x - 1.0f, 10.6f, zw}, {x + 1.0f, 10.75f, zf}, mDarkSteel);
            deco({x - 1.0f, 12.45f, zw}, {x + 1.0f, 12.6f, zf}, mDarkSteel);
            deco({x - 1.0f, 10.6f, zw}, {x - 0.85f, 12.6f, zf}, mDarkSteel);
            deco({x + 0.85f, 10.6f, zw}, {x + 1.0f, 12.6f, zf}, mDarkSteel);
            mat4 back = translate(mat4(1.0f), vec3(x, 11.6f, s * (WALLZ - 0.03f))) * rotate(mat4(1.0f), PI * 0.5f, vec3(1, 0, 0));
            staticOpaque.cylinder(scale(back, vec3(1.7f, 0.04f, 1.7f)), mRubber, 24);
            deco({x - 0.85f, 11.58f, s * (WALLZ - 0.34f)}, {x + 0.85f, 11.62f, s * (WALLZ - 0.32f)}, mDarkSteel);
            deco({x - 0.02f, 10.75f, s * (WALLZ - 0.34f)}, {x + 0.02f, 12.45f, s * (WALLZ - 0.32f)}, mDarkSteel);
        }
    }

    // Fare gate line
    for (int j = 0; j < 10; ++j) {
        float c = -4.5f + j;
        deco({13.3f, CONC, c - 0.15f}, {14.7f, CONC + 1.0f, c + 0.15f}, mSteel);
        deco({13.3f, CONC + 1.0f, c - 0.16f}, {14.7f, CONC + 1.06f, c + 0.16f}, mDarkSteel);
        deco({14.45f, CONC + 1.06f, c - 0.1f}, {14.65f, CONC + 1.08f, c + 0.1f}, emissive({0.2f, 0.5f, 1.0f}, 1.5f));
        collider({13.3f, CONC, c - 0.15f}, {14.7f, CONC + 2.5f, c + 0.15f});
    }
    for (float s : {-1.0f, 1.0f}) {
        float za = s * 4.65f, zb = s * WALLZ;
        staticGlass.quadDoubleSided({FARE_X, CONC + 0.05f, za}, {FARE_X, CONC + 0.05f, zb}, {FARE_X, CONC + 1.2f, zb}, {FARE_X, CONC + 1.2f, za}, mGlass);
        deco({FARE_X - 0.05f, CONC + 1.2f, za}, {FARE_X + 0.05f, CONC + 1.27f, zb}, mSteel);
        for (float z = 4.65f; z <= WALLZ; z += 1.2f)
            deco({FARE_X - 0.04f, CONC, s * z - 0.04f}, {FARE_X + 0.04f, CONC + 1.2f, s * z + 0.04f}, mSteel);
        collider({FARE_X - 0.1f, CONC, za}, {FARE_X + 0.1f, CONC + 2.5f, zb});
    }

    // Clock above the gates
    deco({FARE_X - 0.02f, 11.9f, -0.02f}, {FARE_X + 0.02f, CONC_CEIL, 0.02f}, mDarkSteel);
    mat4 clockXf = translate(mat4(1.0f), vec3(FARE_X, 11.3f, 0)) * rotate(mat4(1.0f), PI * 0.5f, vec3(0, 0, 1));
    staticOpaque.cylinder(scale(clockXf, vec3(1.3f, 0.14f, 1.3f)), mDarkSteel, 32);
    staticOpaque.cylinder(scale(clockXf, vec3(1.18f, 0.16f, 1.18f)), emissive({1.0f, 1.0f, 0.97f}, 1.3f), 32);
    for (int k = 0; k < 12; ++k) {
        float a = k * TAU / 12.0f;
        float y = 11.3f + std::cos(a) * 0.5f, z = std::sin(a) * 0.5f;
        float sz = (k % 3 == 0) ? 0.05f : 0.025f;
        deco({FARE_X - 0.085f, y - sz, z - sz}, {FARE_X + 0.085f, y + sz, z + sz}, mRubber);
    }
    // Departure boards flanking the clock
    for (float s : {-1.0f, 1.0f}) {
        float zc = s * 3.5f;
        deco({FARE_X - 0.1f, 10.9f, zc - 1.4f}, {FARE_X + 0.1f, 11.7f, zc + 1.4f}, mDarkSteel);
        for (float dz : {-1.0f, 1.0f})
            deco({FARE_X - 0.02f, 11.7f, zc + dz - 0.02f}, {FARE_X + 0.02f, CONC_CEIL, zc + dz + 0.02f}, mDarkSteel);
    }

    // "Platforms" sign over the top of the escalators
    sign(staticOpaque, {9.8f, 11.25f, 0}, {0, 0, -1}, {1, 0, 0}, 3.2f, 0.55f, {0.05f, 0.18f, 0.5f}, 55u, 9, true);
    for (float z : {-1.3f, 1.3f}) deco({9.78f, 11.52f, z - 0.02f}, {9.82f, CONC_CEIL, z + 0.02f}, mDarkSteel);

    // Ticket machines
    for (float x : {18.0f, 20.5f, 23.0f, 25.5f}) {
        float zf = WALLZ - 0.6f;
        deco({x - 0.5f, CONC, zf}, {x + 0.5f, CONC + 1.9f, WALLZ}, material({0.15f, 0.3f, 0.55f}, PAT_METAL, 48, 0.5f));
        deco({x - 0.32f, CONC + 1.1f, zf - 0.02f}, {x + 0.32f, CONC + 1.6f, zf}, emissive({0.35f, 0.65f, 1.0f}, 2.2f));
        deco({x - 0.2f, CONC + 0.8f, zf - 0.03f}, {x + 0.2f, CONC + 0.9f, zf}, mRubber);
        deco({x - 0.5f, CONC + 1.9f, zf}, {x + 0.5f, CONC + 2.1f, WALLZ}, emissive({1.0f, 0.55f, 0.1f}, 2.0f));
        collider({x - 0.5f, CONC, zf}, {x + 0.5f, CONC + 2.5f, WALLZ});
    }

    // Information booth
    {
        float x0 = 20.0f, x1 = 26.0f, z0 = -WALLZ, z1 = -7.6f;
        deco({x0, CONC, z1 - 0.3f}, {x1, CONC + 1.1f, z1}, mWood);
        deco({x0, CONC, z0}, {x0 + 0.3f, CONC + 1.1f, z1}, mWood);
        deco({x1 - 0.3f, CONC, z0}, {x1, CONC + 1.1f, z1}, mWood);
        deco({x0, CONC + 2.8f, z0}, {x1, CONC + 3.0f, z1}, mSteel);
        deco({x0, CONC + 3.0f, z1 - 0.05f}, {x1, CONC + 3.4f, z1}, emissive({1.0f, 0.6f, 0.15f}, 1.5f));
        staticGlass.quadDoubleSided({x0, CONC + 1.1f, z1 - 0.15f}, {x1, CONC + 1.1f, z1 - 0.15f}, {x1, CONC + 2.8f, z1 - 0.15f}, {x0, CONC + 2.8f, z1 - 0.15f}, mGlass);
        staticGlass.quadDoubleSided({x0 + 0.15f, CONC + 1.1f, z0}, {x0 + 0.15f, CONC + 1.1f, z1}, {x0 + 0.15f, CONC + 2.8f, z1}, {x0 + 0.15f, CONC + 2.8f, z0}, mGlass);
        staticGlass.quadDoubleSided({x1 - 0.15f, CONC + 1.1f, z0}, {x1 - 0.15f, CONC + 1.1f, z1}, {x1 - 0.15f, CONC + 2.8f, z1}, {x1 - 0.15f, CONC + 2.8f, z0}, mGlass);
        for (float x : {x0, x1 - 0.08f})
            deco({x, CONC, z1 - 0.08f}, {x + 0.08f, CONC + 2.8f, z1}, mDarkSteel);
        deco({x0 + 1.0f, CONC, z0 + 0.3f}, {x1 - 1.0f, CONC + 0.8f, z0 + 1.0f}, mDarkSteel);
        deco({x0 + 1.5f, CONC + 0.8f, z0 + 0.5f}, {x0 + 2.3f, CONC + 1.3f, z0 + 0.55f}, emissive({0.4f, 0.7f, 1.0f}, 1.5f));
        deco({x0 + 1.0f, CONC + 2.75f, -9.3f}, {x1 - 1.0f, CONC + 2.8f, -9.0f}, mFixture);
        addLight({23.0f, CONC + 2.4f, -9.1f}, 7.0f, WARM, 5.0f, true);
        collider({x0, CONC, z0}, {x1, CONC + 3.0f, z1});
    }
}

void World::buildExit() {
    const float X0 = CONC_X, XS = 32.0f, XE = 46.0f, XL = 49.0f, Z = 3.0f, TOP = CONC + 8.0f;
    deco({X0, CONC - 0.1f, -Z}, {XS, CONC, Z}, mConcFloor);
    const float rise = 8.0f / 28.0f;
    for (int i = 0; i < 28; ++i) {
        float xa = XS + i * 0.5f, top = CONC + (i + 1) * rise;
        deco({xa, CONC, -Z}, {xa + 0.5f, top, Z}, mStone);
        deco({xa, top - 0.01f, -Z}, {xa + 0.05f, top + 0.004f, Z}, mYellow);
    }
    deco({XE, CONC, -Z}, {XL, TOP - 0.1f, Z}, mConcrete);
    deco({XE, TOP - 0.1f, -Z}, {XL, TOP, Z}, mConcFloor);
    ramps.push_back(Ramp{X0, XL, -Z, Z, XS, XE, CONC, TOP, 0.0f, 0.5f});

    for (float s : {-1.0f, 1.0f}) {
        solid({X0, CONC, s * Z}, {XL + 0.5f, TOP + 4.5f, s * (Z + 0.5f)}, mWallTile);
        slopedBar(staticOpaque, XS, CONC, XE, TOP, 0.85f, 0.92f, s * (Z - 0.08f), s * (Z - 0.02f), mSteel);
        for (float x : {34.0f, 40.0f}) {
            float y = CONC + ((x - XS) / 0.5f + 1.0f) * rise + 3.4f;
            deco({x - 0.5f, y, s * Z}, {x + 0.5f, y + 0.12f, s * (Z - 0.05f)}, mFixture);
        }
    }
    solid({X0, CONC_CEIL, -Z}, {X0 + 0.5f, TOP + 4.5f, Z}, mWallTile);
    solid({XL, CONC, -Z - 0.5f}, {XL + 0.5f, TOP + 4.5f, Z + 0.5f}, mWallTile);
    deco({X0, TOP + 4.5f, -Z - 0.5f}, {XL + 0.5f, TOP + 5.0f, Z + 0.5f}, mCeilingLight);

    // "Daylight" at the top of the stairs
    deco({XL - 0.05f, TOP, -Z + 0.3f}, {XL, TOP + 3.8f, Z - 0.3f}, emissive({0.8f, 0.9f, 1.0f}, 5.0f));
    deco({XL - 0.1f, TOP, -Z}, {XL, TOP + 4.0f, -Z + 0.3f}, mDarkSteel);
    deco({XL - 0.1f, TOP, Z - 0.3f}, {XL, TOP + 4.0f, Z}, mDarkSteel);
    deco({XL - 0.1f, TOP + 3.8f, -Z}, {XL, TOP + 4.5f, Z}, mDarkSteel);
    addLight({XL - 1.0f, TOP + 2.0f, 0}, 22.0f, {0.85f, 0.92f, 1.0f}, 30.0f);
    addLight({34.0f, 13.2f, 0}, 10.0f, WARM, 5.0f);
    addLight({40.0f, 16.8f, 0}, 10.0f, WARM, 5.0f);
}

// ---------------------------------------------------------------------------------------------
// Simulation
// ---------------------------------------------------------------------------------------------

void World::updateTrain(Train& t, float dt) {
    float prev = t.x;
    switch (t.state) {
    case Train::LAYOVER:
        t.v = 0.0f;
        t.timer -= dt;
        if (t.timer <= 0.0f) t.state = Train::APPROACH;
        break;
    case Train::APPROACH: {
        float d = -t.x * t.dir; // distance to the stopping point
        float target = std::min(T_VMAX, std::sqrt(2.0f * T_DEC * std::max(d, 0.0f)));
        t.v = t.v < target ? std::min(target, t.v + T_ACC * dt) : target;
        float step = t.v * dt;
        if (step >= d || d < 0.01f) {
            t.x = 0.0f; t.v = 0.0f; t.state = Train::DWELL; t.timer = 0.0f;
        } else {
            t.x += t.dir * step;
        }
        break;
    }
    case Train::DWELL: {
        t.timer += dt;
        float tm = t.timer;
        if (tm < 1.5f) t.doors = 0.0f;
        else if (tm < 3.5f) t.doors = (tm - 1.5f) / 2.0f;
        else if (tm < T_DWELL - 6.0f) t.doors = 1.0f;
        else if (tm < T_DWELL - 4.0f) t.doors = 1.0f - (tm - (T_DWELL - 6.0f)) / 2.0f;
        else t.doors = 0.0f;
        if (tm >= T_DWELL) t.state = Train::DEPART;
        break;
    }
    case Train::DEPART:
        t.v = std::min(T_VMAX, t.v + T_ACC * dt);
        t.x += t.dir * t.v * dt;
        if (t.x * t.dir > T_WRAP) {
            t.x = -t.dir * T_WRAP;
            t.v = 0.0f;
            t.state = Train::LAYOVER;
            t.timer = T_LAYOVER;
        }
        break;
    }
    t.delta = t.x - prev;
}

void World::update(float dt, vec3 pf) {
    time += dt;
    for (Train& t : trains) updateTrain(t, dt);

    for (int k = 0; k < 9; ++k) {
        float p = -4.0f + k;
        bool near = std::fabs(pf.x - FARE_X) < 2.0f && std::fabs(pf.z - p) < 0.45f && std::fabs(pf.y - CONC) < 1.0f;
        fareGateOpen[k] = clamp(fareGateOpen[k] + (near ? 3.0f : -2.0f) * dt, 0.0f, 1.0f);
    }
}

// ---------------------------------------------------------------------------------------------
// Moving geometry
// ---------------------------------------------------------------------------------------------

void World::addFan(MeshBuilder& o, vec3 c, vec3 axis, float radius, float angle, int blades, const Material& m) {
    vec3 A = normalize(axis);
    vec3 ref = std::fabs(A.y) < 0.9f ? vec3(0, 1, 0) : vec3(1, 0, 0);
    vec3 U = normalize(cross(A, ref));
    vec3 V = cross(A, U);
    mat4 hub(1.0f);
    hub[0] = vec4(V * radius * 0.36f, 0);
    hub[1] = vec4(A * radius * 0.14f, 0);
    hub[2] = vec4(U * radius * 0.36f, 0);
    hub[3] = vec4(c, 1);
    o.cylinder(hub, mDarkSteel, 12);
    const float pitch = 0.35f;
    for (int k = 0; k < blades; ++k) {
        float a = angle + TAU * k / blades;
        vec3 D = U * std::cos(a) + V * std::sin(a);
        vec3 W = cross(A, D);
        mat4 xf(1.0f);
        xf[0] = vec4(D * radius * 0.85f, 0);
        xf[1] = vec4((W * std::cos(pitch) + A * std::sin(pitch)) * radius * 0.22f, 0);
        xf[2] = vec4((A * std::cos(pitch) - W * std::sin(pitch)) * 0.02f, 0);
        xf[3] = vec4(c + D * radius * 0.55f, 1);
        o.box(xf, m);
    }
}

void World::addEscalatorSteps(MeshBuilder& o, float z0, float z1, float dir) {
    const float L1 = EI0 - EX0, L3 = EX1 - EI1;
    const float L2 = std::sqrt((EI1 - EI0) * (EI1 - EI0) + CONC * CONC);
    const float LT = L1 + L2 + L3;
    const int n = int(LT / 0.4f);
    const float sp = LT / n;
    const float off = std::fmod(time * ESC_SPEED, sp);
    for (int i = 0; i < n; ++i) {
        float s = i * sp + off;
        if (dir < 0) s = LT - s;
        if (s < 0.35f || s > LT - 0.35f) continue;
        vec2 p;
        if (s < L1) p = {EX0 + s, 0.0f};
        else if (s < L1 + L2) { float f = (s - L1) / L2; p = {EI0 + f * (EI1 - EI0), f * CONC}; }
        else p = {EI1 + (s - L1 - L2), CONC};
        o.aabb({p.x - 0.2f, p.y - 0.39f, z0 + 0.03f}, {p.x + 0.2f, p.y + 0.03f, z1 - 0.03f}, mStep);
        o.aabb({p.x - 0.2f, p.y + 0.02f, z0 + 0.03f}, {p.x - 0.16f, p.y + 0.035f, z1 - 0.03f}, mYellow);
    }
}

void World::addTrain(const Train& t, int ti, MeshBuilder& o, MeshBuilder& g) {
    const float rot = t.dir > 0 ? 0.0f : PI;
    const float doorOff = 0.68f * smooth01(t.doors);
    const Material body = material({0.82f, 0.84f, 0.87f}, PAT_METAL, 64, 0.6f);
    const Material stripe = material(t.lineColor, PAT_PLAIN, 48, 0.5f);
    const Material floorM = material({0.30f, 0.32f, 0.36f}, PAT_CONCRETE, 16, 0.1f);
    const Material wallIn = material({0.88f, 0.88f, 0.85f}, PAT_PLAIN, 24, 0.2f);
    const Material seat = material(t.lineColor * 0.6f + vec3(0.1f), PAT_PLAIN, 16, 0.1f);
    const Material pole = material({0.9f, 0.75f, 0.15f}, PAT_METAL, 64, 0.7f);
    const Material roofBox = material({0.45f, 0.46f, 0.48f}, PAT_METAL, 32, 0.3f);
    const Material strip = emissive({1.0f, 0.97f, 0.9f}, 5.0f);
    const Material windshield = material({0.05f, 0.07f, 0.09f}, PAT_GLASS, 128, 1.0f);
    const bool doorsMoving = t.doors > 0.01f;

    for (int i = 0; i < CARS; ++i) {
        float cx = t.x + t.dir * (2 - i) * CAR_PITCH;
        mat4 T = translate(mat4(1.0f), vec3(cx, 0, t.z)) * rotate(mat4(1.0f), rot, vec3(0, 1, 0));
        auto P = [&](vec3 v) { return vec3(T * vec4(v, 1.0f)); };
        auto B = [&](vec3 a, vec3 b, const Material& m) {
            vec3 mn = min(a, b), mx = max(a, b);
            o.box(T * boxXf((mn + mx) * 0.5f, mx - mn), m);
        };
        auto C = [&](vec3 a, vec3 b) {
            vec3 wa = P(a), wb = P(b);
            dynBoxes.push_back({min(wa, wb), max(wa, wb), ti});
        };
        auto Gq = [&](vec3 a, vec3 b, vec3 c, vec3 d) { g.quadDoubleSided(P(a), P(b), P(c), P(d), mGlass); };
        const bool frontCab = i == 0, rearCab = i == CARS - 1;

        // Running gear
        B({-9.3f, -0.7f, -1.35f}, {9.3f, -0.25f, 1.35f}, mDarkSteel);
        for (float bx : {-6.5f, 6.5f}) {
            B({bx - 1.3f, -0.62f, -1.2f}, {bx + 1.3f, -0.35f, 1.2f}, mDarkSteel);
            for (float wx : {-0.9f, 0.9f})
                for (float wz : {-0.72f, 0.72f}) {
                    mat4 w = T * translate(mat4(1.0f), vec3(bx + wx, -0.36f, wz)) * rotate(mat4(1.0f), PI * 0.5f, vec3(1, 0, 0));
                    o.cylinder(scale(w, vec3(0.84f, 0.12f, 0.84f)), mRail, 14);
                }
        }

        // Floor, roof, lighting
        B({-9.5f, -0.25f, -1.5f}, {9.5f, 0.0f, 1.5f}, body);
        B({-9.42f, 0.0f, -1.42f}, {9.42f, 0.05f, 1.42f}, floorM);
        C({-9.5f, -0.8f, -1.5f}, {9.5f, 0.05f, 1.5f});
        B({-9.5f, 2.55f, -1.5f}, {9.5f, 2.85f, 1.5f}, body);
        B({-9.42f, 2.5f, -1.42f}, {9.42f, 2.55f, 1.42f}, wallIn);
        for (float ax : {-4.5f, 4.5f}) B({ax - 1.5f, 2.85f, -0.9f}, {ax + 1.5f, 3.2f, 0.9f}, roofBox);
        for (float lz : {-0.55f, 0.55f}) B({-9.0f, 2.46f, lz - 0.08f}, {9.0f, 2.5f, lz + 0.08f}, strip);

        // Side walls (side -1 faces the platform)
        const float segs[4][2] = {{-9.5f, -6.9f}, {-5.5f, -0.7f}, {0.7f, 5.5f}, {6.9f, 9.5f}};
        for (float side : {-1.0f, 1.0f}) {
            float zi = side * 1.42f, zo = side * 1.5f;
            for (auto& sg : segs) {
                float a = sg[0], b = sg[1];
                B({a, 0.05f, zi}, {b, 0.95f, zo}, body);
                B({a, 1.95f, zi}, {b, 2.55f, zo}, body);
                B({a, 0.72f, zo}, {b, 0.88f, side * 1.51f}, stripe);
                float pillars[3] = {a, b - 0.2f, (a + b) * 0.5f - 0.1f};
                int np = (b - a > 4.0f) ? 3 : 2;
                std::sort(pillars, pillars + np);
                for (int k = 0; k < np; ++k) B({pillars[k], 0.95f, zi}, {pillars[k] + 0.2f, 1.95f, zo}, body);
                for (int k = 0; k + 1 < np; ++k) {
                    float wa = pillars[k] + 0.2f, wb = pillars[k + 1];
                    float zg = side * 1.46f;
                    Gq({wa, 0.95f, zg}, {wb, 0.95f, zg}, {wb, 1.95f, zg}, {wa, 1.95f, zg});
                }
                C({a, 0.05f, side * 1.35f}, {b, 3.0f, side * 1.56f});
                // Longitudinal seats
                float s0 = a + 0.2f, s1 = b - 0.2f;
                B({s0, 0.05f, side * 1.0f}, {s1, 0.45f, zi}, seat);
                B({s0, 0.45f, side * 1.3f}, {s1, 0.95f, zi}, seat);
                C({s0, 0.05f, side * 1.0f}, {s1, 0.5f, zi});
            }
            for (float dc : DOOR_X) {
                B({dc - 0.7f, 2.0f, zi}, {dc + 0.7f, 2.55f, zo}, body);
                float off = side < 0 ? doorOff : 0.0f;
                for (float leaf : {-1.0f, 1.0f}) {
                    float la = leaf < 0 ? dc - 0.7f - off : dc + off, lb = la + 0.7f;
                    float z0 = side * 1.5f, z1 = side * 1.56f;
                    B({la, 0.05f, z0}, {lb, 1.05f, z1}, body);
                    B({la, 1.85f, z0}, {lb, 2.0f, z1}, body);
                    B({la, 1.05f, z0}, {la + 0.08f, 1.85f, z1}, body);
                    B({lb - 0.08f, 1.05f, z0}, {lb, 1.85f, z1}, body);
                    B({la, 0.72f, z1}, {lb, 0.88f, side * 1.565f}, stripe);
                    float zg = side * 1.53f;
                    Gq({la + 0.08f, 1.05f, zg}, {lb - 0.08f, 1.05f, zg}, {lb - 0.08f, 1.85f, zg}, {la + 0.08f, 1.85f, zg});
                }
                if (off < 0.6f) C({dc - 0.7f, 0.05f, side * 1.35f}, {dc + 0.7f, 3.0f, side * 1.56f});
                if (side < 0)
                    B({dc - 0.1f, 2.28f, zo}, {dc + 0.1f, 2.4f, side * 1.54f},
                      doorsMoving ? emissive({1.0f, 0.55f, 0.1f}, 5.0f) : mDarkSteel);
                // Grab poles by the doors
                for (float px : {dc - 0.85f, dc + 0.85f}) {
                    o.cylinder(T * boxXf({px, 1.275f, side * 0.95f}, {0.07f, 2.45f, 0.07f}), pole, 8);
                    C({px - 0.05f, 0.05f, side * 0.9f}, {px + 0.05f, 2.5f, side * 1.0f});
                }
            }
            mat4 rail = T * translate(mat4(1.0f), vec3(0, 2.05f, side * 0.95f)) * rotate(mat4(1.0f), PI * 0.5f, vec3(0, 0, 1));
            o.cylinder(scale(rail, vec3(0.05f, 18.4f, 0.05f)), pole, 8);
        }

        // Ends: driving cabs at the ends of the train, gangways in between
        for (float end : {-1.0f, 1.0f}) {
            float xe = end * 9.5f, xi = end * 9.42f;
            bool cab = (end > 0 && frontCab) || (end < 0 && rearCab);
            if (cab) {
                B({xi, 0.05f, -1.5f}, {xe, 2.55f, 1.5f}, wallIn);
                C({xi, 0.05f, -1.5f}, {xe, 3.0f, 1.5f});
                float xn = xe + end * 0.35f;
                B({xe, -0.25f, -1.45f}, {xn, 2.6f, 1.45f}, body);
                B({xn, 1.3f, -1.2f}, {xn + end * 0.01f, 2.3f, 1.2f}, windshield);
                B({xn, 0.72f, -1.45f}, {xn + end * 0.015f, 0.88f, 1.45f}, stripe);
                B({xn, 2.35f, -0.8f}, {xn + end * 0.015f, 2.5f, 0.8f}, emissive({1.0f, 0.6f, 0.1f}, 3.0f));
                Material lamp = end > 0 ? emissive({1.0f, 0.98f, 0.92f}, 12.0f) : emissive({1.0f, 0.05f, 0.03f}, 6.0f);
                for (float hz : {-1.0f, 1.0f}) B({xn, 0.45f, hz - 0.18f}, {xn + end * 0.02f, 0.65f, hz + 0.18f}, lamp);
            } else {
                B({xi, 0.05f, -1.5f}, {xe, 2.55f, -0.55f}, wallIn);
                B({xi, 0.05f, 0.55f}, {xe, 2.55f, 1.5f}, wallIn);
                B({xi, 2.05f, -0.55f}, {xe, 2.55f, 0.55f}, wallIn);
                C({xi, 0.05f, -1.5f}, {xe, 3.0f, -0.55f});
                C({xi, 0.05f, 0.55f}, {xe, 3.0f, 1.5f});
                float xg = end * 9.8f;
                B({xe, 0.05f, -0.65f}, {xg, 2.15f, -0.55f}, mRubber);
                B({xe, 0.05f, 0.55f}, {xg, 2.15f, 0.65f}, mRubber);
                B({xe, 2.05f, -0.65f}, {xg, 2.15f, 0.65f}, mRubber);
                B({xe, -0.05f, -0.6f}, {xg, 0.05f, 0.6f}, floorM);
                C({xe, -0.8f, -0.6f}, {xg, 0.05f, 0.6f});
                C({xe, 0.05f, -0.7f}, {xg, 3.0f, -0.55f});
                C({xe, 0.05f, 0.55f}, {xg, 3.0f, 0.7f});
            }
        }

        // Lights
        for (float lx : {-6.0f, 0.0f, 6.0f}) {
            Light l;
            l.pos = P({lx, 1.7f, 0});
            l.radius = 7.0f;
            l.color = vec3(1.0f, 0.96f, 0.88f) * 3.5f;
            dynLights.push_back(l);
        }
        if (frontCab) {
            Light l;
            l.pos = P({10.0f, 0.8f, 0});
            l.radius = 80.0f;
            l.color = vec3(1.0f, 0.97f, 0.9f) * 120.0f;
            l.dir = normalize(vec3(t.dir, -0.06f, 0));
            l.cosOuter = std::cos(radians(22.0f));
            dynLights.push_back(l);
        }
        if (rearCab) {
            Light l;
            l.pos = P({-10.1f, 0.6f, 0});
            l.radius = 7.0f;
            l.color = vec3(1.0f, 0.05f, 0.02f) * 4.0f;
            dynLights.push_back(l);
        }
    }
}

void World::buildDynamic(MeshBuilder& o, MeshBuilder& g) {
    o.clear();
    g.clear();
    dynBoxes.clear();
    dynLights.clear();

    addEscalatorSteps(o, -2.3f, -1.1f, 1.0f);
    addEscalatorSteps(o, 1.1f, 2.3f, -1.0f);

    for (int i = 0; i < 2; ++i) addTrain(trains[i], i, o, g);

    // Platform screen gate leaves follow the train doors
    for (float s : {-1.0f, 1.0f}) {
        const Train& t = trains[s > 0 ? 0 : 1];
        float open = smooth01(t.doors);
        float off = 0.85f * open;
        float z0 = s * 5.55f, z1 = s * 5.63f, zm = s * 5.59f;
        for (float gx : doorGapX) {
            for (float side : {-1.0f, 1.0f}) {
                float a = side < 0 ? gx - 0.9f - off : gx + off, b = a + 0.9f;
                o.aabb({a, 0, z0}, {b, 1.0f, z1}, mSteel);
                o.aabb({a, 1.45f, z0}, {b, 1.5f, z1}, mDarkSteel);
                float edge = side < 0 ? b - 0.05f : a;
                o.aabb({edge, 0, z0}, {edge + 0.05f, 1.5f, z1}, mRubber);
                g.quadDoubleSided({a, 1.0f, zm}, {b, 1.0f, zm}, {b, 1.45f, zm}, {a, 1.45f, zm}, mGlass);
                dynBoxes.push_back({min(vec3(a, 0, z0), vec3(b, 3.0f, s * 5.85f)), max(vec3(a, 0, z0), vec3(b, 3.0f, s * 5.85f)), -1});
            }
            Material lamp = open > 0.5f ? emissive({0.1f, 1.0f, 0.3f}, 4.0f) : emissive({1.0f, 0.1f, 0.05f}, 1.2f);
            o.aabb({gx - 0.15f, 1.53f, s * 5.7f}, {gx + 0.15f, 1.6f, s * 5.8f}, lamp);
        }
    }

    // Fare gate flaps
    for (int k = 0; k < 9; ++k) {
        float p = -4.0f + k, op = smooth01(fareGateOpen[k]);
        float ext = 0.33f * (1.0f - op);
        if (ext > 0.01f) {
            g.aabb({13.97f, CONC + 0.45f, p - 0.35f}, {14.03f, CONC + 1.25f, p - 0.35f + ext}, mRedGlass);
            g.aabb({13.97f, CONC + 0.45f, p + 0.35f - ext}, {14.03f, CONC + 1.25f, p + 0.35f}, mRedGlass);
            o.aabb({13.96f, CONC + 0.45f, p - 0.37f + ext}, {14.04f, CONC + 1.25f, p - 0.35f + ext}, emissive({1.0f, 0.15f, 0.1f}, 2.0f));
            o.aabb({13.96f, CONC + 0.45f, p + 0.35f - ext}, {14.04f, CONC + 1.25f, p + 0.37f - ext}, emissive({1.0f, 0.15f, 0.1f}, 2.0f));
        }
        if (op < 0.7f) dynBoxes.push_back({{13.9f, CONC, p - 0.35f}, {14.1f, CONC + 2.5f, p + 0.35f}, -1});
        Material lamp = op > 0.5f ? emissive({0.1f, 1.0f, 0.3f}, 4.0f) : emissive({1.0f, 0.1f, 0.05f}, 3.0f);
        for (float x : {13.35f, 14.55f})
            o.aabb({x, CONC + 1.06f, p - 0.56f}, {x + 0.1f, CONC + 1.1f, p - 0.44f}, lamp);
    }

    // Fans
    const Material blade = material({0.7f, 0.7f, 0.68f}, PAT_METAL, 32, 0.4f);
    for (float x : {-50.0f, -40.0f, -30.0f, -20.0f, 20.0f, 30.0f, 40.0f, 50.0f})
        addFan(o, {x, 5.62f, 0}, {0, 1, 0}, 1.1f, time * TAU * 1.1f + x, 4, blade);
    for (float x : {-22.0f, -8.0f})
        for (float s : {-1.0f, 1.0f})
            addFan(o, {x, 11.6f, s * (WALLZ - 0.2f)}, {0, 0, s}, 0.78f, time * TAU * 1.7f + x, 6, blade);
    for (float s : {-1.0f, 1.0f})
        for (float e : {-1.0f, 1.0f})
            for (int k = 0; k < 4; ++k) {
                float x = e * (90.0f + 60.0f * k);
                for (float end : {-1.0f, 1.0f})
                    addFan(o, {x + end * 1.8f, 3.95f, s * TZ}, {1, 0, 0}, 0.48f, time * TAU * 2.3f * s + k, 8, mSteel);
            }

    // Clock hands (real local time)
    {
        std::time_t now = std::time(nullptr);
        std::tm lt = *std::localtime(&now);
        float hA = ((lt.tm_hour % 12) + lt.tm_min / 60.0f) / 12.0f * TAU;
        float mA = (lt.tm_min + lt.tm_sec / 60.0f) / 60.0f * TAU;
        float sA = lt.tm_sec / 60.0f * TAU;
        for (float face : {-1.0f, 1.0f}) {
            auto hand = [&](float ang, float len, float w, const Material& m, float dx) {
                float phi = face < 0 ? ang : -ang;
                vec3 dir(0, std::cos(phi), std::sin(phi));
                vec3 c = vec3(FARE_X + face * (0.085f + dx), 11.3f, 0) + dir * (len * 0.5f - 0.06f);
                mat4 xf = translate(mat4(1.0f), c) * rotate(mat4(1.0f), phi, vec3(1, 0, 0));
                o.box(scale(xf, vec3(0.01f, len, w)), m);
            };
            hand(hA, 0.34f, 0.045f, mRubber, 0.005f);
            hand(mA, 0.5f, 0.03f, mRubber, 0.015f);
            hand(sA, 0.52f, 0.012f, material({0.9f, 0.1f, 0.05f}), 0.025f);
        }
    }

    // Next-train displays: fill bar grows as the train approaches, blinks while it is in
    auto row = [&](float xf0, float xf1, float y0, float y1, float zs, float len, const Train& t) {
        float frac = 1.0f - clamp(t.eta() / 90.0f, 0.0f, 1.0f);
        bool here = t.state == Train::DWELL;
        o.aabb({xf0, y0, zs}, {xf1, y1, zs + 0.25f}, emissive(t.lineColor, 3.0f));
        float barLen = (len - 0.4f) * (here ? 1.0f : frac);
        bool on = !here || std::fmod(time, 1.0f) < 0.6f;
        if (barLen > 0.01f && on)
            o.aabb({xf0, y0 + 0.02f, zs + 0.4f}, {xf1, y1 - 0.02f, zs + 0.4f + barLen}, emissive({1.0f, 0.6f, 0.1f}, 3.0f));
    };
    for (float s : {-1.0f, 1.0f}) {
        const Train& t = trains[s > 0 ? 0 : 1];
        float zc = s * 3.5f;
        row(FARE_X - 0.12f, FARE_X - 0.1f, 11.2f, 11.4f, zc - 1.3f, 2.6f, t);
        row(FARE_X + 0.1f, FARE_X + 0.12f, 11.2f, 11.4f, zc - 1.3f, 2.6f, t);
    }
    for (float x : {-14.0f, 14.0f}) {
        for (float face : {-1.0f, 1.0f}) {
            float a = x + face * 0.12f, b = a + face * 0.02f;
            row(std::min(a, b), std::max(a, b), 5.45f, 5.65f, -1.5f, 3.0f, trains[0]);
            row(std::min(a, b), std::max(a, b), 5.15f, 5.35f, -1.5f, 3.0f, trains[1]);
        }
    }

    // Signals: exit signals clear as a train departs, entry signals stop trains while the platform is occupied
    struct Sig { float x, z; int train; bool exit; };
    const Sig sigs[4] = {{63.0f, 10.3f, 0, true}, {-63.0f, 10.3f, 0, false}, {-63.0f, -10.3f, 1, true}, {63.0f, -10.3f, 1, false}};
    for (const Sig& sg : sigs) {
        const Train& t = trains[sg.train];
        bool green;
        if (sg.exit) green = t.state == Train::DEPART || (t.state == Train::DWELL && t.timer > T_DWELL - 3.0f);
        else green = !(t.state == Train::DWELL || (t.state == Train::DEPART && t.x * t.dir < PL + 40.0f));
        float fx = sg.x - t.dir * 0.16f;
        o.aabb({fx - 0.02f, 2.0f, sg.z - 0.1f}, {fx + 0.02f, 2.2f, sg.z + 0.1f}, green ? mRubber : emissive({1.0f, 0.08f, 0.04f}, 8.0f));
        o.aabb({fx - 0.02f, 1.6f, sg.z - 0.1f}, {fx + 0.02f, 1.8f, sg.z + 0.1f}, green ? emissive({0.1f, 1.0f, 0.35f}, 8.0f) : mRubber);
        Light l;
        l.pos = vec3(fx - t.dir * 0.4f, 1.9f, sg.z * 0.97f);
        l.radius = 6.0f;
        l.color = (green ? vec3(0.1f, 1.0f, 0.3f) : vec3(1.0f, 0.08f, 0.04f)) * 3.0f;
        dynLights.push_back(l);
    }

    // A failing fluorescent tube over track B
    {
        float ph = std::fmod(time, 7.0f), fl = 1.0f;
        if (ph > 5.3f) fl = (std::sin(time * 61.0f) * std::sin(time * 23.0f) > 0.05f) ? 1.0f : 0.06f;
        if (ph > 6.5f) fl = 0.03f;
        float x = 55.0f, z = -8.5f;
        o.aabb({x - 1.6f, 6.82f, z - 0.22f}, {x + 1.6f, CEIL, z + 0.22f}, mDarkSteel);
        o.aabb({x - 1.5f, 6.79f, z - 0.15f}, {x + 1.5f, 6.82f, z + 0.15f}, emissive({1.0f, 0.95f, 0.88f}, 6.0f * fl));
        Light l;
        l.pos = {x, 6.4f, z};
        l.radius = 12.0f;
        l.color = WARM * 12.0f * fl;
        l.dir = vec3(0, -1, 0);
        l.cosOuter = -0.05f;
        dynLights.push_back(l);
    }
}

void World::gatherLights(vec3 cam, std::vector<Light>& out, size_t maxLights) const {
    std::vector<std::pair<float, const Light*>> cand;
    cand.reserve(staticLights.size() + dynLights.size());
    for (const Light& l : staticLights) cand.push_back({length(l.pos - cam) - l.radius, &l});
    for (const Light& l : dynLights) cand.push_back({length(l.pos - cam) - l.radius, &l});
    size_t n = std::min(maxLights, cand.size());
    std::partial_sort(cand.begin(), cand.begin() + n, cand.end(),
                      [](const auto& a, const auto& b) { return a.first < b.first; });
    for (size_t i = 0; i < n; ++i) out.push_back(*cand[i].second);
}

// ---------------------------------------------------------------------------------------------
// Collision queries
// ---------------------------------------------------------------------------------------------

GroundInfo World::ground(vec3 f, float r, float step) const {
    GroundInfo g;
    float limit = f.y + step;
    auto consider = [&](const Box& b) {
        if (!overlapXZ(b, f, r)) return;
        if (b.mx.y <= limit && b.mx.y > g.height) {
            g.found = true;
            g.height = b.mx.y;
            g.carrier = b.carrier;
            g.conveyor = 0.0f;
        }
    };
    for (const Box& b : staticBoxes) consider(b);
    for (const Box& b : dynBoxes) consider(b);
    for (const Ramp& rp : ramps) {
        if (f.x < rp.xa || f.x > rp.xb || f.z + r <= rp.za || f.z - r >= rp.zb) continue;
        float h = rp.heightAt(f.x);
        if (h > limit) continue;
        if (h >= g.height) {
            g.found = true;
            g.height = h;
            g.carrier = -1;
            g.conveyor = rp.conveyorAt(f.x);
        } else if (g.height - h < 0.3f) {
            // Footprint just touching a landing edge: the escalator still carries us over the lip
            g.conveyor = rp.conveyorAt(f.x);
        }
    }
    return g;
}

bool World::blocked(vec3 f, float r, float h, float step) const {
    auto hit = [&](const Box& b) {
        return overlapXZ(b, f, r) && b.mx.y > f.y + step && b.mn.y < f.y + h;
    };
    for (const Box& b : staticBoxes) if (hit(b)) return true;
    for (const Box& b : dynBoxes) if (hit(b)) return true;
    return false;
}

void World::pushOut(vec3& f, float r, float h, float step) const {
    // Moving obstacles (closing doors, gates) shove the player out along the shallowest axis.
    for (int iter = 0; iter < 4; ++iter) {
        bool moved = false;
        for (const Box& b : dynBoxes) {
            if (!(overlapXZ(b, f, r) && b.mx.y > f.y + step && b.mn.y < f.y + h)) continue;
            float pxn = (f.x + r) - b.mn.x, pxp = b.mx.x - (f.x - r);
            float pzn = (f.z + r) - b.mn.z, pzp = b.mx.z - (f.z - r);
            float m = std::min(std::min(pxn, pxp), std::min(pzn, pzp));
            if (m == pxn) f.x -= pxn + 0.001f;
            else if (m == pxp) f.x += pxp + 0.001f;
            else if (m == pzn) f.z -= pzn + 0.001f;
            else f.z += pzp + 0.001f;
            moved = true;
        }
        if (!moved) break;
    }
}
