// Underground metro station walkthrough.
//   WASD move, mouse look, Shift run, Space jump, F flashlight, N noclip (Space/Ctrl up/down),
//   B toggle bloom, 1 platform, 2 concourse, 3 exit stairs, Esc quit.
#include "gl_loader.h"
#include "shaders.h"
#include "world.h"
#include "player.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <string>
#include <vector>

using namespace glm;

namespace {

GLuint compileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::fprintf(stderr, "Shader compile error:\n%s\n", log);
    }
    return s;
}

GLuint makeProgram(const char* vs, const char* fs) {
    GLuint p = glCreateProgram();
    GLuint v = compileShader(GL_VERTEX_SHADER, vs), f = compileShader(GL_FRAGMENT_SHADER, fs);
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        std::fprintf(stderr, "Program link error:\n%s\n", log);
    }
    glDeleteShader(v);
    glDeleteShader(f);
    return p;
}

GLuint makeTexture(int w, int h) {
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

// Off-screen buffers: multisampled HDR scene, its resolve, and two half-size bloom buffers.
struct RenderTargets {
    int w = 0, h = 0, bw = 0, bh = 0;
    GLuint msFbo = 0, msColor = 0, msDepth = 0;
    GLuint resolveFbo = 0, resolveTex = 0;
    GLuint bloomFbo[2] = {0, 0}, bloomTex[2] = {0, 0};

    void destroy() {
        if (!msFbo) return;
        glDeleteFramebuffers(1, &msFbo);
        glDeleteRenderbuffers(1, &msColor);
        glDeleteRenderbuffers(1, &msDepth);
        glDeleteFramebuffers(1, &resolveFbo);
        glDeleteTextures(1, &resolveTex);
        glDeleteFramebuffers(2, bloomFbo);
        glDeleteTextures(2, bloomTex);
        msFbo = 0;
    }

    void create(int width, int height) {
        destroy();
        w = width; h = height;
        bw = std::max(1, w / 2); bh = std::max(1, h / 2);

        glGenFramebuffers(1, &msFbo);
        glBindFramebuffer(GL_FRAMEBUFFER, msFbo);
        glGenRenderbuffers(1, &msColor);
        glBindRenderbuffer(GL_RENDERBUFFER, msColor);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_RGBA16F, w, h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msColor);
        glGenRenderbuffers(1, &msDepth);
        glBindRenderbuffer(GL_RENDERBUFFER, msDepth);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_DEPTH_COMPONENT24, w, h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, msDepth);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            std::fprintf(stderr, "MSAA framebuffer incomplete\n");

        glGenFramebuffers(1, &resolveFbo);
        glBindFramebuffer(GL_FRAMEBUFFER, resolveFbo);
        resolveTex = makeTexture(w, h);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, resolveTex, 0);

        glGenFramebuffers(2, bloomFbo);
        for (int i = 0; i < 2; ++i) {
            glBindFramebuffer(GL_FRAMEBUFFER, bloomFbo[i]);
            bloomTex[i] = makeTexture(bw, bh);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, bloomTex[i], 0);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
};

struct LightBlock {
    vec4 posR[MAX_LIGHTS];
    vec4 colI[MAX_LIGHTS];
    vec4 dirCos[MAX_LIGHTS];
    int count[4];
};

double lastMouseX = 0, lastMouseY = 0;
bool firstMouse = true;

// Scripted camera positions for `metro.exe --shots` (renders them to shots/*.ppm and exits).
struct Shot { float time; vec3 feet; float yaw, pitch; const char* name; };
const Shot SHOTS[] = {
    {2.0f, {-22.0f, 0.0f, 4.6f}, 3.1416f, 0.0f, "01_platform_start"},
    {12.0f, {-30.0f, 0.0f, 4.6f}, 3.1416f, -0.05f, "02_train_arriving"},
    {22.0f, {-21.0f, 0.0f, 1.5f}, 1.1f, 0.0f, "03_doors_open"},
    {22.5f, {-12.0f, 0.05f, 7.6f}, 0.0f, 0.0f, "04_inside_train"},
    {25.0f, {-14.0f, 0.0f, -0.3f}, 0.0f, 0.2f, "05_escalators"},
    {25.5f, {25.0f, 8.0f, 2.0f}, 3.1416f, 0.05f, "06_concourse"},
    {26.0f, {-45.0f, 4.0f, -8.5f}, 0.3f, -0.2f, "07_overview"},
    {26.5f, {100.0f, 0.0f, 9.6f}, 3.1416f, 0.05f, "08_tunnel"},
    {27.0f, {31.0f, 8.0f, 0.0f}, 0.0f, 0.2f, "09_exit"},
    {27.5f, {3.0f, 8.0f, 6.0f}, -2.3f, -0.35f, "10_escalator_top"},
};

