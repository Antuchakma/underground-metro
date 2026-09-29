#include "mesh.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

void MeshBuilder::push(glm::vec3 p, glm::vec3 n, const Material& m) {
    verts.push_back({p, n, m.color, glm::vec4(m.pattern, m.shininess, m.specular, m.emissive)});
}

void MeshBuilder::quad(glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, const Material& m) {
    glm::vec3 n = glm::cross(b - a, c - a);
    float len = glm::length(n);
    if (len < 1e-8f) n = glm::cross(c - a, d - a), len = glm::length(n);
    if (len < 1e-8f) return;
    n /= len;
    uint32_t base = (uint32_t)verts.size();
    push(a, n, m); push(b, n, m); push(c, n, m); push(d, n, m);
    indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
}

void MeshBuilder::quadDoubleSided(glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, const Material& m) {
    quad(a, b, c, d, m);
    quad(d, c, b, a, m);
}

void MeshBuilder::box(const glm::mat4& xf, const Material& m) {
    // 8 corners of the unit cube
    glm::vec3 c[8];
    for (int i = 0; i < 8; ++i) {
        glm::vec4 p((i & 1) ? 0.5f : -0.5f, (i & 2) ? 0.5f : -0.5f, (i & 4) ? 0.5f : -0.5f, 1.0f);
        c[i] = glm::vec3(xf * p);
    }
    hexa(c, m);
}

void MeshBuilder::hexa(const glm::vec3 c[8], const Material& m) {
    // Faces as CCW corner indices seen from outside.
    static const int f[6][4] = {
        {1, 3, 7, 5}, // +X
        {0, 4, 6, 2}, // -X
        {2, 6, 7, 3}, // +Y
        {0, 1, 5, 4}, // -Y
        {4, 5, 7, 6}, // +Z
        {0, 2, 3, 1}, // -Z
    };
    for (auto& q : f) quad(c[q[0]], c[q[1]], c[q[2]], c[q[3]], m);
}

void MeshBuilder::aabb(glm::vec3 a, glm::vec3 b, const Material& m) {
    glm::vec3 mn = glm::min(a, b), mx = glm::max(a, b);
    glm::mat4 xf = glm::translate(glm::mat4(1.0f), (mn + mx) * 0.5f);
    xf = glm::scale(xf, mx - mn);
    box(xf, m);
}

void MeshBuilder::cylinder(const glm::mat4& xf, const Material& m, int seg) {
    glm::mat3 nm = glm::transpose(glm::inverse(glm::mat3(xf)));
    auto P = [&](float x, float y, float z) { return glm::vec3(xf * glm::vec4(x, y, z, 1.0f)); };
    auto N = [&](float x, float y, float z) { return glm::normalize(nm * glm::vec3(x, y, z)); };
    const float TAU = 6.28318530718f;
    // Side
    for (int i = 0; i < seg; ++i) {
        float a0 = TAU * i / seg, a1 = TAU * (i + 1) / seg;
        float x0 = std::cos(a0) * 0.5f, z0 = std::sin(a0) * 0.5f;
        float x1 = std::cos(a1) * 0.5f, z1 = std::sin(a1) * 0.5f;
        uint32_t base = (uint32_t)verts.size();
        push(P(x0, -0.5f, z0), N(x0, 0, z0), m);
        push(P(x0, 0.5f, z0), N(x0, 0, z0), m);
        push(P(x1, 0.5f, z1), N(x1, 0, z1), m);
        push(P(x1, -0.5f, z1), N(x1, 0, z1), m);
        indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    // Caps (triangle fans)
    for (int cap = 0; cap < 2; ++cap) {
        float y = cap ? 0.5f : -0.5f;
        glm::vec3 n = N(0, cap ? 1.0f : -1.0f, 0);
        uint32_t center = (uint32_t)verts.size();
        push(P(0, y, 0), n, m);
        for (int i = 0; i <= seg; ++i) {
            float a = TAU * i / seg;
            push(P(std::cos(a) * 0.5f, y, std::sin(a) * 0.5f), n, m);
        }
        for (int i = 0; i < seg; ++i) {
            if (cap) indices.insert(indices.end(), {center, center + 2 + i, center + 1 + i});
            else     indices.insert(indices.end(), {center, center + 1 + i, center + 2 + i});
        }
    }
}

void GpuMesh::upload(const MeshBuilder& b, bool dynamic) {
    if (!vao) {
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glGenBuffers(1, &ebo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        GLsizei stride = sizeof(Vertex);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, pos));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, normal));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, color));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, params));
    }
    glBindVertexArray(vao);
    GLenum usage = dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW;
    size_t vbytes = b.verts.size() * sizeof(Vertex);
    size_t ibytes = b.indices.size() * sizeof(uint32_t);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    if (vbytes > vboCap || !dynamic) { vboCap = vbytes * (dynamic ? 2 : 1); glBufferData(GL_ARRAY_BUFFER, vboCap, nullptr, usage); }
    glBufferSubData(GL_ARRAY_BUFFER, 0, vbytes, b.verts.data());
    if (ibytes > eboCap || !dynamic) { eboCap = ibytes * (dynamic ? 2 : 1); glBufferData(GL_ELEMENT_ARRAY_BUFFER, eboCap, nullptr, usage); }
    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, ibytes, b.indices.data());
    count = (GLsizei)b.indices.size();
}

void GpuMesh::draw() const {
    if (!count) return;
    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, nullptr);
}
