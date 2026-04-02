#include <iostream>
#include <random>
#include <cmath>

#include "shader.h"
#include "simulation.h"
#include "settings.h"
#include "Mesh.h"
#include "Measurments.h"

#include "glad/glad.h"
#include <GLFW/glfw3.h>
#include <chrono>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

using namespace std;
// ---------------- RANDOM --------------------------
namespace Mathf {
    float randomFloat(float max) {
        static std::mt19937 gen{ std::random_device{}() };
        std::uniform_real_distribution<float> dist(0.0f, max);
        return dist(gen);
    }
}

struct Vec3 {
    float x, y, z;

    Vec3 operator+(const Vec3& v) const { return { x + v.x,y + v.y,z + v.z }; }
    Vec3 operator-(const Vec3& v) const { return { x - v.x,y - v.y,z - v.z }; }
    Vec3 operator*(float s) const { return { x * s,y * s,z * s }; }
};

Vec3 normalize(Vec3 v)
{
    float l = sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    return { v.x / l,v.y / l,v.z / l };
};

using Clock = std::chrono::high_resolution_clock;
using ms = std::chrono::duration<double, std::milli>;

// Camera Properties
float yaw = 3.14159f / 2.0f;
float pitch = 0.0f;
float D = 2.5f;

double lastMouseX = 0;
double lastMouseY = 0;

bool firstMouse = true;
bool buttonDown = false;

void mouseCallback(GLFWwindow* window, double xpos, double ypos)
{
    if (firstMouse)
    {
        lastMouseX = xpos;
        lastMouseY = ypos;
        firstMouse = false;
    }

    double dx = xpos - lastMouseX;
    double dy = ypos - lastMouseY;

    lastMouseX = xpos;
    lastMouseY = ypos;

    float sensitivity = 0.005f;

    // Only rotate if button is pressed
    if(buttonDown){
        yaw += dx * sensitivity;
        pitch += dy * sensitivity;
    }

    if (pitch > 1.5f) pitch = 1.5f;
    if (pitch < -1.5f) pitch = -1.5f;
}

