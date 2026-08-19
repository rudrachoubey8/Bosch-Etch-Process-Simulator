#define NOMINMAX
#include "AppSupport.h"

#include <windows.h>
#include <wincodec.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <numeric>

#include "implot.h"
#include "settings.h"

namespace
{
    constexpr uint32_t PARTICLE_DATA_MAGIC = 0x42504550;
    constexpr uint32_t PARTICLE_DATA_VERSION = 4;
    constexpr uint32_t MATERIAL_DATA_MAGIC = 0x42504D54;
    constexpr uint32_t MATERIAL_DATA_VERSION = 1;
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

    bool writeMaterialInfo(std::ostream& out, const VoxelMaterialInfo& material)
    {
        return
            writeValue(out, material.type) &&
            writeString(out, material.name) &&
            writeValue(out, material.r) &&
            writeValue(out, material.g) &&
            writeValue(out, material.b) &&
            writeValue(out, material.threshold) &&
            writeValue(out, material.depositThreshold) &&
            writeValue(out, material.solid);
    }

    bool readMaterialInfo(std::istream& in, VoxelMaterialInfo& material, uint32_t version)
    {
        if (version != 1)
            return false;

        return
            readValue(in, material.type) &&
            readString(in, material.name) &&
            readValue(in, material.r) &&
            readValue(in, material.g) &&
            readValue(in, material.b) &&
            readValue(in, material.threshold) &&
            readValue(in, material.depositThreshold) &&
            readValue(in, material.solid);
    }

    std::string lowercaseExtension(const char* path)
    {
        std::string value(path ? path : "");
        const std::size_t dot = value.find_last_of('.');
        if (dot == std::string::npos)
            return ".png";

        std::string ext = value.substr(dot);
        for (char& ch : ext)
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        return ext;
    }

    std::wstring widenPath(const char* path)
    {
        if (!path || !*path)
            return L"slice.png";

        const int size = MultiByteToWideChar(CP_UTF8, 0, path, -1, nullptr, 0);
        if (size <= 0)
            return L"slice.png";

        std::wstring wide(static_cast<std::size_t>(size - 1), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, path, -1, wide.data(), size);
        return wide;
    }
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
        writeValue(out, particle.releaseDuration) &&
        writeValue(out, particle.depositVoxelType) &&
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
        (version >= 3 && !readValue(in, particle.releaseDuration)) ||
        (version >= 4 && !readValue(in, particle.depositVoxelType)) ||
        !readString(in, particle.name) ||
        !readFloatVector(in, particle.iedf.energyCenters) ||
        !readFloatVector(in, particle.iedf.pdf))
        return false;

    if (version < 3)
        particle.releaseDuration = particle.interval;
    if (version < 4)
        particle.depositVoxelType = 3;

    particle.deposit = deposit != 0;
    particle.draw = draw != 0;
    particle.custom = custom != 0;
    return particle.iedf.energyCenters.size() == particle.iedf.pdf.size();
}

void writeMaterialData(std::ostream& out)
{
    const std::vector<VoxelMaterialInfo>& materials = stackSimulationMaterials();
    writeValue(out, MATERIAL_DATA_MAGIC);
    writeValue(out, MATERIAL_DATA_VERSION);
    writeValue(out, static_cast<uint32_t>(materials.size()));

    for (const VoxelMaterialInfo& material : materials)
        writeMaterialInfo(out, material);
}

void tryReadMaterialData(std::istream& in)
{
    uint32_t materialMagic = 0;
    uint32_t materialVersion = 0;
    uint32_t materialCount = 0;

    if (!readValue(in, materialMagic))
    {
        in.clear();
        return;
    }

    if (materialMagic != MATERIAL_DATA_MAGIC ||
        !readValue(in, materialVersion) ||
        !readValue(in, materialCount) ||
        materialVersion != MATERIAL_DATA_VERSION ||
        materialCount > MAX_SERIALIZED_ITEMS)
    {
        in.clear();
        return;
    }

    std::vector<VoxelMaterialInfo> loadedMaterials(materialCount);
    for (VoxelMaterialInfo& material : loadedMaterials)
    {
        if (!readMaterialInfo(in, material, materialVersion))
        {
            in.clear();
            return;
        }
    }

    stackSimulationMaterials() = std::move(loadedMaterials);
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
    particle.depositVoxelType = 3;
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
    int interval,
    int releaseDuration)
{
    ParticleTypeData particle = source;
    particle.custom = false;
    particle.interval = std::max(interval, 1);
    particle.releaseDuration = std::max(releaseDuration, 1);
    return particle;
}

