// Sample freeglut program: a rotating colored triangle.
#include <GL/freeglut.h>

static float angle = 0.0f;

void display() {
    glClear(GL_COLOR_BUFFER_BIT);
    glLoadIdentity();
    glRotatef(angle, 0.0f, 0.0f, 1.0f);

    glBegin(GL_TRIANGLES);
    glColor3f(1.0f, 0.0f, 0.0f); glVertex2f(-0.6f, -0.5f);
    glColor3f(0.0f, 1.0f, 0.0f); glVertex2f( 0.6f, -0.5f);
    glColor3f(0.0f, 0.0f, 1.0f); glVertex2f( 0.0f,  0.6f);
    glEnd();

    glutSwapBuffers();
}

void timer(int) {
    angle += 1.0f;
    glutPostRedisplay();
    glutTimerFunc(16, timer, 0);
}

void keyboard(unsigned char key, int, int) {
    if (key == 27) glutLeaveMainLoop(); // Esc to quit
}

int main(int argc, char** argv) {
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB);
    glutInitWindowSize(640, 480);
    glutCreateWindow("freeglut test - press Esc to quit");

    glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
    glutDisplayFunc(display);
    glutKeyboardFunc(keyboard);
    glutTimerFunc(16, timer, 0);
    glutMainLoop();
    return 0;
}