void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
{
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS)
    {
        buttonDown = true;
    }
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE)
    {
        buttonDown = false;
    }
    
}
void scrollCallback(GLFWwindow* window, double xoffset, double yoffset)
{
    D -= yoffset * 0.3f;

    if (D < 0.3f) D = 0.3f;
    if (D > 20.0f) D = 20.0f;
}

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
    
    //Initialize callback functions
    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    glfwSetCursorPosCallback(window, mouseCallback);
    glfwSetScrollCallback(window, scrollCallback);

    glfwMakeContextCurrent(window);
    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);

    // Initialize OpenGL window centered on the screen
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
    float aspect = float(width) / float(height);
    float nearP = -10.0f;
    float farP = 10.0f;

    float Projection[16] = {
        1.0f / aspect, 0, 0, 0,
        0, 1.0f, 0, 0,
        0, 0, -2.0f / (farP - nearP), -(farP + nearP) / (farP - nearP),
        0, 0, 0, 1
    };

    float Transform[16] = {
        1, 0,  0, -Settings::X/2,
        0, -1, 0, Settings::Y/2,
        0, 0,  1, -Settings::Z/2,
        0, 0,  0, 1
    };

    // Initialize Matrices
    glUniformMatrix4fv(
        glGetUniformLocation(shader.shaderProgram, "Projection"),
        1, GL_TRUE, Projection
    );

    glUniformMatrix4fv(
        glGetUniformLocation(shader.shaderProgram, "Transform"),
        1, GL_TRUE, Transform
    );

    
    // Inititalize Mesh 
    Mesh mesh(simulation.grid);

    mesh.setRenderingProgram(shader.shaderProgram);

    simulation.createBuffers();
    simulation.uploadVoxels(simulation.grid.voxels);

    mesh.initGPU();
    mesh.setVoxelBuffer(simulation.voxelSSBO);
    mesh.buildMesh();

    int frame = 0;
    double tickTime = 0;

    bool pause = false;
    bool draw = false;

    // Initialize Measurment function
    Measure measure;
    
    float duration = 3000;
    int waitTime = 10;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();

    // Style (optional but makes it look less 2002)
    ImGui::StyleColorsDark();

    // Backend init
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 430");
    while (!glfwWindowShouldClose(window)) {

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::Begin("Simulation Controls");

        ImGui::SliderFloat("Duration", &duration, 0, 10000);
        ImGui::SliderInt("Wait Time", &waitTime, 1, 100);
        ImGui::Checkbox("Pause", &pause);
        ImGui::Checkbox("Draw", &draw);

        ImGui::End();
        glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        float cy = cos(yaw);
        float sy = sin(yaw);

        float cx = cos(pitch);
        float sx = sin(pitch);

        float Rotate[16] = {
            cy,        0,     sy, 0,
            sx * sy,     cx,   -sx * cy, 0,
           -cx * sy,     sx,    cx * cy, 0,
            0,         0,     0, 1
        };

        glUniformMatrix4fv(
            glGetUniformLocation(shader.shaderProgram, "Rotate"),
            1, GL_TRUE, Rotate
        );
        float scale = 2.0f / (100.0f * D);

        float Size[16] = {
            scale,0,0,0,
            0,scale,0,0,
            0,0,scale,0,
            0,0,0,1
        };
        glUniformMatrix4fv(
            glGetUniformLocation(shader.shaderProgram, "Size"),
            1, GL_TRUE, Size
        );

        auto t1 = Clock::now();
        
        if (frame <= duration && frame % waitTime == 0) {
            simulation.uploadParticles(1e4, 0, 0, Mathf::randomFloat(1000));
        }
        if (!pause) {
            simulation.tick(Settings::dt);
            frame++;
            if (draw && frame % 10 == 0) {
                mesh.buildMesh();
            }

        }

        if (draw) {
            mesh.draw();
        }

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
        glfwPollEvents();

        auto t2 = Clock::now();
        tickTime += ms(t2 - t1).count();
        
        if (frame == duration + 1) {
            cout << tickTime / (duration + 1);

            simulation.downloadVoxels();
            measure.measure(simulation.grid, Settings::X/2, 0, Settings::Z/2, 0, 1, 0);
            
            for (int i = 0;i < measure.ZYPlane.size();i++) {
                if (i % 10 == 0) cout << endl;
                cout << measure.ZYPlane[i] << ", ";
            }
            
            std::vector<float> conv = measure.convolve(measure.ZYPlane, 5);
            int depth = measure.getDepth(conv);

            cout << endl;
            cout << "Depth: " << depth;
            cout << endl;
            cout << "Width: " << measure.getWidth(conv, depth / 2);
        }
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwTerminate();
}

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

    //simulation.initRectangle(mask, 0, 5, 0,Settings::X, 10, Settings::Z/3);
    //simulation.initRectangle(mask, 0, 5, 2 * Settings::Z / 3, Settings::X, 10, Settings::Z);
    simulation.initRectangle(voxel, 0,10,0,Settings::X, Settings::Y, Settings::Z);

    //Voxel low{};
    //low.solid = 1;
    //low.type = 1;
    //low.threshold = 100;
    //low.depositThreshold = 10;

    //Voxel high{};
    //high.solid = 1;
    //high.type = 3;
    //high.threshold = 5000;
    //high.depositThreshold = 5000;

    //int gradientWidth = 6;
    //int coverThickness = 4;   // wall thickness

    //for (int x = 0; x < Settings::X; x++) {
    //    for (int y = 0; y < Settings::Y - 5; y++) {
    //        for (int z = 0; z < Settings::Z; z++) {

    //            Voxel v;

    //            // shell but OPEN TOP
    //            if (
    //                x < coverThickness || x >= Settings::X - coverThickness ||
    //                z < coverThickness || z >= Settings::Z - coverThickness ||
    //                y >= Settings::Y - coverThickness   // top wall
    //                )
    //            {
    //                v = high;
    //            }
    //            else {

    //                float region = (float)z / Settings::Z;

    //                bool lowRegion =
    //                    (region < 0.2f) ||
    //                    (region > 0.4f && region < 0.6f) ||
    //                    (region > 0.8f);

    //                if (lowRegion) v = low;
    //                else v = high;

    //                int distToBoundary =
    //                    std::min({ abs(z - Settings::Z / 5),
    //                               abs(z - 2 * Settings::Z / 5),
    //                               abs(z - 3 * Settings::Z / 5),
    //                               abs(z - 4 * Settings::Z / 5) });

    //                if (distToBoundary < gradientWidth) {
    //                    float t = (float)distToBoundary / gradientWidth;

    //                    int lowT = low.threshold;
    //                    int highT = high.threshold;

    //                    if (v.type == 1)
    //                        v.threshold = lowT + (highT - lowT) * (1.0f - t);
    //                    else
    //                        v.threshold = highT - (highT - lowT) * (1.0f - t);
    //                }
    //            }

    //            simulation.setVoxel(x, y + 5, z, v);
    //        }
    //    }
    //}
    /*

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
    );*/


    // Start loop
    renderMesh(simulation);

    return 0;
}
