#include <iostream>
#include <random>
#include <cmath>

#include "shader.h"
#include "simulation.h"
#include "settings.h"
#include "Mesh.h"

#include "glad/glad.h"
#include <GLFW/glfw3.h>

using namespace std;

// ---------------- RANDOM ----------------

namespace Mathf {
    float randomFloat(float max) {
        static std::mt19937 gen{ std::random_device{}() };
        std::uniform_real_distribution<float> dist(0.0f, max);
        return dist(gen);
    }
}

// ---------------- PARTICLE SPAWNER ----------------

void spawnParticles(Simulation& simulation, int count, bool deposit, bool ion) {

    constexpr float pi = 3.1415926f;
    float halfAngle = ion ? pi / 20.0f : pi/2;

    float cosTheta = cos(halfAngle);

    for (int i = 0; i < count; i++) {

        Particle p{};
        p.deposit = deposit;
        p.speed = 10.0f;
        p.energy = Mathf::randomFloat(1) * 50.0f;

        float u = Mathf::randomFloat(1);
        float v = Mathf::randomFloat(1);

        float y = cosTheta + u * (1.0f - cosTheta);
        float phi = 2.0f * pi * v;
        float r = sqrt(1.0f - y * y);

        p.dx = r * cos(phi);
        p.dy = y;
        p.dz = r * sin(phi);
        

        p.x = Mathf::randomFloat(Settings::X);
        p.y = 5 + Mathf::randomFloat(1) * 5;
        p.z = (80 / 2 - 10) + Mathf::randomFloat(1) * 20;

        simulation.initParticle(p);
    }
}

// ---------------- RENDER LOOP ----------------
void renderMesh(Simulation& simulation) {

    if (!glfwInit()) return;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window =
        glfwCreateWindow(1280, 720, "Bosch Etch Mesh", nullptr, nullptr);
    if (!window) return;

    glfwMakeContextCurrent(window);
    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
    int major = 0, minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    std::cout << "OpenGL version: " << major << "." << minor << std::endl;

    glEnable(GL_DEPTH_TEST);
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

    Shader shader(
        string(PROJECT_ROOT) + "/shaders/vertex.shader",
        string(PROJECT_ROOT) + "/shaders/fragment.shader"
    );

    glUseProgram(shader.shaderProgram);
    
    // ---------- MATRICES ----------
    float aspect = 1280.0f / 720.0f;
    float nearP = -10.0f;
    float farP = 10.0f;

    float Projection[16] = {
        1.0f / aspect, 0, 0, 0,
        0, 1.0f, 0, 0,
        0, 0, -2.0f / (farP - nearP), -(farP + nearP) / (farP - nearP),
        0, 0, 0, 1
    };

    float Size[16] = {
        2.0f / Settings::X, 0, 0, -1.0f,
        0, 2.0f / Settings::Y, 0, -1.0f,
        0, 0, 2.0f / Settings::Z, -1.0f,
        0, 0, 0, 1
    };

    float Transform[16] = {
         1,  0,  0,  0,
         0, -1,  0, -0.5f,
         0,  0,  1,  0.5f,
         0,  0,  0,  1
    };

    glUniformMatrix4fv(
        glGetUniformLocation(shader.shaderProgram, "Projection"),
        1, GL_TRUE, Projection
    );

    glUniformMatrix4fv(
        glGetUniformLocation(shader.shaderProgram, "Transform"),
        1, GL_TRUE, Transform
    );


    glUniformMatrix4fv(
        glGetUniformLocation(shader.shaderProgram, "Size"),
        1, GL_TRUE, Size
    );

    // ---------- MESH ----------
    Mesh mesh(simulation.grid);
    mesh.setRenderingProgram(shader.shaderProgram);

    mesh.initGPU();
    mesh.uploadVoxels();
    mesh.buildMesh();
    std::cout << "vertCount = " << mesh.vertCount << std::endl;

    int frame = 0;
    float theta = 3.14f / 2.0f;

    while (!glfwWindowShouldClose(window)) {

        glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS)
            theta -= 0.002f;
        if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS)
            theta += 0.002f;
        if (glfwGetKey(window, GLFW_KEY_1) == GLFW_PRESS) {
            spawnParticles(simulation, 10000,0,1);
        }
        if (glfwGetKey(window, GLFW_KEY_2) == GLFW_PRESS) {
            spawnParticles(simulation, 10000, 1, 1);
        }
        float c = cos(theta);
        float s = sin(theta);

        float Rotate[16] = {
             c, 0,  s, 0,
             0, 1,  0, 0,
            -s, 0,  c, 0,
             0, 0,  0, 1
        };

        glUniformMatrix4fv(
            glGetUniformLocation(shader.shaderProgram, "Rotate"),
            1, GL_TRUE, Rotate
        );

        // ---- SIMULATION ----
        simulation.tick(Settings::dt);

        // ---- UPDATE MESH OCCASIONALLY ----
        frame++;
        if (frame % 10 == 0) {
            mesh.uploadVoxels();
            mesh.buildMesh();
        }
        
        // ---- DRAW ----
        mesh.draw();

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwTerminate();
}

// ---------------- MAIN ----------------

int main() {

    Simulation simulation(
        Settings::X,
        Settings::Y,
        Settings::Z,
        Settings::voxelSize
    );

    Voxel voxel{};
    voxel.solid = 1;
    voxel.type = 1;
    voxel.threshold = 100;
    voxel.depositThreshold = 10;

    simulation.initRectangle(
        voxel,
        2, 10, 0,
        Settings::X - 2, 60, 80
    );

    
    // -------- RUN --------
    renderMesh(simulation);

    return 0;
}
