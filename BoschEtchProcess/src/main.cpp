
#include <windows.h>
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
#include "implot.h"



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

    Shader shader(
        "shaders/vertex.shader",
        "shaders/fragment.shader"
    );
    
    // Inititalize Mesh 
    Mesh mesh(simulation.grid);

    mesh.setRenderingProgram(shader.shaderProgram);

    simulation.createBuffers();
    simulation.uploadVoxels(simulation.grid.voxels);

    mesh.initGPU();
    vector<Voxel> v = simulation.grid.voxels;
    
    mesh.setVoxelBuffer(simulation.voxelSSBO);
    //mesh.buildMesh(rayOrigin, viewMatrix);

    int frame = 0;
    double tickTime = 0;

    bool pause = 1;
    bool draw = 1;
    bool ion = 0;

    // Initialize Measurment function
    Measure measure;
    
    int duration = 3000;

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
    char fileName2[256] = "slice.txt";
    int sliceDir = 2;     // 0=XY, 1=XZ, 2=YZ
    int sliceIndex = 0;

    int previewWidth = 0;
    int previewHeight = 0;
    GLuint sliceTexture;

    glGenTextures(1, &sliceTexture);

    glBindTexture(GL_TEXTURE_2D, sliceTexture);

    glTexParameteri(
        GL_TEXTURE_2D,
        GL_TEXTURE_MIN_FILTER,
        GL_NEAREST
    );

    glTexParameteri(
        GL_TEXTURE_2D,
        GL_TEXTURE_MAG_FILTER,
        GL_NEAREST
    );

    glTexParameteri(
        GL_TEXTURE_2D,
        GL_TEXTURE_WRAP_S,
        GL_CLAMP_TO_EDGE
    );

    glTexParameteri(
        GL_TEXTURE_2D,
        GL_TEXTURE_WRAP_T,
        GL_CLAMP_TO_EDGE
    );

    std::vector<ParticleTypeData> particleTypes(typesOfParticles);

    vector<float> gridData(typesOfParticles * typesOfVoxels * 3, 0.0f);

    static char gridFilename[256] = "grid.dat";
    static char dllFile[256] = "stack.dll";

    static HMODULE simulationDLL = nullptr;

    simulation.tick(gridData, typesOfVoxels, typesOfParticles);


    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();


    ImGui::StyleColorsDark();
    ImPlot::StyleColorsDark();

    // Backend init
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 430");

    

    while (!glfwWindowShouldClose(window)) {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        float cy = cos(yaw);
        float sy = sin(yaw);

        float cx = cos(pitch);
        float sx = sin(pitch);



        float rayOrigin[3] = {
            D * cx * sin(yaw),
            D * sx,
            D * cx * cos(yaw)
        };

        // Center of the voxel grid in world space
        float centerX = 0;
        float centerY = 0;
        float centerZ = 0;

        // Orbit: place camera on a sphere around center
        float camX = centerX + D * cos(pitch) * sin(yaw);
        float camY = centerY + D * sin(pitch);
        float camZ = centerZ + D * cos(pitch) * cos(yaw);

        float rayOriginArr[3] = { camX, camY, camZ };

        // Forward = camera -> center (always looks at the mesh)
        float fx = centerX - camX;
        float fy = centerY - camY;
        float fz = centerZ - camZ;
        float fLen = sqrt(fx * fx + fy * fy + fz * fz);
        fx /= fLen; fy /= fLen; fz /= fLen;

        // Right = cross(forward, world_up)
        float rx = fz;
        float ry = 0.0f;
        float rz = -fx;
        float rLen = sqrt(rx * rx + rz * rz);
        if (rLen > 0.0001f) { rx /= rLen; rz /= rLen; }

        // Up = cross(right, forward)
        float ux = ry * fz - rz * fy;
        float uy = rz * fx - rx * fz;
        float uz = rx * fy - ry * fx;

        // Column-major mat3
        float viewMatrix[9] = {
            rx, -ux, fx,
            ry, -uy, fy,
            rz, -uz, fz
        };



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

        ImGui::SliderInt("Duration", &duration, 0, 50000);
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
        ImGui::InputInt("Release Interval", &p.interval);

        if (p.interval < 1)
            p.interval = 1;

        ImGui::Checkbox("Deposit", &p.deposit);

        ImGui::Checkbox("Pause", &pause);
        ImGui::Checkbox("Draw", &draw);


        if (ImGui::Button("Reset"))
        {
            frame = 0;
            tickTime = 0;

            simulation.uploadVoxels(v);

            mesh.initGPU();
            mesh.setVoxelBuffer(simulation.voxelSSBO);
            mesh.buildMesh(rayOrigin, viewMatrix, sliceDir, sliceIndex);
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

        
        // ========================= VOXEL WINDOW ========================= //
        
        ImGui::Begin("Voxel Editor");

        ImGui::Text("Voxel Settings");

        ImGui::SliderInt("Voxel Type", &voxelType, 1, 3);
        ImGui::InputFloat("Threshold", &voxelThreshold);
        ImGui::InputFloat("Deposit Threshold", &voxelDepositThreshold);
        ImGui::SliderInt("Solid", &solid, 0, 1);

        ImGui::Separator();

        ImGui::Text("Fill Region");

        ImGui::InputInt("x0", &x0);
        ImGui::InputInt("x1", &x1);

        ImGui::InputInt("y0", &y0);
        ImGui::InputInt("y1", &y1);

        ImGui::InputInt("z0", &z0);
        ImGui::InputInt("z1", &z1);

        if (ImGui::Button("Fill"))
        {
            Voxel voxel{};

            voxel.solid = solid;
            voxel.type = voxelType;
            voxel.threshold = voxelThreshold;
            voxel.depositThreshold = voxelDepositThreshold;

            simulation.initRectangle(voxel, x0, y0, z0, x1, y1, z1);

            v = simulation.grid.voxels;
            simulation.uploadVoxels(v);
        }

        ImGui::End();
        
        // ========================= GRID FILE WINDOW ========================= //
        
        ImGui::Begin("Grid Save/Load");


        ImGui::InputText("Filename", gridFilename, IM_ARRAYSIZE(gridFilename));
        if (ImGui::Button("Save Grid"))
        {
            std::ofstream out(gridFilename, std::ios::binary);

            if (out.is_open())
            {
                // Grid
                size_t voxelCount = simulation.grid.voxels.size();

                out.write((char*)&voxelCount, sizeof(size_t));
                out.write(
                    (char*)simulation.grid.voxels.data(),
                    voxelCount * sizeof(Voxel)
                );

                // Settings
                out.write((char*)&duration, sizeof(duration));

                out.write((char*)&voxelType, sizeof(voxelType));
                out.write((char*)&solid, sizeof(solid));

                out.write((char*)&voxelThreshold, sizeof(voxelThreshold));
                out.write((char*)&voxelDepositThreshold, sizeof(voxelDepositThreshold));

                out.write((char*)&typesOfVoxels, sizeof(typesOfVoxels));
                out.write((char*)&typesOfParticles, sizeof(typesOfParticles));

                // Particle types
                size_t particleCount = particleTypes.size();

                out.write(
                    (char*)&particleCount,
                    sizeof(particleCount)
                );

                out.write(
                    (char*)particleTypes.data(),
                    particleCount * sizeof(ParticleTypeData)
                );

                // Reaction / Deposit / Adsorb grid
                size_t gridSize = gridData.size();

                out.write((char*)&gridSize, sizeof(gridSize));

                out.write(
                    (char*)gridData.data(),
                    gridSize * sizeof(float)
                );

                out.close();

                std::cout << "Saved grid + settings to: "
                    << gridFilename << std::endl;
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
                        (char*)simulation.grid.voxels.data(),
                        voxelCount * sizeof(Voxel)
                    );

                    // Settings
                    in.read((char*)&duration, sizeof(duration));
                    in.read((char*)&voxelType, sizeof(voxelType));
                    in.read((char*)&solid, sizeof(solid));

                    in.read((char*)&voxelThreshold, sizeof(voxelThreshold));
                    in.read((char*)&voxelDepositThreshold, sizeof(voxelDepositThreshold));

                    in.read((char*)&typesOfVoxels, sizeof(typesOfVoxels));
                    in.read((char*)&typesOfParticles, sizeof(typesOfParticles));

                    // Particle types
                    size_t particleCount;

                    in.read(
                        (char*)&particleCount,
                        sizeof(particleCount)
                    );

                    particleTypes.resize(particleCount);

                    in.read(
                        (char*)particleTypes.data(),
                        particleCount * sizeof(ParticleTypeData)
                    );

                    // Grid data
                    size_t gridSize;

                    in.read((char*)&gridSize, sizeof(gridSize));

                    gridData.resize(gridSize);

                    in.read(
                        (char*)gridData.data(),
                        gridSize * sizeof(float)
                    );

                    in.close();

                    simulation.uploadVoxels(
                        simulation.grid.voxels
                    );

                    mesh.initGPU();
                    mesh.setVoxelBuffer(simulation.voxelSSBO);
                    mesh.buildMesh(rayOrigin, viewMatrix, sliceDir, sliceIndex);

                    std::cout << "Loaded grid + settings from: "
                        << gridFilename << std::endl;
                }
                else
                {
                    std::cout << "Voxel count mismatch.\n";
                    in.close();
                }
            }
        }

        ImGui::Separator();
        
        ImGui::InputText(
            "DLL File",
            dllFile,
            sizeof(dllFile)
        );

        if (ImGui::Button("Load Simulation"))
        {
            try
            {
                // unload previous dll
                if (simulationDLL)
                {
                    FreeLibrary(simulationDLL);
                    simulationDLL = nullptr;
                }

                simulationDLL = LoadLibraryA(dllFile);

                if (!simulationDLL)
                {
                    std::cout
                        << "Failed to load DLL: "
                        << dllFile
                        << std::endl;
                }
                else
                {
                    using CreateSimulationFn =
                        Simulation(*)();

                    CreateSimulationFn createSimulation =
                        (CreateSimulationFn)
                        GetProcAddress(
                            simulationDLL,
                            "CreateSimulation"
                        );

                    if (!createSimulation)
                    {
                        std::cout
                            << "CreateSimulation not found"
                            << std::endl;

                        FreeLibrary(simulationDLL);
                        simulationDLL = nullptr;
                    }
                    else
                    {
                        simulation = createSimulation();

                        simulation.createBuffers();

                        simulation.uploadVoxels(
                            simulation.grid.voxels
                        );

                        mesh.initGPU();

                        mesh.setVoxelBuffer(
                            simulation.voxelSSBO
                        );

                        mesh.buildMesh(rayOrigin, viewMatrix, sliceDir, sliceIndex);

                        std::cout
                            << "Simulation loaded from "
                            << dllFile
                            << std::endl;
                    }
                }
            }
            catch (...)
            {
                std::cout
                    << "Exception while loading simulation"
                    << std::endl;
            }
        }

        if (simulationDLL)
        {
            ImGui::TextColored(
                ImVec4(0, 1, 0, 1),
                "DLL Loaded"
            );
        }
        else
        {
            ImGui::TextColored(
                ImVec4(1, 0, 0, 1),
                "No DLL Loaded"
            );
        }


        ImGui::End();
        glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);


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

                if (frame <= duration)
                {
                    for (int i = 0; i < particleTypes.size(); i++)
                    {
                        ParticleTypeData& p = particleTypes[i];

                        if (frame % p.interval == 0)
                        {
                            simulation.uploadParticles(p, i);
                        }
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
                // In your render loop, before buildMesh():
                mesh.buildMesh(rayOrigin, viewMatrix, sliceDir, sliceIndex);
            }
            mesh.draw();
        }
        ImGui::Begin("Information");

        ImGui::Separator();

        ImGui::Text("Simulation");

        ImGui::Text("Total Voxels: %d", mesh.voxelCount);
        ImGui::Text("Axes:");

        ImGui::TextColored(ImVec4(1, 0, 0, 1), "X Axis");
        ImGui::TextColored(ImVec4(0, 0, 1, 1), "Y Axis");
        ImGui::TextColored(ImVec4(1, 1, 0, 1), "Z Axis");


        ImGui::End();


        if (ImGui::Begin("Slice Export"))
        {
            ImGui::InputText("File", fileName2, sizeof(fileName2));

            ImGui::Combo(
                "Direction",
                &sliceDir,
                "XY\0XZ\0YZ\0"
            );

            ImGui::InputInt("Slice Index", &sliceIndex);

            if (ImGui::Button("Extract Slice"))
            {
                std::vector<int> slice =
                    mesh.extractSlice(sliceDir, sliceIndex);

                switch (sliceDir)
                {
                case 0: // XY
                    previewWidth = Settings::X;
                    previewHeight = Settings::Y;
                    break;

                case 1: // XZ
                    previewWidth = Settings::X;
                    previewHeight = Settings::Z;
                    break;

                default: // YZ
                    previewWidth = Settings::Y;
                    previewHeight = Settings::Z;
                    break;
                }

                std::vector<uint32_t> pixels(
                    previewWidth * previewHeight
                );

                for (int i = 0; i < pixels.size(); i++)
                {
                    switch (slice[i])
                    {
                    case -1:
                        pixels[i] = 0xFF000000;
                        break;

                    case 0:
                        pixels[i] = 0xFFFFFFFF;
                        break;

                    case 1:
                        pixels[i] = 0xFFFF0000;
                        break;

                    case 2:
                        pixels[i] = 0xFF00FF00;
                        break;

                    default:
                        pixels[i] = 0xFF0000FF;
                        break;
                    }
                }

                // Update texture
                glBindTexture(GL_TEXTURE_2D, sliceTexture);

                glTexImage2D(
                    GL_TEXTURE_2D,
                    0,
                    GL_RGBA8,
                    previewWidth,
                    previewHeight,
                    0,
                    GL_RGBA,
                    GL_UNSIGNED_BYTE,
                    pixels.data()
                );

                // Save slice to file
                std::ofstream out(fileName2);

                if (out)
                {
                    out << previewWidth
                        << " "
                        << previewHeight
                        << "\n";

                    for (int y = 0; y < previewHeight; y++)
                    {
                        for (int x = 0; x < previewWidth; x++)
                        {
                            out << slice[x + y * previewWidth];

                            if (x != previewWidth - 1)
                                out << ' ';
                        }

                        out << '\n';
                    }

                    out.close();
                }
            }
            
        }
        ImGui::End();
        ImGui::Begin("Graph");
        if (previewWidth && previewHeight && ImPlot::BeginPlot("Slice"))
        {
            ImPlot::SetupAxes(
                "X",
                (sliceDir == 1) ? "Z" : "Y"
            );

            ImPlot::SetupAxisLimits(
                ImAxis_X1,
                0,
                previewWidth,
                ImGuiCond_Always
            );

            ImPlot::SetupAxisLimits(
                ImAxis_Y1,
                0,
                previewHeight,
                ImGuiCond_Always
            );


            ImPlot::PlotImage(
                "Slice",
                (ImTextureID)(intptr_t)sliceTexture,
                ImPlotPoint(0, 0),
                ImPlotPoint(
                    previewWidth,
                    previewHeight
                )
            );

            if (ImPlot::IsPlotHovered())
            {
                ImPlotPoint p =
                    ImPlot::GetPlotMousePos();

                ImGui::Text(
                    "X: %.1f  Y: %.1f",
                    p.x,
                    p.y
                );
            }
            ImPlot::EndPlot();

        }
        ImGui::End();
        ImGui::Render();

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
        glfwPollEvents();

    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwTerminate();

}

int main() {

    Simulation sim = stackSimulation();
    sim.initSDF();

    renderMesh(sim);

    return 0;
}