ParticleTypeData makeCustomParticle(
    const std::string& name,
    const SpeciesProperties& properties,
    int releaseDuration)
{
    ParticleTypeData particle = makeSprayParticle(name, properties, 1e18);
    particle.custom = true;
    particle.releaseDuration = std::max(releaseDuration, 1);
    makeMonoEnergy(particle);
    return particle;
}

const VoxelMaterialInfo* findStackMaterial(int type)
{
    const auto& materials = stackSimulationMaterials();
    const auto it = std::find_if(
        materials.begin(),
        materials.end(),
        [type](const VoxelMaterialInfo& material)
        {
            return material.type == type;
        });

    return it == materials.end() ? nullptr : &(*it);
}

ImVec4 materialColor(const VoxelMaterialInfo& material)
{
    return ImVec4(
        material.r / 255.0f,
        material.g / 255.0f,
        material.b / 255.0f,
        1.0f);
}

const VoxelMaterialInfo* materialByListIndex(int index)
{
    const auto& materials = stackSimulationMaterials();
    if (index < 0 || index >= static_cast<int>(materials.size()))
        return nullptr;

    return &materials[index];
}

int materialListIndexForType(int type)
{
    const auto& materials = stackSimulationMaterials();
    for (int i = 0; i < static_cast<int>(materials.size()); ++i)
    {
        if (materials[i].type == type)
            return i;
    }

    return materials.empty() ? -1 : 0;
}

void applyMaterialPropertiesToVoxels(std::vector<Voxel>& voxels)
{
    for (Voxel& voxel : voxels)
    {
        if (voxel.solid == 0)
            continue;

        const VoxelMaterialInfo* material = findStackMaterial(voxel.type);
        if (!material)
            continue;

        voxel.solid = material->solid;
        voxel.threshold = material->threshold;
        voxel.depositThreshold = material->depositThreshold;
    }
}

const ParticleTypeData* findParticleByName(
    const std::vector<ParticleTypeData>& particles,
    const std::string& name)
{
    const auto it = std::find_if(
        particles.begin(),
        particles.end(),
        [&](const ParticleTypeData& particle)
        {
            return particle.name == name;
        });

    return it == particles.end() ? nullptr : &(*it);
}

std::vector<ParticleTypeData> runPlasmaCase(
    const BulkModel& controls,
    double absorbedPower,
    double biasPower)
{
    BulkModel trial;
    initializeDefaultBulk(
        trial,
        controls.gasTemp,
        controls.pressureMtorr);

    trial.dt = controls.dt;
    trial.duration = controls.duration;
    trial.Pabs = absorbedPower;
    trial.pump = controls.pump;
    trial.biasPower = biasPower;
    trial.biasFrequency = controls.biasFrequency;
    trial.biasVoltageGuess = controls.biasVoltageGuess;
    trial.residualVoltageRipple = controls.residualVoltageRipple;
    trial.sheathPoints = controls.sheathPoints;
    trial.sheathIterations = controls.sheathIterations;
    trial.ionCount = controls.ionCount;
    trial.ionDt = controls.ionDt;
    trial.maxCycles = controls.maxCycles;
    trial.enableMomentumTransfer = controls.enableMomentumTransfer;
    trial.enableChargeExchange = controls.enableChargeExchange;
    trial.momentumTransferScale = controls.momentumTransferScale;
    trial.chargeExchangeScale = controls.chargeExchangeScale;
    trial.useBias = controls.useBias;

    advanceModelForDuration(trial);

    Sheath trialSheath;
    initializeSheath(trial, trialSheath);
    return generateParticles(trial, trialSheath);
}

