#include <iostream>
#include <random>
#include <cmath>

#include "shader.h"
#include "simulation.h"
#include "settings.h"
#include "Mesh.h"

#include "glad/glad.h"
#include <GLFW/glfw3.h>
#include <chrono>

using namespace std;
// ---------------- RANDOM --------------------------
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
    float halfAngle = ion ? pi / 10.0f : pi/2.0f;

    float cosTheta = cos(halfAngle);

    for (int i = 0; i < count; i++) {

        Particle p{};
        p.deposit = deposit;
        p.speed = 10.0f;
        p.energy = 50.0f;

        float u = Mathf::randomFloat(1);
        float v = Mathf::randomFloat(1);

        float y = cosTheta + u * (1.0f - cosTheta);
        float phi = 2.0f * pi * v;
        float r = sqrt(1.0f - y * y);

        p.dx = r * cos(phi);
        p.dy = y;
        p.dz = r * sin(phi);

        p.x = Mathf::randomFloat(Settings::X);
        p.y = 1;
        p.z = Mathf::randomFloat(Settings::Z);;

        simulation.initParticle(p);
    }
}

using Clock = std::chrono::high_resolution_clock;
using ms = std::chrono::duration<double, std::milli>;

void renderMesh(Simulation& simulation) {

    if (!glfwInit()) return;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    const int width = 1280;
    const int height = 720;

    GLFWwindow* window =
        glfwCreateWindow(width, height, "Bosch Etch Mesh", nullptr, nullptr);
    if (!window) return;

    glfwMakeContextCurrent(window);
    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);

    // ---- Center window on primary monitor ----
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
    glfwSetWindowPos(
        window,
        (mode->width - width) / 2,
        (mode->height - height) / 2
    );

    glEnable(GL_DEPTH_TEST);
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

    Shader shader(
        string(PROJECT_ROOT) + "/shaders/vertex.shader",
        string(PROJECT_ROOT) + "/shaders/fragment.shader"
    );

    glUseProgram(shader.shaderProgram);

    // MATRICES
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
        2.0f / 100.0f, 0, 0, 0.0f,
        0, 2.0f / 100.0f, 0, 0.0f,
        0, 0, 2.0f / 100.0f, 0.0f,
        0, 0, 0, 1
    };

    float Transform[16] = {
        1, 0,  0, -Settings::X/2,
        0, -1, 0, 0,
        0, 0,  1, -Settings::Z/2,
        0, 0,  0, 1
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

    // ---------------- MESH ----------------
    Mesh mesh(simulation.grid);

    mesh.setRenderingProgram(shader.shaderProgram);

    simulation.createBuffers();
    simulation.uploadVoxels(simulation.grid.voxels);


    mesh.initGPU();
    mesh.setVoxelBuffer(simulation.voxelSSBO);
    mesh.buildMesh();

    int frame = 0;
    float theta = 3.14159f / 2.0f;

    double tickTime = 0;
    double buildTime = 0;

    bool pause = false;

    while (!glfwWindowShouldClose(window)) {

        glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS)
            theta -= 0.01f;
        
        if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS)
            theta += 0.01f; 

        if (glfwGetKey(window, GLFW_KEY_1) == GLFW_PRESS) {
            pause = !pause;
        }
        
        
        if (!pause && frame % 10 == 0) {
            spawnParticles(simulation,100,0,0);
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

        // ---------------- SIMULATION TIMING ----------------
        auto t1 = Clock::now();
        simulation.tick(Settings::dt);
        auto t2 = Clock::now();
        tickTime += ms(t2 - t1).count();

        // ---------------- MESH UPDATE TIMING ----------------
        frame++;

        if (frame % 10 == 0) {
            auto b1 = Clock::now();
            mesh.buildMesh();
            auto b2 = Clock::now();

            buildTime += ms(b2 - b1).count();
        }

        mesh.draw();

        glfwSwapBuffers(window);
        glfwPollEvents();

        // ---- Print every 120 frames ----
        if (frame % 120 == 0) {
            cout << "Tick avg:   " << tickTime / 120.0 << " ms\n";
            cout << "Build avg:  " << buildTime / 12.0 << " ms\n";
            cout << "-----------------------------\n";

            tickTime = 0;
            buildTime = 0;
        }
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


    Voxel mask{};

    mask.solid = 1;
    mask.type = 3;
    mask.threshold = 5000;
    mask.depositThreshold = 5000;

    simulation.initRectangle(
        mask,
        0, 5, 0,
        20, 10, 20
    );
    simulation.initRectangle(
        mask,
        0, 5, 40,
        20, 10, 60
    );

    simulation.initRectangle(
        mask,
        0, 5, 80,
        20, 10, 99
    );

    simulation.initRectangle(
        mask,
        0, 10, 0,
        20, 20, 100
    );

    simulation.initRectangle(
        voxel,
        0, 5, 20,
        20, 10, 40
    );

    simulation.initRectangle(
        voxel,
        0, 5, 60,
        20, 10, 80
    );

    simulation.initRectangle(
        mask,
        0, 10, 100,
        20, 20, 120
    );

    simulation.initRectangle(
        voxel,
        0, 5, 99,
        20, 10, 120
    );

    simulation.initRectangle(
        mask,
        0, 5, 120,
        20, 20, 140
    );

    simulation.initRectangle(
        mask,
        0, 5, 80,
        20, 10, 100
    );


    // -------- RUN --------
    renderMesh(simulation);

    return 0;
}
