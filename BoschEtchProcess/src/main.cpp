#define NOMINMAX
#include <windows.h>
#include <iostream>
#include <random>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <algorithm>
#include <cfloat>


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

namespace
{
    constexpr uint32_t PARTICLE_DATA_MAGIC = 0x42504550;
    constexpr uint32_t PARTICLE_DATA_VERSION = 2;
    constexpr uint32_t MAX_SERIALIZED_ITEMS = 1000000;

    template <typename T>
    bool writeValue(std::ostream& out, const T& value)
    {
        out.write(reinterpret_cast<const char*>(&value), sizeof(T));
        return static_cast<bool>(out);
    }

    template <typename T>
    bool readValue(std::istream& in, T& value)
    {
        in.read(reinterpret_cast<char*>(&value), sizeof(T));
        return static_cast<bool>(in);
    }

    bool writeString(std::ostream& out, const std::string& value)
    {
        const uint32_t size = static_cast<uint32_t>(value.size());
        return writeValue(out, size) &&
            static_cast<bool>(out.write(value.data(), size));
    }

    bool readString(std::istream& in, std::string& value)
    {
        uint32_t size = 0;
        if (!readValue(in, size) || size > MAX_SERIALIZED_ITEMS)
            return false;

        value.resize(size);
        return static_cast<bool>(in.read(value.data(), size));
    }

    bool writeFloatVector(std::ostream& out, const std::vector<float>& values)
    {
        const uint32_t size = static_cast<uint32_t>(values.size());
        return writeValue(out, size) &&
            static_cast<bool>(out.write(
                reinterpret_cast<const char*>(values.data()),
                static_cast<std::streamsize>(size) * sizeof(float)));
    }

    bool readFloatVector(std::istream& in, std::vector<float>& values)
    {
        uint32_t size = 0;
        if (!readValue(in, size) || size > MAX_SERIALIZED_ITEMS)
            return false;

        values.resize(size);
        return static_cast<bool>(in.read(
            reinterpret_cast<char*>(values.data()),
            static_cast<std::streamsize>(size) * sizeof(float)));
    }

    bool writeParticleType(std::ostream& out, const ParticleTypeData& particle)
    {
        const uint8_t deposit = particle.deposit ? 1 : 0;
        const uint8_t draw = particle.draw ? 1 : 0;
        const uint8_t custom = particle.custom ? 1 : 0;

        return
            writeValue(out, particle.count) &&
            writeValue(out, particle.energy) &&
            writeValue(out, particle.stddev) &&
            writeValue(out, particle.halfAngle) &&
            writeValue(out, deposit) &&
            writeValue(out, draw) &&
            writeValue(out, custom) &&
            writeValue(out, particle.interval) &&
            writeString(out, particle.name) &&
            writeFloatVector(out, particle.iedf.energyCenters) &&
            writeFloatVector(out, particle.iedf.pdf);
    }

    bool readParticleType(std::istream& in, ParticleTypeData& particle, uint32_t version)
    {
        uint8_t deposit = 0;
        uint8_t draw = 0;
        uint8_t custom = 0;

        if (!readValue(in, particle.count) ||
            !readValue(in, particle.energy) ||
            !readValue(in, particle.stddev) ||
            !readValue(in, particle.halfAngle) ||
            !readValue(in, deposit) ||
            !readValue(in, draw))
            return false;

        if (version >= 2 && !readValue(in, custom))
            return false;

        if (!readValue(in, particle.interval) ||
            !readString(in, particle.name) ||
            !readFloatVector(in, particle.iedf.energyCenters) ||
            !readFloatVector(in, particle.iedf.pdf))
            return false;

        particle.deposit = deposit != 0;
        particle.draw = draw != 0;
        particle.custom = custom != 0;
        return particle.iedf.energyCenters.size() == particle.iedf.pdf.size();
    }

    int probabilityIndex(
        int particle,
        int voxelType,
        int probabilityType,
        int particleCount,
        int voxelTypeCount)
    {
        return
            particle +
            voxelType * particleCount +
            particleCount * voxelTypeCount * probabilityType;
    }

    void setDefaultProbabilities(
        std::vector<float>& probabilities,
        int particleCount,
        int voxelTypeCount)
    {
        probabilities.assign(particleCount * voxelTypeCount * 3, 0.0f);

        for (int voxel = 0; voxel < voxelTypeCount; ++voxel)
        {
            for (int particle = 0; particle < particleCount; ++particle)
            {
                probabilities[
                    probabilityIndex(
                        particle,
                        voxel,
                        0,
                        particleCount,
                        voxelTypeCount)] = 1.0f;
            }
        }
    }