void buildIedfSweep(
    const BulkModel& controls,
    const std::string& speciesName,
    const std::vector<double>& values,
    bool varyBias,
    std::vector<IedfSweepCurve>& curves)
{
    curves.clear();
    for (double value : values)
    {
        std::vector<ParticleTypeData> particles = runPlasmaCase(
            controls,
            varyBias ? controls.Pabs : value,
            varyBias ? value : controls.biasPower);

        const ParticleTypeData* particle = findParticleByName(
            particles,
            speciesName);

        if (!particle || particle->iedf.energyCenters.empty())
            continue;

        IedfSweepCurve curve;
        curve.label =
            std::to_string(static_cast<int>(std::round(value))) +
            (varyBias ? " W bias" : " W power");
        curve.distribution = particle->iedf;
        curves.push_back(std::move(curve));
    }
}

void plotIedfCurves(
    const char* title,
    const std::vector<IedfSweepCurve>& curves)
{
    if (curves.empty())
        return;

    if (!ImPlot::BeginPlot(title))
        return;

    double xMax = 1.0;
    double yMax = 1e-30;
    double yMin = 1e30;
    for (const IedfSweepCurve& curve : curves)
    {
        if (!curve.distribution.energyCenters.empty())
            xMax = std::max<double>(
                xMax,
                curve.distribution.energyCenters.back());

        for (float value : curve.distribution.pdf)
        {
            if (std::isfinite(value))
            {
                yMax = std::max<double>(yMax, value);
                if (value > 0.0f)
                    yMin = std::min<double>(yMin, value);
            }
        }
    }
    if (!std::isfinite(yMin) || yMin >= yMax)
        yMin = yMax * 1e-6;

    ImPlot::SetupAxes(
        "Energy (eV)",
        "Ion Flux (mol m^{-2} s^{-1} eV^{-1})");
    ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Log10);
    ImPlot::SetupAxisLimits(ImAxis_X1, 0.0, xMax, ImGuiCond_Once);
    ImPlot::SetupAxisLimits(ImAxis_Y1, yMin, yMax * 1.10, ImGuiCond_Once);

    for (const IedfSweepCurve& curve : curves)
    {
        const int count = static_cast<int>(
            std::min(
                curve.distribution.energyCenters.size(),
                curve.distribution.pdf.size()));
        if (count <= 0)
            continue;

        std::vector<float> displayPdf(
            curve.distribution.pdf.begin(),
            curve.distribution.pdf.begin() + count);
        for (float& value : displayPdf)
        {
            if (!std::isfinite(value) || value <= 0.0f)
                value = static_cast<float>(yMin);
        }

        ImPlot::PlotLine(
            curve.label.c_str(),
            curve.distribution.energyCenters.data(),
            displayPdf.data(),
            count);
    }

    ImPlot::EndPlot();
}

uint32_t rgbaPixel(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    return
        static_cast<uint32_t>(r) |
        (static_cast<uint32_t>(g) << 8) |
        (static_cast<uint32_t>(b) << 16) |
        (static_cast<uint32_t>(a) << 24);
}

uint32_t materialPixel(int type)
{
    const VoxelMaterialInfo* material = findStackMaterial(type);
    if (!material)
        return rgbaPixel(255, 255, 255);

    return rgbaPixel(material->r, material->g, material->b);
}

bool writeIedfCsv(const char* path, const ParticleTypeData& particle)
{
    std::ofstream out(path);
    if (!out)
        return false;

    out << "energy_eV,ion_flux_mol_m-2_s-1_eV-1\n";
    const std::size_t count = std::min(
        particle.iedf.energyCenters.size(),
        particle.iedf.pdf.size());
    for (std::size_t i = 0; i < count; ++i)
        out << particle.iedf.energyCenters[i] << ',' << particle.iedf.pdf[i] << '\n';

    return true;
}