void savePPM(const char* path, int w, int h) {
    std::vector<unsigned char> px(size_t(w) * h * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px.data());
    FILE* f = std::fopen(path, "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = h - 1; y >= 0; --y) std::fwrite(&px[size_t(y) * w * 3], 1, size_t(w) * 3, f);
    std::fclose(f);
}

// Headless gameplay checks for `metro.exe --selftest`: scripted walks through every mechanic.
int selfTest() {
    World w;
    w.build();
    MeshBuilder o, g;
    Player p;
    const float dt = 1.0f / 60.0f;
    int fails = 0;
    auto run = [&](PlayerInput in, float secs, bool (*until)(const World&, const Player&) = nullptr) {
        for (float t = 0; t < secs; t += dt) {
            w.update(dt, p.feet);
            w.buildDynamic(o, g);
            p.update(dt, in, w);
            if (until && until(w, p)) return;
        }
    };
    auto check = [&](bool ok, const char* what) {
        std::printf("%s  %-48s pos (%.2f, %.2f, %.2f)%s\n", ok ? "PASS" : "FAIL", what,
                    p.feet.x, p.feet.y, p.feet.z, p.ridingTrain() ? " [on train]" : "");
        if (!ok) ++fails;
    };
    PlayerInput idle, walk;
    walk.forward = 1.0f;

    p.respawn({-12.0f, 0.0f, -1.7f}, 0.0f);
    run(walk, 1.5f);
    run(idle, 45.0f);
    check(std::fabs(p.feet.y - dim::CONC) < 0.05f && p.feet.x > 8.0f, "up escalator carries player to concourse");

    p.respawn({10.0f, dim::CONC, 1.7f}, 3.1416f);
    run(walk, 1.0f);
    run(idle, 45.0f);
    check(std::fabs(p.feet.y) < 0.05f && p.feet.x < -8.5f, "down escalator carries player to platform");

    p.respawn({-12.0f, 0.0f, 0.0f}, 0.0f);
    run(walk, 7.0f);
    check(std::fabs(p.feet.y - dim::CONC) < 0.05f, "walk up the fixed stairs");

    p.respawn({17.0f, dim::CONC, 0.0f}, 3.1416f);
    run(walk, 3.0f);
    check(p.feet.x < 13.0f, "fare gate opens and lets player through");

    p.respawn({-12.0f, 0.0f, 4.0f}, 1.5708f);
    run(walk, 2.0f);
    check(p.feet.z < 5.6f, "closed platform gate blocks the track edge");

    p.respawn({0.0f, dim::CONC, 5.0f}, 1.5708f);
    run(walk, 5.0f);
    check(p.feet.z < dim::WALLZ && std::fabs(p.feet.y - dim::CONC) < 0.05f, "concourse wall blocks player");

    // Board train A at the middle door (x = 0) once its doors are open
    p.respawn({0.0f, 0.0f, 4.6f}, 1.5708f);
    run(idle, 120.0f, [](const World& w, const Player&) { return w.trains[0].state == Train::DWELL && w.trains[0].doors >= 1.0f; });
    check(w.trains[0].doors >= 1.0f, "train A arrives and opens doors");
    run(walk, 1.2f);
    run(idle, 0.3f);
    check(p.ridingTrain() && p.feet.z > 6.2f, "player boards through open gates and doors");
    run(idle, 60.0f, [](const World& w, const Player&) { return w.trains[0].state == Train::DEPART && w.trains[0].x > 150.0f; });
    check(p.ridingTrain() && p.feet.x > 140.0f && std::fabs(p.feet.y - 0.05f) < 0.01f, "player rides train into the tunnel");
    run(idle, 120.0f, [](const World& w, const Player&) { return w.trains[0].state == Train::DWELL; });
    check(p.ridingTrain() && std::fabs(p.feet.x) < 12.0f, "train loops round and brings player back");

    std::printf("%s (%d failure%s)\n", fails ? "SELFTEST FAILED" : "SELFTEST OK", fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--selftest") return selfTest();
    const bool shotMode = argc > 1 && std::string(argv[1]) == "--shots";
    size_t shotIndex = 0;
    if (!glfwInit()) {
        std::fprintf(stderr, "Failed to initialise GLFW\n");
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    GLFWwindow* window = glfwCreateWindow(1600, 900, "Metro Station", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "Failed to create an OpenGL 3.3 window\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    if (!loadGL()) return 1;
    std::printf("OpenGL %s on %s\n", glGetString(GL_VERSION), glGetString(GL_RENDERER));
    std::printf("Controls: WASD move | mouse look | Shift run | Space jump | F flashlight | N noclip\n"
                "          B bloom | 1 platform | 2 concourse | 3 exit stairs | Esc quit\n");

    if (!shotMode) {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        if (glfwRawMouseMotionSupported()) glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    }

    GLuint sceneProg = makeProgram(SCENE_VS, SCENE_FS);
    GLuint brightProg = makeProgram(FULLSCREEN_VS, BRIGHT_FS);
    GLuint blurProg = makeProgram(FULLSCREEN_VS, BLUR_FS);
    GLuint compProg = makeProgram(FULLSCREEN_VS, COMPOSITE_FS);

    GLuint lightUbo;
    glGenBuffers(1, &lightUbo);
    glBindBuffer(GL_UNIFORM_BUFFER, lightUbo);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(LightBlock), nullptr, GL_DYNAMIC_DRAW);
    glUniformBlockBinding(sceneProg, glGetUniformBlockIndex(sceneProg, "Lights"), 0);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, lightUbo);

    GLint uViewProj = glGetUniformLocation(sceneProg, "uViewProj");
    GLint uCamPos = glGetUniformLocation(sceneProg, "uCamPos");
    GLint uFogColor = glGetUniformLocation(sceneProg, "uFogColor");
    GLint uFogDensity = glGetUniformLocation(sceneProg, "uFogDensity");
    GLint uAmbient = glGetUniformLocation(sceneProg, "uAmbient");
    GLint uAlpha = glGetUniformLocation(sceneProg, "uAlpha");

    GLuint emptyVao;
    glGenVertexArrays(1, &emptyVao);

    World world;
    world.build();
    GpuMesh staticMesh, staticGlass, dynMesh, dynGlass;
    staticMesh.upload(world.staticOpaque, false);
    staticGlass.upload(world.staticGlass, false);
    std::printf("Static geometry: %zu vertices\n", world.staticOpaque.verts.size() + world.staticGlass.verts.size());
    MeshBuilder dynOpaque, dynGlassB;

    Player player;
    RenderTargets rt;
    LightBlock lb;
    std::vector<Light> lights;
    bool flashlight = false, bloom = true;
    bool prevF = false, prevN = false, prevB = false;
    const vec3 fogColor(0.008f, 0.008f, 0.01f);

    double lastTime = glfwGetTime(), fpsTimer = 0.0;
    int frames = 0;

    while (!glfwWindowShouldClose(window)) {
        double now = glfwGetTime();
        float dt = float(std::min(now - lastTime, 0.05));
        lastTime = now;

        glfwPollEvents();
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) glfwSetWindowShouldClose(window, 1);

        // Mouse look
        double mx, my;
        glfwGetCursorPos(window, &mx, &my);
        if (firstMouse) { lastMouseX = mx; lastMouseY = my; firstMouse = false; }
        player.yaw += float(mx - lastMouseX) * 0.0022f;
        player.pitch -= float(my - lastMouseY) * 0.0022f;
        player.pitch = clamp(player.pitch, -1.5f, 1.5f);
        lastMouseX = mx; lastMouseY = my;

        auto key = [&](int k) { return glfwGetKey(window, k) == GLFW_PRESS; };
        auto toggled = [&](int k, bool& prev) { bool d = key(k); bool t = d && !prev; prev = d; return t; };
        if (toggled(GLFW_KEY_F, prevF)) flashlight = !flashlight;
        if (toggled(GLFW_KEY_N, prevN)) player.noclip = !player.noclip;
        if (toggled(GLFW_KEY_B, prevB)) bloom = !bloom;
        if (key(GLFW_KEY_1)) player.respawn({-22.0f, 0.0f, 4.6f}, 3.14159265f);
        if (key(GLFW_KEY_2)) player.respawn({22.0f, dim::CONC, 0.0f}, 3.14159265f);
        if (key(GLFW_KEY_3)) player.respawn({31.0f, dim::CONC, 0.0f}, 0.0f);

        PlayerInput in;
        in.forward = float(key(GLFW_KEY_W)) - float(key(GLFW_KEY_S));
        in.right = float(key(GLFW_KEY_D)) - float(key(GLFW_KEY_A));
        in.up = float(key(GLFW_KEY_SPACE)) - float(key(GLFW_KEY_LEFT_CONTROL));
        in.run = key(GLFW_KEY_LEFT_SHIFT);
        in.jump = key(GLFW_KEY_SPACE);

        bool capture = false;
        if (shotMode) {
            dt = 1.0f / 30.0f;
            in = PlayerInput();
            const Shot& s = SHOTS[shotIndex];
            if (world.time >= s.time) {
                player.respawn(s.feet, s.yaw);
                player.pitch = s.pitch;
                player.noclip = true;
                capture = true;
            }
        }

        // Simulate
        world.update(dt, player.feet);
        world.buildDynamic(dynOpaque, dynGlassB);
        player.update(dt, in, world);

        int fbw, fbh;
        glfwGetFramebufferSize(window, &fbw, &fbh);
        if (fbw == 0 || fbh == 0) continue; // minimised
        if (fbw != rt.w || fbh != rt.h) rt.create(fbw, fbh);

        dynMesh.upload(dynOpaque, true);
        dynGlass.upload(dynGlassB, true);

        // Lights
        vec3 eye = player.eye(), fwd = player.forward();
        lights.clear();
        world.gatherLights(eye, lights, MAX_LIGHTS - 1);
        if (flashlight) {
            Light l;
            vec3 rightV = normalize(cross(fwd, vec3(0, 1, 0)));
            l.pos = eye + rightV * 0.25f - vec3(0, 0.25f, 0);
            l.radius = 35.0f;
            l.color = vec3(1.0f, 0.95f, 0.85f) * 30.0f;
            l.dir = fwd;
            l.cosOuter = std::cos(radians(19.0f));
            lights.push_back(l);
        }
        for (size_t i = 0; i < lights.size(); ++i) {
            const Light& l = lights[i];
            lb.posR[i] = vec4(l.pos, l.radius);
            lb.colI[i] = vec4(l.color, 0.0f);
            lb.dirCos[i] = vec4(l.dir, l.cosOuter);
        }
        lb.count[0] = int(lights.size());
        glBindBuffer(GL_UNIFORM_BUFFER, lightUbo);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(LightBlock), &lb);

        // Scene pass (HDR, 4x MSAA)
        mat4 proj = perspective(radians(72.0f), float(fbw) / float(fbh), 0.05f, 900.0f);
        mat4 view = lookAt(eye, eye + fwd, vec3(0, 1, 0));
        mat4 vp = proj * view;

        glBindFramebuffer(GL_FRAMEBUFFER, rt.msFbo);
        glViewport(0, 0, rt.w, rt.h);
        glClearColor(fogColor.r, fogColor.g, fogColor.b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);

        glUseProgram(sceneProg);
        glUniformMatrix4fv(uViewProj, 1, GL_FALSE, value_ptr(vp));
        glUniform3f(uCamPos, eye.x, eye.y, eye.z);
        glUniform3f(uFogColor, fogColor.r, fogColor.g, fogColor.b);
        glUniform1f(uFogDensity, 0.0065f);
        glUniform1f(uAmbient, 1.0f);
        glUniform1f(uAlpha, 1.0f);
        staticMesh.draw();
        dynMesh.draw();

        // Glass: blended, no depth writes
        glDisable(GL_CULL_FACE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        glUniform1f(uAlpha, 0.14f);
        staticGlass.draw();
        dynGlass.draw();
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);

        // Resolve MSAA
        glBindFramebuffer(GL_READ_FRAMEBUFFER, rt.msFbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, rt.resolveFbo);
        glBlitFramebuffer(0, 0, rt.w, rt.h, 0, 0, rt.w, rt.h, GL_COLOR_BUFFER_BIT, GL_NEAREST);

        // Bloom: bright pass then separable blur at half resolution
        glDisable(GL_DEPTH_TEST);
        glBindVertexArray(emptyVao);
        glActiveTexture(GL_TEXTURE0);
        glViewport(0, 0, rt.bw, rt.bh);
        glBindFramebuffer(GL_FRAMEBUFFER, rt.bloomFbo[0]);
        glUseProgram(brightProg);
        glUniform1i(glGetUniformLocation(brightProg, "uTex"), 0);
        glUniform1f(glGetUniformLocation(brightProg, "uThreshold"), 1.0f);
        glBindTexture(GL_TEXTURE_2D, rt.resolveTex);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        glUseProgram(blurProg);
        glUniform1i(glGetUniformLocation(blurProg, "uTex"), 0);
        GLint uDir = glGetUniformLocation(blurProg, "uDir");
        for (int i = 0; i < 6; ++i) {
            float spread = 1.0f + i * 0.5f;
            glBindFramebuffer(GL_FRAMEBUFFER, rt.bloomFbo[1]);
            glBindTexture(GL_TEXTURE_2D, rt.bloomTex[0]);
            glUniform2f(uDir, spread / rt.bw, 0.0f);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            glBindFramebuffer(GL_FRAMEBUFFER, rt.bloomFbo[0]);
            glBindTexture(GL_TEXTURE_2D, rt.bloomTex[1]);
            glUniform2f(uDir, 0.0f, spread / rt.bh);
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }

        // Composite: tone map, gamma, vignette, crosshair
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, fbw, fbh);
        glUseProgram(compProg);
        glUniform1i(glGetUniformLocation(compProg, "uScene"), 0);
        glUniform1i(glGetUniformLocation(compProg, "uBloom"), 1);
        glUniform1f(glGetUniformLocation(compProg, "uBloomStrength"), bloom ? 0.35f : 0.0f);
        glUniform1f(glGetUniformLocation(compProg, "uExposure"), 1.0f);
        glUniform2f(glGetUniformLocation(compProg, "uRes"), float(fbw), float(fbh));
        glUniform1i(glGetUniformLocation(compProg, "uCrosshair"), 1);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, rt.resolveTex);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, rt.bloomTex[0]);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glActiveTexture(GL_TEXTURE0);

        if (capture) {
            char path[256];
            std::snprintf(path, sizeof(path), "shots/%s.ppm", SHOTS[shotIndex].name);
            savePPM(path, fbw, fbh);
            std::printf("Saved %s\n", path);
            if (++shotIndex == sizeof(SHOTS) / sizeof(SHOTS[0])) glfwSetWindowShouldClose(window, 1);
        }

        glfwSwapBuffers(window);

        // Title bar stats
        ++frames;
        fpsTimer += dt;
        if (fpsTimer >= 0.5) {
            char title[256];
            std::snprintf(title, sizeof(title),
                          "Metro Station | %.0f FPS | %zu lights | pos %.1f %.1f %.1f%s%s | WASD Shift Space F N B 1-3 Esc",
                          frames / fpsTimer, lights.size(), player.feet.x, player.feet.y, player.feet.z,
                          player.noclip ? " | NOCLIP" : "", player.ridingTrain() ? " | ON TRAIN" : "");
            glfwSetWindowTitle(window, title);
            frames = 0;
            fpsTimer = 0.0;
        }
    }

    rt.destroy();
    glfwTerminate();
    return 0;
}