    void rebuildProbabilityGrid(
        const std::vector<ParticleTypeData>& oldParticles,
        const std::vector<float>& oldGrid,
        const std::vector<ParticleTypeData>& newParticles,
        int voxelTypeCount,
        std::vector<float>& newGrid)
    {
        std::vector<float> rebuilt;
        const int oldParticleCount = static_cast<int>(oldParticles.size());
        const int newParticleCount = std::max(
            1,
            static_cast<int>(newParticles.size()));

        setDefaultProbabilities(
            rebuilt,
            newParticleCount,
            voxelTypeCount);

        for (int newParticle = 0; newParticle < static_cast<int>(newParticles.size()); ++newParticle)
        {
            for (int oldParticle = 0; oldParticle < oldParticleCount; ++oldParticle)
            {
                if (newParticles[newParticle].name != oldParticles[oldParticle].name)
                    continue;

                for (int kind = 0; kind < 3; ++kind)
                {
                    for (int voxel = 0; voxel < voxelTypeCount; ++voxel)
                    {
                        const int oldIndex = probabilityIndex(
                            oldParticle,
                            voxel,
                            kind,
                            oldParticleCount,
                            voxelTypeCount);

                        if (oldIndex >= 0 && oldIndex < static_cast<int>(oldGrid.size()))
                        {
                            rebuilt[
                                probabilityIndex(
                                    newParticle,
                                    voxel,
                                    kind,
                                    newParticleCount,
                                    voxelTypeCount)] = oldGrid[oldIndex];
                        }
                    }
                }
            }
        }

        newGrid.swap(rebuilt);
    }

    ParticleTypeData makeSprayParticle(
        const std::string& name,
        const SpeciesProperties& properties,
        double density)
    {
        ParticleTypeData particle;
        particle.name = name;
        particle.count = static_cast<int>(
            std::clamp(density / 1e18, 1.0, 100000.0));
        particle.energy = properties.charge > 0 ? 50.0f : 5.0f;
        particle.halfAngle = properties.charge > 0 ? 20.0f : 60.0f;
        particle.interval = 10;
        particle.deposit = false;
        particle.draw = true;
        particle.iedf.energyCenters = { particle.energy };
        particle.iedf.pdf = { 1.0f };
        return particle;
    }

    void makeMonoEnergy(ParticleTypeData& particle)
    {
        particle.energy = std::max(particle.energy, 0.01f);
        particle.iedf.energyCenters = { particle.energy };
        particle.iedf.pdf = { 1.0f };
    }

    ParticleTypeData makeFireableFromTemplate(
        const ParticleTypeData& source,
        int duration)
    {
        ParticleTypeData particle = source;
        particle.custom = false;
        particle.interval = std::max(duration, 1);
        return particle;
    }

    ParticleTypeData makeCustomParticle(
        const std::string& name,
        const SpeciesProperties& properties,
        int duration)
    {
        ParticleTypeData particle = makeSprayParticle(name, properties, 1e18);
        particle.custom = true;
        particle.interval = std::max(duration, 1);
        makeMonoEnergy(particle);
        return particle;
    }
}