bool writeSliceCsv(
    const char* path,
    int width,
    int height,
    const std::vector<int>& slice)
{
    if (width <= 0 || height <= 0 || slice.size() < static_cast<std::size_t>(width * height))
        return false;

    std::ofstream out(path);
    if (!out)
        return false;

    out << "x,y,voxel_type,material\n";
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const int type = slice[x + y * width];
            const VoxelMaterialInfo* material = findStackMaterial(type);
            out << x << ',' << y << ',' << type << ','
                << (material ? material->name : "Void") << '\n';
        }
    }

    return true;
}

bool writeSliceImage(
    const char* path,
    int width,
    int height,
    const std::vector<uint32_t>& pixels)
{
    if (width <= 0 || height <= 0 || pixels.size() < static_cast<std::size_t>(width * height))
        return false;

    const std::string ext = lowercaseExtension(path);
    const GUID containerFormat =
        (ext == ".jpg" || ext == ".jpeg") ? GUID_ContainerFormatJpeg :
        (ext == ".png") ? GUID_ContainerFormatPng :
        GUID_NULL;
    if (IsEqualGUID(containerFormat, GUID_NULL))
        return false;

    std::vector<uint8_t> bgr(static_cast<std::size_t>(width * height * 3));
    for (int i = 0; i < width * height; ++i)
    {
        const uint32_t pixel = pixels[i];
        bgr[static_cast<std::size_t>(i * 3 + 0)] = static_cast<uint8_t>((pixel >> 16) & 0xFF);
        bgr[static_cast<std::size_t>(i * 3 + 1)] = static_cast<uint8_t>((pixel >> 8) & 0xFF);
        bgr[static_cast<std::size_t>(i * 3 + 2)] = static_cast<uint8_t>(pixel & 0xFF);
    }

    const HRESULT initHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool uninitCom = SUCCEEDED(initHr);
    if (FAILED(initHr) && initHr != RPC_E_CHANGED_MODE)
        return false;

    IWICImagingFactory* factory = nullptr;
    IWICBitmapEncoder* encoder = nullptr;
    IWICBitmapFrameEncode* frame = nullptr;
    IWICStream* stream = nullptr;
    IPropertyBag2* options = nullptr;
    bool ok = false;

    HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory));

    if (SUCCEEDED(hr))
        hr = factory->CreateStream(&stream);
    const std::wstring widePath = widenPath(path);
    if (SUCCEEDED(hr))
        hr = stream->InitializeFromFilename(widePath.c_str(), GENERIC_WRITE);
    if (SUCCEEDED(hr))
        hr = factory->CreateEncoder(containerFormat, nullptr, &encoder);
    if (SUCCEEDED(hr))
        hr = encoder->Initialize(stream, WICBitmapEncoderNoCache);
    if (SUCCEEDED(hr))
        hr = encoder->CreateNewFrame(&frame, &options);
    if (SUCCEEDED(hr))
        hr = frame->Initialize(options);
    if (SUCCEEDED(hr))
        hr = frame->SetSize(static_cast<UINT>(width), static_cast<UINT>(height));
    WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
    if (SUCCEEDED(hr))
        hr = frame->SetPixelFormat(&format);
    if (SUCCEEDED(hr) && !IsEqualGUID(format, GUID_WICPixelFormat24bppBGR))
        hr = E_FAIL;
    if (SUCCEEDED(hr))
        hr = frame->WritePixels(
            static_cast<UINT>(height),
            static_cast<UINT>(width * 3),
            static_cast<UINT>(bgr.size()),
            bgr.data());
    if (SUCCEEDED(hr))
        hr = frame->Commit();
    if (SUCCEEDED(hr))
        hr = encoder->Commit();

    ok = SUCCEEDED(hr);

    if (options)
        options->Release();
    if (frame)
        frame->Release();
    if (encoder)
        encoder->Release();
    if (stream)
        stream->Release();
    if (factory)
        factory->Release();
    if (uninitCom)
        CoUninitialize();

    return ok;
}

int maxSliceIndexForDirection(int sliceDir)
{
    if (sliceDir == 0)
        return Settings::Z - 1;
    if (sliceDir == 1)
        return Settings::Y - 1;
    return Settings::X - 1;
}
