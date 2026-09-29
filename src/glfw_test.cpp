// Sample GLFW program: a rotating colored triangle.
#include <GLFW/glfw3.h>
#include <cstdio>

int main() {
    if (!glfwInit()) {
        std::fprintf(stderr, "Failed to initialize GLFW\n");
        return 1;
    }

    GLFWwindow* window = glfwCreateWindow(640, 480, "GLFW test - press Esc to quit", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "Failed to create window\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    std::printf("OpenGL version: %s\n", glGetString(GL_VERSION));
    std::printf("Renderer:       %s\n", glGetString(GL_RENDERER));

    while (!glfwWindowShouldClose(window)) {
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, 1);

        glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glLoadIdentity();
        glRotatef((float)glfwGetTime() * 60.0f, 0.0f, 0.0f, 1.0f);

        glBegin(GL_TRIANGLES);
        glColor3f(1.0f, 0.0f, 0.0f); glVertex2f(-0.6f, -0.5f);
        glColor3f(0.0f, 1.0f, 0.0f); glVertex2f( 0.6f, -0.5f);
        glColor3f(0.0f, 0.0f, 1.0f); glVertex2f( 0.0f,  0.6f);
        glEnd();

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwTerminate();
    return 0;
}
