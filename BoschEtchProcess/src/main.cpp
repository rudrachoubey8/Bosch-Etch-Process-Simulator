#include <iostream>
#include <random>
#include <cmath>
#include <fstream>

#include "shader.h"
#include "simulation.h"
#include "settings.h"
#include "Mesh.h"
#include "Measurments.h"

#include "glad/glad.h"
#include <GLFW/glfw3.h>
#include <chrono>


#include "stackSimulations.h"
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
    if (buttonDown) {
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

    const int width = 1920;
    const int height = 1080;

    GLFWwindow* window =
        glfwCreateWindow(width, height, "Bosch Etch Mesh", nullptr, nullptr);
    if (!window) return;

    // Initialize callback functions
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

    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

    Shader shader(
        "shaders/vertex.shader",
        "shaders/fragment.shader"
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
        1, 0,  0, -Settings::X / 2,
        0, 1,  0, -Settings::Y / 2,
        0, 0,  1, -Settings::Z / 2,
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
    simulation.chunk();

    simulation.uploadChunks(simulation.chunks);

    mesh.initGPU();
    vector<Voxel> v = simulation.grid.voxels;
    
    mesh.setChunkSSBO(simulation.chunkSSBO);
    mesh.buildMesh();

    int frame = 0;
    double tickTime = 0;

    bool pause = 1;
    bool draw = 1;
    bool ion = 0;

    // Initialize Measurment function
    Measure measure;
    
    int duration = 3000;
    int waitTime = 10;

    int voxelType = 1;
    int solid = 1;
    

    int x0 = 0, x1 = 0;
    int y0 = 0, y1 = 0;
    int z0 = 0, z1 = 0;

    float voxelThreshold = 500;
    float voxelDepositThreshold = 500;

    int typesOfVoxels = 3;
    
    int selectedParticleType = 0;
    int typesOfParticles = 4;

    std::vector<ParticleTypeData> particleTypes(typesOfParticles);

    vector<float> gridData(typesOfParticles * typesOfVoxels * 3, 0.0f);

    static char gridFilename[256] = "grid.dat";
    simulation.tick(gridData, typesOfVoxels, typesOfParticles);


    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();


    ImGui::StyleColorsDark();

    // Backend init
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 430");

    while (!glfwWindowShouldClose(window)) {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        //
        // ========================= PARTICLE WINDOW =========================
        //
        ImGui::Begin("Particle Controls");

        ImGui::InputInt("Particle Types", &typesOfParticles);

        if (typesOfParticles < 1)
            typesOfParticles = 1;

        if (particleTypes.size() != typesOfParticles)
        {
            particleTypes.resize(typesOfParticles);
        }

        ImGui::SliderInt("Duration", &duration, 0, 10000);
        ImGui::SliderInt("Wait Time", &waitTime, 1, 100);

        ImGui::SliderInt(
            "Selected Particle Type",
            &selectedParticleType,
            0,
            typesOfParticles - 1
        );

        ParticleTypeData& p =
            particleTypes[selectedParticleType];

        ImGui::InputInt("Particle Count", &p.count);
        ImGui::InputFloat("Mean Energy", &p.energy);
        ImGui::InputFloat("Std Dev", &p.stddev);
        ImGui::InputFloat("Half Angle", &p.halfAngle);

        ImGui::Checkbox("Deposit", &p.deposit);

        ImGui::Checkbox("Pause", &pause);
        ImGui::Checkbox("Draw", &draw);


        if (ImGui::Button("Reset"))
        {
            frame = 0;
            tickTime = 0;

            //simulation.uploadChunks(v);

            mesh.initGPU();
            mesh.setChunkSSBO(simulation.chunkSSBO);
            mesh.buildMesh();
        }

        ImGui::End();

        
        // ========================= PROBABILITY WINDOW ========================= //
        
        ImGui::Begin("Reaction Probability Grid");
        RenderDynamicInputGrid(typesOfParticles, typesOfVoxels, gridData, 0);
        ImGui::End();

        // ========================= DEPOSIT WINDOW ========================= //

        ImGui::Begin("Deposit Probability Grid");
        RenderDynamicInputGrid(typesOfParticles, typesOfVoxels, gridData, 1);
        ImGui::End();

        // ========================= ADSORB WINDOW ========================= //

        ImGui::Begin("Adsorb Probability Grid");
        RenderDynamicInputGrid(typesOfParticles, typesOfVoxels, gridData, 2);
        ImGui::End();

        // ========================= GRID FILE WINDOW ========================= //
        
        ImGui::Begin("Grid Save/Load");

        ImGui::InputText("Filename", gridFilename, IM_ARRAYSIZE(gridFilename));

        if (ImGui::Button("Save Grid"))
        {
            std::ofstream out(gridFilename, std::ios::binary);

            if (out.is_open())
            {
                size_t voxelCount = simulation.grid.voxels.size();

                out.write((char*)&voxelCount, sizeof(size_t));
                out.write(
                    (char*)simulation.grid.voxels.data(),
                    voxelCount * sizeof(Voxel)
                );

                out.close();

                std::cout << "Saved grid to: " << gridFilename << std::endl;
            }
            else
            {
                std::cout << "Failed to save grid.\n";
            }
        }

        if (ImGui::Button("Load Grid"))
        {
            std::ifstream in(gridFilename, std::ios::binary);

            if (in.is_open())
            {
                size_t voxelCount = 0;

                in.read((char*)&voxelCount, sizeof(size_t));

                if (voxelCount == simulation.grid.voxels.size())
                {
                    in.read(
                        (char*)simulation.chunks.data(),
                        simulation.chunks.size() * sizeof(Chunk)
                    );

                    in.close();

                    simulation.uploadChunks(simulation.chunks);

                    mesh.initGPU();
                    mesh.setChunkSSBO(simulation.chunkSSBO);
                    mesh.buildMesh();

                    std::cout << "Loaded grid from: " << gridFilename << std::endl;
                }
                else
                {
                    std::cout << "Voxel count mismatch.\n";
                    in.close();
                }
            }
            else
            {
                std::cout << "Failed to load grid.\n";
            }
        }

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

        static auto previousTime = Clock::now();

        static double accumulator = 0.0;

        const double fixedDelta = 1.0 / 240.0;

        auto currentTime = Clock::now();

        double deltaTime =
            std::chrono::duration<double>(
                currentTime - previousTime
            ).count();

        previousTime = currentTime;

        accumulator += deltaTime;

        while (accumulator >= fixedDelta)
        {
            if (!pause)
            {
                auto t1 = Clock::now();

                if (
                    waitTime > 0 &&
                    frame <= duration &&
                    frame % waitTime == 0
                    )
                {
                    for (int i = 0; i < particleTypes.size(); i++)
                    {
                        ParticleTypeData& p =
                            particleTypes[i];

                        simulation.uploadParticles(
                            p,i
                        );
                    }
                }

                simulation.tick(
                    gridData,
                    typesOfVoxels,
                    typesOfParticles
                );

                frame++;

                auto t2 = Clock::now();

                tickTime += ms(t2 - t1).count();
            }

            accumulator -= fixedDelta;
        }
        if (draw) {
            if (draw && frame % 10 == 0) {
                mesh.buildMesh();
            }
            mesh.draw();
        }
        ImGui::Begin("Information");

        ImGui::Separator();

        ImGui::Text("Simulation");

        ImGui::Text("Total Voxels: %d", mesh.voxelCount);

        ImGui::End();


        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
        glfwPollEvents();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwTerminate();

}

int main() {

    renderMesh(stackSimulation());

    return 0;
}