// Geometry building (CPU side) and GPU mesh upload.
#pragma once
#include "gl_loader.h"
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

// Procedural surface patterns, evaluated in the fragment shader.
enum Pattern {
    PAT_PLAIN = 0,
    PAT_FLOOR_TILE = 1,   // large polished floor tiles
    PAT_WALL_TILE = 2,    // small metro-style wall tiles
    PAT_TACTILE = 3,      // tactile dots on the platform edge
    PAT_BALLAST = 4,      // gravel in the track bed
    PAT_CONCRETE = 5,     // noisy concrete
    PAT_GROOVES = 6,      // escalator step grooves
    PAT_METAL = 7,        // brushed metal
    PAT_GLASS = 8,        // dark reflective glass
    PAT_STRIPES = 9       // warning stripes (yellow/black)
};

struct Material {
    glm::vec3 color{0.8f};
    float pattern = PAT_PLAIN;
    float shininess = 16.0f;
    float specular = 0.1f;
    float emissive = 0.0f;
};

inline Material material(glm::vec3 c, Pattern p = PAT_PLAIN, float shin = 16.0f, float spec = 0.1f, float emis = 0.0f) {
    Material m;
    m.color = c; m.pattern = (float)p; m.shininess = shin; m.specular = spec; m.emissive = emis;
    return m;
}
inline Material emissive(glm::vec3 c, float strength) { return material(c, PAT_PLAIN, 1.0f, 0.0f, strength); }

struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec3 color;
    glm::vec4 params; // pattern, shininess, specular, emissive
};

class MeshBuilder {
public:
    std::vector<Vertex> verts;
    std::vector<uint32_t> indices;

    void clear() { verts.clear(); indices.clear(); }

    // Unit cube centred at origin, transformed by xf.
    void box(const glm::mat4& xf, const Material& m);
    // Axis-aligned box between two opposite corners (any order).
    void aabb(glm::vec3 a, glm::vec3 b, const Material& m);
    // Arbitrary hexahedron; corner i uses bit0 = +x side, bit1 = +y side, bit2 = +z side.
    void hexa(const glm::vec3 c[8], const Material& m);
    // Unit cylinder (radius 0.5, height 1, along Y, centred), transformed by xf.
    void cylinder(const glm::mat4& xf, const Material& m, int segments = 16);
    // Planar quad, counter-clockwise when viewed from the front.
    void quad(glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, const Material& m);
    void quadDoubleSided(glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, const Material& m);

private:
    void push(glm::vec3 p, glm::vec3 n, const Material& m);
};

class GpuMesh {
public:
    void upload(const MeshBuilder& b, bool dynamic);
    void draw() const;
private:
    GLuint vao = 0, vbo = 0, ebo = 0;
    GLsizei count = 0;
    size_t vboCap = 0, eboCap = 0;
};
