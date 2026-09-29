// The metro station: static layout, animated objects, lights and collision.
#pragma once
#include "mesh.h"
#include <glm/glm.hpp>
#include <vector>

// ---- Station dimensions (metres). X runs along the tracks, Y is up. ----
namespace dim {
constexpr float PL = 60.0f;          // platform half length
constexpr float PW = 6.0f;           // platform half width (edge at |z| = PW)
constexpr float TZ = 7.6f;           // track centre line |z|
constexpr float WALLZ = 10.6f;       // inner face of the outer walls |z|
constexpr float CEIL = 7.0f;         // platform hall ceiling
constexpr float CONC = 8.0f;         // concourse floor level
constexpr float CONC_CEIL = 13.0f;   // concourse ceiling
constexpr float CONC_X = 30.0f;      // concourse half length
constexpr float BED = -1.1f;         // track bed level
constexpr float TUN_END = 330.0f;    // tunnel ends
constexpr float TUN_CEIL = 4.6f;     // tunnel ceiling
constexpr float TUN_IN = 5.6f;       // tunnel inner wall |z|
constexpr float GATE_Z = 5.75f;      // platform screen gate line |z|
constexpr float FARE_X = 14.0f;      // fare gate line on the concourse
}

struct Light {
    glm::vec3 pos;
    float radius;
    glm::vec3 color;     // already multiplied by intensity
    glm::vec3 dir{0, -1, 0};
    float cosOuter = -3.0f; // <= -2 means omnidirectional
};

// Axis-aligned collision box. `carrier` >= 0 marks a box that belongs to a moving train.
struct Box {
    glm::vec3 mn, mx;
    int carrier = -1;
};

// Walkable slope along X (escalators, stairs). Height rises from y0 to y1 between ix0 and ix1.
struct Ramp {
    float xa, xb, za, zb;   // footprint
    float ix0, ix1, y0, y1; // incline
    float speed = 0.0f;     // conveyor speed along the path (+ = towards +x)
    float stepRun = 0.0f;   // > 0 quantises height into stair steps
    float heightAt(float x) const;
    float conveyorAt(float x) const;
};

struct GroundInfo {
    bool found = false;
    float height = -1e9f;
    int carrier = -1;
    float conveyor = 0.0f;
};

struct Train {
    enum State { APPROACH, DWELL, DEPART, LAYOVER };
    float dir;           // +1 travels towards +x, -1 towards -x
    float z;             // track centre
    float x = 0.0f;      // centre of the train
    float v = 0.0f;
    float delta = 0.0f;  // x displacement this frame (includes wrap-around jumps)
    float doors = 0.0f;  // 0 closed .. 1 fully open
    float timer = 0.0f;
    State state = APPROACH;
    glm::vec3 lineColor;
    float eta() const;   // rough seconds until it reaches the platform
};

class World {
public:
    void build();
    void update(float dt, glm::vec3 playerFeet);
    // Rebuilds all moving geometry for this frame.
    void buildDynamic(MeshBuilder& opaque, MeshBuilder& glass);
    // Picks the lights most relevant to the camera.
    void gatherLights(glm::vec3 cam, std::vector<Light>& out, size_t maxLights) const;

    GroundInfo ground(glm::vec3 feet, float radius, float step) const;
    bool blocked(glm::vec3 feet, float radius, float height, float step) const;
    void pushOut(glm::vec3& feet, float radius, float height, float step) const;

    MeshBuilder staticOpaque, staticGlass;
    Train trains[2];
    float time = 0.0f;

private:
    std::vector<Box> staticBoxes, dynBoxes;
    std::vector<Ramp> ramps;
    std::vector<Light> staticLights, dynLights;
    std::vector<float> fareGateOpen;   // per passage, 0..1
    std::vector<float> doorGapX;       // platform screen gate opening centres

    // static building helpers
    void solid(glm::vec3 mn, glm::vec3 mx, const Material& m);
    void deco(glm::vec3 mn, glm::vec3 mx, const Material& m);
    void collider(glm::vec3 mn, glm::vec3 mx);
    void addLight(glm::vec3 p, float radius, glm::vec3 color, float intensity, bool down = false);

    void buildPlatformLevel();
    void buildTracksAndTunnels();
    void buildEscalatorBank();
    void buildConcourse();
    void buildExit();

    void updateTrain(Train& t, float dt);
    void addTrain(const Train& t, int index, MeshBuilder& o, MeshBuilder& g);
    void addEscalatorSteps(MeshBuilder& o, float z0, float z1, float dir);
    void addFan(MeshBuilder& o, glm::vec3 c, glm::vec3 axis, float radius, float angle, int blades, const Material& m);
};