// ---------------- RANDOM --------------------------
namespace Mathf {
    float randomFloat(float max) {
        static std::mt19937 gen{ std::random_device{}() };
        std::uniform_real_distribution<float> dist(0.0f, max);
        return dist(gen);
    }
}


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
    ImGuiContext* imguiContext = ImGui::GetCurrentContext();
    if (imguiContext && ImGui::GetIO().WantCaptureMouse)
    {
        lastMouseX = xpos;
        lastMouseY = ypos;
        return;
    }

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
    ImGuiContext* imguiContext = ImGui::GetCurrentContext();
    if (imguiContext && ImGui::GetIO().WantCaptureMouse)
    {
        if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE)
            buttonDown = false;
        return;
    }

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
    ImGuiContext* imguiContext = ImGui::GetCurrentContext();
    if (imguiContext && ImGui::GetIO().WantCaptureMouse)
        return;

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

    Page currentPage = Page::RenderPage;

    bool pause = 1;
    bool draw = 1;
    // Initialize Measurment function
    Measure measure;
    
    int duration = 3000;

    int voxelType = 0;
    int solid = 1;
    

    int x0 = 0, x1 = 0;
    int y0 = 0, y1 = 0;
    int z0 = 0, z1 = 0;

    float voxelThreshold = 500;
    float voxelDepositThreshold = 500;

    int typesOfVoxels = 4;
    
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

    vector<float> gridData;

    static char gridFilename[256] = "grid.dat";
    static char dllFile[256] = "stack.dll";

    static HMODULE simulationDLL = nullptr;

    simulation.tick(gridData, typesOfVoxels, typesOfParticles);


    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();


    ImGui::StyleColorsLight();
    ImPlot::StyleColorsLight();

    // Backend init
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 430");

    static BulkModel bulk;
    static Sheath sheath;

    initializeDefaultBulk(bulk);

    advanceModelForDuration(bulk);
    initializeSheath(bulk, sheath);

    vector<ParticleTypeData> particleDataTypes = generateParticles(bulk, sheath);
    for (ParticleTypeData& particle : particleDataTypes)
        particle.interval = duration;
    
    vector<ParticleTypeData> plasmaParticleTemplates = particleDataTypes;
    typesOfParticles = std::max(1, static_cast<int>(particleDataTypes.size()));

    setDefaultProbabilities(gridData, typesOfParticles, typesOfVoxels);

    std::vector<std::string> spraySpeciesNames;
    for (const auto& species : Species)
    {
        if (species.first != "e-")
            spraySpeciesNames.push_back(species.first);
    }
    std::sort(spraySpeciesNames.begin(), spraySpeciesNames.end());

    int selectedSpraySpecies = 0;
    int selectedTemplateParticle = 0;
    int selectedIEDF = 0;

    bool forceMeshBuild = true;
    float lastBuildYaw = yaw;
    float lastBuildPitch = pitch;
    float lastBuildDistance = D;

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


        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("Render Page"))
            {
                currentPage = Page::RenderPage;

                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Plasma Model"))
            {
                currentPage = Page::PlasmaModel;

                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Particles"))
            {
                currentPage = Page::ParticleSetup;

                ImGui::EndMenu();
            }

            ImGui::EndMainMenuBar();
        }



        glClearColor(1.0f,1.0f,1.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);



        if (currentPage == Page::PlasmaModel) {
            ImGui::Begin("Plasma Model");

            ImGui::Separator();
            ImGui::Text("Electron Temp: %.3f eV", bulk.Te0);
            ImGui::Text("Neutral Gas Density: %.3e m^-3", bulk.Ngas);
            ImGui::InputDouble("Absorbed Power (W)", &bulk.Pabs, 1.0, 10.0, "%.3f");
            ImGui::InputDouble("Temperature (K)", &bulk.gasTemp, 1.0, 1000, "%.3f");
            ImGui::InputDouble("Pump Rate", &bulk.pump, 0.1, 1.0, "%.3f");
            ImGui::InputDouble("Time Step", &bulk.dt, 1e-10, 1e-9, "%.3e");
            ImGui::InputDouble("Model Duration", &bulk.duration, 1e-5, 1e-4, "%.3e");
            ImGui::InputDouble("Bias Power (W)", &bulk.biasPower, 1.0, 10.0, "%.3f");
            ImGui::InputDouble("Bias Frequency (Hz)", &bulk.biasFrequency, 1e5, 1e6, "%.3e");
            ImGui::InputDouble("Bias Voltage Guess (V)", &bulk.biasVoltageGuess, 1.0, 10.0, "%.3f");
            ImGui::InputInt("Sheath Points", &bulk.sheathPoints);
            ImGui::InputInt("Sheath Iterations", &bulk.sheathIterations);
            ImGui::InputInt("Transport Ions", &bulk.ionCount);
            ImGui::InputDouble("Ion Time Step", &bulk.ionDt, 1e-10, 1e-9, "%.3e");
            ImGui::InputInt("Max RF Cycles", &bulk.maxCycles);
            ImGui::Checkbox("Momentum Transfer", &bulk.enableMomentumTransfer);
            ImGui::Checkbox("Charge Exchange", &bulk.enableChargeExchange);
            ImGui::InputDouble("MT Scale", &bulk.momentumTransferScale, 0.1, 1.0, "%.3f");
            ImGui::InputDouble("CX Scale", &bulk.chargeExchangeScale, 0.01, 0.1, "%.3f");
           
            if (ImGui::Button("Run Plasma Model"))
            {

                const std::vector<ParticleTypeData> oldParticles = particleDataTypes;
                const std::vector<float> oldGrid = gridData;
                const BulkModel controls = bulk;
                initializeDefaultBulk(bulk, controls.gasTemp);
                bulk.dt = controls.dt;
                bulk.duration = controls.duration;
                bulk.Pabs = controls.Pabs;
                bulk.pump = controls.pump;
                bulk.biasPower = controls.biasPower;
                bulk.biasFrequency = controls.biasFrequency;
                bulk.biasVoltageGuess = controls.biasVoltageGuess;
                bulk.residualVoltageRipple = controls.residualVoltageRipple;
                bulk.sheathPoints = controls.sheathPoints;
                bulk.sheathIterations = controls.sheathIterations;
                bulk.ionCount = controls.ionCount;
                bulk.ionDt = controls.ionDt;
                bulk.maxCycles = controls.maxCycles;
                bulk.enableMomentumTransfer = controls.enableMomentumTransfer;
                bulk.enableChargeExchange = controls.enableChargeExchange;
                bulk.momentumTransferScale = controls.momentumTransferScale;
                bulk.chargeExchangeScale = controls.chargeExchangeScale;
                bulk.useBias = controls.useBias;

                advanceModelForDuration(bulk);
                initializeSheath(bulk, sheath);

                plasmaParticleTemplates = generateParticles(bulk, sheath);
            
                for (ParticleTypeData& particle : plasmaParticleTemplates)
                    particle.interval = duration;

                for (ParticleTypeData& fireable : particleDataTypes)
                {
                    if (fireable.custom)
                        continue;

                    const int savedDuration = fireable.interval;
                    auto matchingTemplate = std::find_if(
                        plasmaParticleTemplates.begin(),
                        plasmaParticleTemplates.end(),
                        [&](const ParticleTypeData& source)
                        {
                            return source.name == fireable.name;
                        });

                    if (matchingTemplate != plasmaParticleTemplates.end())
                    {
                        fireable = makeFireableFromTemplate(*matchingTemplate, savedDuration);
                    }
                }

                for (const ParticleTypeData& source : plasmaParticleTemplates)
                {
                    const bool alreadyFireable = std::any_of(
                        particleDataTypes.begin(),
                        particleDataTypes.end(),
                        [&](const ParticleTypeData& particle)
                        {
                            return particle.name == source.name;
                        });

                    if (!alreadyFireable)
                        particleDataTypes.push_back(makeFireableFromTemplate(source, duration));
                }

                typesOfParticles = std::max(1, static_cast<int>(particleDataTypes.size()));
                rebuildProbabilityGrid(
                    oldParticles,
                    oldGrid,
                    particleDataTypes,
                    typesOfVoxels,
                    gridData);
                frame = 0;
                tickTime = 0;
            }


            if (!sheath.voltageWaveform.empty() && !sheath.thicknessWaveform.empty())
            {
                const auto voltageRange = std::minmax_element(
                    sheath.voltageWaveform.begin(),
                    sheath.voltageWaveform.end());
                const auto thicknessRange = std::minmax_element(
                    sheath.thicknessWaveform.begin(),
                    sheath.thicknessWaveform.end());
                ImGui::Text(
                    "Sheath Voltage: %.3f to %.3f V",
                    *voltageRange.first,
                    *voltageRange.second);
                ImGui::Text(
                    "Sheath Thickness: %.3e to %.3e m",
                    *thicknessRange.first,
                    *thicknessRange.second);
            }
            else
            {
                ImGui::Text("Sheath Voltage: %.3f V", sheath.voltage);
                ImGui::Text("Sheath Thickness: %.3e m", sheath.thickness);
            }
            ImGui::Text("Generated Ion Species: %d", static_cast<int>(plasmaParticleTemplates.size()));




            if (ImGui::BeginTable("GeneratedIons", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
            {
                ImGui::TableSetupColumn("Particle");
                ImGui::TableSetupColumn("Density");
                ImGui::TableSetupColumn("Particles");
                ImGui::TableSetupColumn("Bins");
                ImGui::TableHeadersRow();

                for (const ParticleTypeData& particle : plasmaParticleTemplates)
                {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%s", particle.name.c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%.3e", bulk.densities[particle.name]);
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%d", particle.count);
                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%d", static_cast<int>(particle.iedf.pdf.size()));
                }

                ImGui::EndTable();
            }

            if (ImGui::CollapsingHeader("Species Densities"))
            {
                if (ImGui::BeginTable("SpeciesDensityTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
                {
                    ImGui::TableSetupColumn("Species");
                    ImGui::TableSetupColumn("Charge");
                    ImGui::TableSetupColumn("Density");
                    ImGui::TableHeadersRow();

                    for (const auto& species : Species)
                    {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::Text("%s", species.first.c_str());
                        ImGui::TableSetColumnIndex(1);
                        ImGui::Text("%d", species.second.charge);
                        ImGui::TableSetColumnIndex(2);
                        ImGui::Text("%.3e", bulk.densities[species.first]);
                    }

                    ImGui::EndTable();
                }
            }


            ImGui::End();


            if (!particleDataTypes.empty() && ImPlot::BeginPlot("Histogram")) {

                selectedIEDF = std::clamp(
                    selectedIEDF,
                    0,
                    static_cast<int>(particleDataTypes.size()) - 1
                );

                if (ImGui::BeginCombo(
                    "Select Species",
                    particleDataTypes[selectedIEDF].name.c_str()))
                {
                    for (int i = 0; i < static_cast<int>(particleDataTypes.size()); ++i) {

                        bool isSelected = (selectedIEDF == i);

                        if (ImGui::Selectable(
                            particleDataTypes[i].name.c_str(),
                            isSelected))
                        {
                            selectedIEDF = i;
                        }

                        if (isSelected)
                            ImGui::SetItemDefaultFocus();
                    }

                    ImGui::EndCombo();
                }

                const auto& x = particleDataTypes[selectedIEDF].iedf.energyCenters;
                const auto& y = particleDataTypes[selectedIEDF].iedf.pdf;

                if (!x.empty() && !y.empty())
                {
                    double width = x.size() > 1 ? (x[1] - x[0]) : 1.0;
                    const double xMin = 0.0;
                    const double xMax = std::max<double>(x.back() + width, width);
                    double yMax = 0.0;
                    for (float value : y)
                    {
                        if (std::isfinite(value))
                            yMax = std::max(yMax, static_cast<double>(value));
                    }
                    yMax = std::max(yMax * 1.10, 1e-30);

                    ImPlot::SetupAxes(
                        "Energy (eV)",
                        "Ion Flux (mol m^{-2} s^{-1} eV^{-1})"
                    );
                    ImPlot::SetupAxisLimits(ImAxis_X1, xMin, xMax, ImGuiCond_Once);
                    ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, yMax, ImGuiCond_Once);
                    ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, xMin, xMax);
                    ImPlot::SetupAxisLimitsConstraints(ImAxis_Y1, 0.0, yMax);
                    ImPlot::SetupAxisZoomConstraints(ImAxis_X1, width * 0.25, xMax - xMin);
                    ImPlot::SetupAxisZoomConstraints(ImAxis_Y1, yMax * 1e-6, yMax);

                    ImPlot::PlotBars(
                        "IEDF",
                        x.data(),
                        y.data(),
                        static_cast<int>(x.size()),
                        width
                    );
                }

                ImPlot::EndPlot();
            }
        }



        else if (currentPage == Page::ParticleSetup) {
            ImGui::Begin("Particle Firing");
            ImGui::Text("Global Simulation Duration: %d frames", duration);

            if (!plasmaParticleTemplates.empty())
            {
                selectedTemplateParticle = std::clamp(
                    selectedTemplateParticle,
                    0,
                    static_cast<int>(plasmaParticleTemplates.size()) - 1);

                if (ImGui::BeginCombo(
                    "Plasma Particle",
                    plasmaParticleTemplates[selectedTemplateParticle].name.c_str()))
                {
                    for (int i = 0; i < plasmaParticleTemplates.size(); ++i)
                    {
                        const bool selected = i == selectedTemplateParticle;
                        if (ImGui::Selectable(plasmaParticleTemplates[i].name.c_str(), selected))
                            selectedTemplateParticle = i;
                        if (selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                if (ImGui::Button("Add Plasma Particle"))
                {
                    const std::vector<ParticleTypeData> oldParticles = particleDataTypes;
                    const std::vector<float> oldGrid = gridData;
                    particleDataTypes.push_back(
                        makeFireableFromTemplate(
                            plasmaParticleTemplates[selectedTemplateParticle],
                            duration));
                    typesOfParticles = std::max(1, (int) particleDataTypes.size());
                    rebuildProbabilityGrid(
                        oldParticles,
                        oldGrid,
                        particleDataTypes,
                        typesOfVoxels,
                        gridData);
                }
            }

            if (!spraySpeciesNames.empty())
            {
                selectedSpraySpecies = std::clamp(
                    selectedSpraySpecies,
                    0,
                    static_cast<int>(spraySpeciesNames.size()) - 1);

                if (ImGui::BeginCombo(
                    "Custom Particle Species",
                    spraySpeciesNames[selectedSpraySpecies].c_str()))
                {
                    for (int i = 0; i < spraySpeciesNames.size(); ++i)
                    {
                        const bool selected = i == selectedSpraySpecies;
                        if (ImGui::Selectable(spraySpeciesNames[i].c_str(), selected))
                            selectedSpraySpecies = i;
                        if (selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                if (ImGui::Button("Add Custom Particle"))
                {
                    const std::string& name = spraySpeciesNames[selectedSpraySpecies];
                    const std::vector<ParticleTypeData> oldParticles = particleDataTypes;
                    const std::vector<float> oldGrid = gridData;
                    particleDataTypes.push_back(
                        makeCustomParticle(
                            name,
                            Species[name],
                            duration));
                    typesOfParticles = std::max(1, (int) particleDataTypes.size());
                    rebuildProbabilityGrid(
                        oldParticles,
                        oldGrid,
                        particleDataTypes,
                        typesOfVoxels,
                        gridData);
                }
            }

            ImGui::Separator();

            if (ImGui::BeginTable(
                "FireableParticles",
                10,
                ImGuiTableFlags_Borders |
                ImGuiTableFlags_RowBg |
                ImGuiTableFlags_Resizable |
                ImGuiTableFlags_ScrollX))
            {
                ImGui::TableSetupColumn("Particle Type");
                ImGui::TableSetupColumn("Source");
                ImGui::TableSetupColumn("Custom");
                ImGui::TableSetupColumn("Release Interval");
                ImGui::TableSetupColumn("Count");
                ImGui::TableSetupColumn("Energy");
                ImGui::TableSetupColumn("Angle");
                ImGui::TableSetupColumn("Deposit");
                ImGui::TableSetupColumn("IEDF Bins");
                ImGui::TableSetupColumn("Remove");
                ImGui::TableHeadersRow();

                int removeParticle = -1;
                for (int i = 0; i < particleDataTypes.size(); ++i)
                {
                    ParticleTypeData& particle = particleDataTypes[i];
                    ImGui::PushID(i);
                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%s", particle.name.c_str());

                    ImGui::TableSetColumnIndex(1);
                    if (particle.custom || plasmaParticleTemplates.empty())
                    {
                        ImGui::Text("Mono");
                    }
                    else
                    {
                        int currentTemplate = 0;
                        for (int sourceIndex = 0; sourceIndex < static_cast<int>(plasmaParticleTemplates.size()); ++sourceIndex)
                        {
                            if (plasmaParticleTemplates[sourceIndex].name == particle.name)
                            {
                                currentTemplate = sourceIndex;
                                break;
                            }
                        }

                        if (ImGui::BeginCombo(
                            "##source",
                            plasmaParticleTemplates[currentTemplate].name.c_str()))
                        {
                            for (int sourceIndex = 0; sourceIndex < plasmaParticleTemplates.size(); ++sourceIndex)
                            {
                                const bool selected = sourceIndex == currentTemplate;
                                if (ImGui::Selectable(plasmaParticleTemplates[sourceIndex].name.c_str(), selected))
                                {
                                    const int savedDuration = particle.interval;
                                    particle = makeFireableFromTemplate(
                                        plasmaParticleTemplates[sourceIndex],
                                        savedDuration);
                                }
                                if (selected)
                                    ImGui::SetItemDefaultFocus();
                            }
                            ImGui::EndCombo();
                        }
                    }

                    ImGui::TableSetColumnIndex(2);
                    bool custom = particle.custom;
                    if (ImGui::Checkbox("##custom", &custom))
                    {
                        particle.custom = custom;
                        if (particle.custom)
                        {
                            makeMonoEnergy(particle);
                        }
                        else
                        {
                            const int savedDuration = particle.interval;
                            auto matchingTemplate = std::find_if(
                                plasmaParticleTemplates.begin(),
                                plasmaParticleTemplates.end(),
                                [&](const ParticleTypeData& source)
                                {
                                    return source.name == particle.name;
                                });
                            if (matchingTemplate != plasmaParticleTemplates.end())
                                particle = makeFireableFromTemplate(*matchingTemplate, savedDuration);
                            else
                                particle.custom = true;
                        }
                    }

                    ImGui::TableSetColumnIndex(3);
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    ImGui::InputInt("##duration", &particle.interval);
                    particle.interval = std::clamp(particle.interval, 1, 1000000);

                    ImGui::TableSetColumnIndex(4);
                    if (particle.custom)
                    {
                        ImGui::SetNextItemWidth(-FLT_MIN);
                        ImGui::InputInt("##count", &particle.count);
                        particle.count = std::max(particle.count, 1);
                    }
                    else
                    {
                        ImGui::Text("%d", particle.count);
                    }

                    ImGui::TableSetColumnIndex(5);
                    if (particle.custom)
                    {
                        ImGui::SetNextItemWidth(-FLT_MIN);
                        if (ImGui::InputFloat("##energy", &particle.energy))
                            makeMonoEnergy(particle);
                    }
                    else
                    {
                        ImGui::Text("%.3f", particle.energy);
                    }

                    ImGui::TableSetColumnIndex(6);
                    if (particle.custom)
                    {
                        ImGui::SetNextItemWidth(-FLT_MIN);
                        ImGui::InputFloat("##angle", &particle.halfAngle);
                        particle.halfAngle = std::clamp(particle.halfAngle, 0.0f, 89.0f);
                    }
                    else
                    {
                        ImGui::Text("%.1f", particle.halfAngle);
                    }

                    ImGui::TableSetColumnIndex(7);
                    if (particle.custom)
                    {
                        ImGui::Checkbox("##deposit", &particle.deposit);
                    }
                    else
                    {
                        ImGui::Text("%s", particle.deposit ? "Yes" : "No");
                    }

                    ImGui::TableSetColumnIndex(8);
                    ImGui::Text("%d", static_cast<int>(particle.iedf.pdf.size()));

                    ImGui::TableSetColumnIndex(9);
                    if (ImGui::Button("Remove"))
                        removeParticle = i;

                    ImGui::PopID();
                }

                if (removeParticle >= 0)
                {
                    const std::vector<ParticleTypeData> oldParticles = particleDataTypes;
                    const std::vector<float> oldGrid = gridData;
                    particleDataTypes.erase(particleDataTypes.begin() + removeParticle);
                    typesOfParticles = std::max(1, static_cast<int>(particleDataTypes.size()));
                    rebuildProbabilityGrid(
                        oldParticles,
                        oldGrid,
                        particleDataTypes,
                        typesOfVoxels,
                        gridData);
                }

                ImGui::EndTable();
            }

            ImGui::End();

            ImGui::Begin("Surface Probabilities");
            static int selectedProbabilityKind = 0;
            const char* probabilityKinds[] = { "Reaction", "Deposit", "Adsorb" };

            if (ImGui::BeginTabBar("ProbabilityKindTabs"))
            {
                for (int kind = 0; kind < 3; ++kind)
                {
                    if (ImGui::BeginTabItem(probabilityKinds[kind]))
                    {
                        selectedProbabilityKind = kind;
                        const int columns = 1 + static_cast<int>(particleDataTypes.size());
                        if (ImGui::BeginTable(
                            "SurfaceProbabilityGrid",
                            columns,
                            ImGuiTableFlags_Borders |
                            ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_Resizable |
                            ImGuiTableFlags_ScrollX))
                        {
                            ImGui::TableSetupColumn("Voxel Type");
                            for (const ParticleTypeData& particle : particleDataTypes)
                                ImGui::TableSetupColumn(particle.name.c_str());
                            ImGui::TableHeadersRow();

                            for (int voxel = 0; voxel < typesOfVoxels; ++voxel)
                            {
                                ImGui::TableNextRow();
                                ImGui::TableSetColumnIndex(0);
                                ImGui::Text("Voxel %d", voxel);

                                for (int particle = 0; particle < static_cast<int>(particleDataTypes.size()); ++particle)
                                {
                                    ImGui::TableSetColumnIndex(particle + 1);
                                    float& probability =
                                        gridData[
                                            probabilityIndex(
                                                particle,
                                                voxel,
                                                selectedProbabilityKind,
                                                typesOfParticles,
                                                typesOfVoxels)];
                                    ImGui::PushID(voxel * 100000 + particle);
                                    ImGui::SetNextItemWidth(90.0f);
                                    ImGui::InputFloat("##prob", &probability, 0.01f, 0.1f, "%.3f");
                                    probability = std::clamp(probability, 0.0f, 1.0f);
                                    ImGui::PopID();
                                }
                            }

                            ImGui::EndTable();
                        }
                        ImGui::EndTabItem();
                    }
                }
                ImGui::EndTabBar();
            }

            ImGui::End();
        }


        // ========================= GRID FILE WINDOW ========================= //
        else if(currentPage == Page::RenderPage){
            ImGui::Begin("Grid Save/Load");
            ImGui::SliderInt("Duration", &duration, 0, 50000);
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
                forceMeshBuild = false;
            }

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
                    size_t particleCount = particleDataTypes.size();

                    const uint32_t serializedParticleCount =
                        static_cast<uint32_t>(particleCount);

                    writeValue(out, PARTICLE_DATA_MAGIC);
                    writeValue(out, PARTICLE_DATA_VERSION);
                    writeValue(out, serializedParticleCount);

                    for (const ParticleTypeData& particle : particleDataTypes)
                    {
                        if (!writeParticleType(out, particle))
                            break;
                    }

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
                        uint32_t particleMagic = 0;
                        uint32_t particleVersion = 0;
                        uint32_t particleCount = 0;

                        bool validParticleData =
                            readValue(in, particleMagic) &&
                            readValue(in, particleVersion) &&
                            readValue(in, particleCount) &&
                            particleMagic == PARTICLE_DATA_MAGIC &&
                            particleVersion >= 1 &&
                            particleVersion <= PARTICLE_DATA_VERSION &&
                            particleCount <= MAX_SERIALIZED_ITEMS;

                        if (validParticleData)
                        {
                        particleDataTypes.resize(particleCount);
                        for (ParticleTypeData& particle : particleDataTypes)
                        {
                            if (!readParticleType(in, particle, particleVersion))
                            {
                                validParticleData = false;
                                break;
                                }
                            }
                        }

                        size_t gridSize = 0;
                        if (validParticleData)
                        {
                            in.read((char*)&gridSize, sizeof(gridSize));
                            validParticleData =
                                static_cast<bool>(in) &&
                                gridSize <= MAX_SERIALIZED_ITEMS;
                        }

                        if (validParticleData)
                        {
                            gridData.resize(gridSize);
                            in.read(
                                (char*)gridData.data(),
                                gridSize * sizeof(float)
                            );
                            validParticleData = static_cast<bool>(in);
                        }

                        if (!validParticleData)
                        {
                            std::cout
                                << "Grid file uses an unsupported or corrupt "
                                "particle-data format.\n";
                            in.close();
                        }
                        else
                        {
                            in.close();

                            simulation.uploadVoxels(
                                simulation.grid.voxels
                            );

                            mesh.initGPU();
                            mesh.setVoxelBuffer(simulation.voxelSSBO);
                            mesh.buildMesh(rayOrigin, viewMatrix, sliceDir, sliceIndex);
                            forceMeshBuild = false;

                            std::cout << "Loaded grid + settings from: "
                                << gridFilename << std::endl;
                        }
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
                        for (int i = 0;i < particleDataTypes.size();i++)
                        {
                            ParticleTypeData p = particleDataTypes[i];
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
                const bool cameraChanged =
                    std::abs(yaw - lastBuildYaw) > 0.0001f ||
                    std::abs(pitch - lastBuildPitch) > 0.0001f ||
                    std::abs(D - lastBuildDistance) > 0.0001f;

                if (forceMeshBuild || cameraChanged || (!pause && frame % 10 == 0)) {
                    mesh.buildMesh(rayOrigin, viewMatrix, sliceDir, sliceIndex);
                    lastBuildYaw = yaw;
                    lastBuildPitch = pitch;
                    lastBuildDistance = D;
                    forceMeshBuild = false;
                }
                mesh.draw();
            }
        
            ImGui::Begin("Information");

            ImGui::Separator();

            ImGui::Text("Simulation");

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
        }
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

    renderMesh(stackSimulation());

    return 0;
}
