#include "ChemicalReactions.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <numeric>

namespace
{
    constexpr double AVOGADRO = 6.02214076e23;
    constexpr double SCCM_TO_PARTICLES_PER_SECOND = 4.48e17;

    const std::vector<std::string> PUMP_SPECIES = {
        "C4F8",
        "Ar",
        "Ar*",
        "F-"
    };

    double clampValue(double value, double lo, double hi)
    {
        return std::max(lo, std::min(value, hi));
    }

    double bohmSpeed(double Te, double mass)
    {
        return std::sqrt(E_CHARGE * std::max(Te, 0.05) / std::max(mass, 1e-31));
    }

    double electronThermalSpeed(double Te)
    {
        return std::sqrt(8.0 * E_CHARGE * std::max(Te, 0.05) / (PI * E_MASS));
    }

    double gasThermalSpeed(double mass, double gasTemp)
    {
        return std::sqrt(8.0 * K_B * gasTemp / (PI * std::max(mass, 1e-31)));
    }

    bool isPumpedSpecies(const std::string& name)
    {
        return std::find(PUMP_SPECIES.begin(), PUMP_SPECIES.end(), name) != PUMP_SPECIES.end();
    }

    double getDensity(const BulkModel& bulk, const std::string& name)
    {
        const auto it = bulk.densities.find(name);
        return it == bulk.densities.end() ? 0.0 : it->second;
    }

    double speciesDensityForRate(const BulkModel& bulk, const std::string& name)
    {
        const auto propIt = Species.find(name);
        const double n = getDensity(bulk, name);
        if (propIt == Species.end())
            return std::max(n, 0.0);
        if (propIt->second.charge == 0)
            return std::max(n, 1.0);
        return std::max(n, 0.0);
    }

    double effectiveIonMassDensityWeighted(const BulkModel& bulk)
    {
        double sumNi = 0.0;
        double sumNiMi = 0.0;

        for (const auto& p : Species)
        {
            if (p.second.charge <= 0)
                continue;

            const double ni = std::max(getDensity(bulk, p.first), 0.0);
            sumNi += ni;
            sumNiMi += ni * p.second.mass * AMU;
        }

        if (sumNi <= 0.0)
            return 40.0 * AMU;

        return std::max(sumNiMi / sumNi, AMU);
    }

    double totalIonCurrentDensity(const BulkModel& bulk, double Te)
    {
        double Ji = 0.0;
        for (const auto& p : Species)
        {
            if (p.second.charge <= 0)
                continue;

            const double ni = std::max(getDensity(bulk, p.first), 0.0);
            const double mass = p.second.mass * AMU;
            Ji += p.second.charge * E_CHARGE * ni * bohmSpeed(Te, mass);
        }
        return Ji;
    }

    double totalNeutralDensity(const BulkModel& bulk)
    {
        double neutralDensity = 0.0;
        for (const auto& p : Species)
        {
            if (p.second.charge == 0)
                neutralDensity += std::max(getDensity(bulk, p.first), 0.0);
        }
        return neutralDensity;
    }

    double periodicLinearInterp(
        const std::vector<double>& t,
        const std::vector<double>& y,
        double query)
    {
        if (t.empty() || y.empty())
            return 0.0;
        if (t.size() == 1 || y.size() == 1)
            return y.front();

        const double dt = t[1] - t[0];
        const double period = t.back() + dt;
        double tau = std::fmod(query, period);
        if (tau < 0.0)
            tau += period;

        const auto upper = std::upper_bound(t.begin(), t.end(), tau);
        if (upper == t.begin())
            return y.front();

        const std::size_t i0 = static_cast<std::size_t>((upper - t.begin()) - 1);
        const std::size_t i1 = (i0 + 1) % y.size();
        const double t0 = t[i0];
        const double t1 = (i1 == 0) ? period : t[i1];
        const double f = (tau - t0) / std::max(t1 - t0, 1e-30);
        return y[i0] + f * (y[i1] - y[i0]);
    }

    double sheathCircuitRhs(
        double tau,
        double V,
        const std::vector<double>& t,
        const std::vector<double>& ds,
        double Te,
        double ne,
        double IiTotal,
        double Imax,
        const BulkModel& bulk)
    {
        const double expo = clampValue(V / std::max(Te, 0.05), -50.0, 50.0);
        const double Ie =
            (E_CHARGE * electronThermalSpeed(Te) * ne * bulk.substrateArea / 4.0) *
            std::exp(expo);
        const double Irf =
            bulk.useBias ? Imax * std::sin(2.0 * PI * bulk.biasFrequency * tau) : 0.0;
        const double dNow = std::max(periodicLinearInterp(t, ds, tau), 1e-8);
        const double Cs = std::max(EPS0 * bulk.substrateArea / dNow, 1e-15);


        return (IiTotal - Ie - Irf) / Cs;
    }

    std::vector<double> smoothGaussian(const std::vector<double>& values, double sigma)
    {
        if (values.empty() || sigma <= 0.0)
            return values;

        const int radius = static_cast<int>(std::ceil(4.0 * sigma));
        std::vector<double> kernel(2 * radius + 1);
        double norm = 0.0;

        for (int i = -radius; i <= radius; ++i)
        {
            const double w = std::exp(-0.5 * (i * i) / (sigma * sigma));
            kernel[i + radius] = w;
            norm += w;
        }

        for (double& w : kernel)
            w /= norm;

        std::vector<double> out(values.size(), 0.0);
        for (std::size_t i = 0; i < values.size(); ++i)
        {
            double acc = 0.0;
            for (int k = -radius; k <= radius; ++k)
            {
                const int idx = std::clamp<int>(
                    static_cast<int>(i) + k,
                    0,
                    static_cast<int>(values.size()) - 1);
                acc += values[idx] * kernel[k + radius];
            }
            out[i] = acc;
        }
        return out;
    }
}

double sigmaCX(double E)
{
    E = std::max(E, 0.1);
    return 2.5e-19 * (1.0 + 0.15 * std::exp(-((E - 60.0) * (E - 60.0)) / 3000.0));
}

double sigmaMT(double E)
{
    E = std::max(E, 0.1);
    return 1.4e-19 * (1.0 + 0.10 * std::exp(-((E - 30.0) * (E - 30.0)) / 2000.0));
}

double electricField(double V, double x, double d)
{
    if (d <= 1e-12)
        return 0.0;

    const double xi = clampValue(x / d, 0.0, 1.0);
    return -(1.5 * V / d) * std::sqrt(xi);
}

void initializeDefaultBulk(BulkModel& bulk, double gasTemp)
{
    bulk = BulkModel{};

    bulk.dt = 1e-9;
    bulk.duration = 1e-3;
    bulk.Te0 = 3.0;

    const double reactorRadius = 0.1;
    const double reactorLength = 0.03;
    bulk.Volume = PI * reactorRadius * reactorRadius * reactorLength;
    bulk.Area = 2.0 * PI * reactorRadius * reactorLength +
        2.0 * PI * reactorRadius * reactorRadius;
    bulk.substrateArea = 0.01;
    bulk.pressureMtorr = 10.0;
    bulk.gasTemp = gasTemp;
    bulk.Pabs = 700.0 * 0.3;
    bulk.biasPower = 200.0;
    bulk.biasFrequency = 2.0e6;
    bulk.biasVoltageGuess = 100.0;
    bulk.residualVoltageRipple = 20.0;
    bulk.sheathPoints = 240;
    bulk.sheathIterations = 20;
    bulk.ionCount = 5000;
    bulk.ionDt = 1e-9;
    bulk.maxCycles = 20;
    bulk.enableChargeExchange = true;
    bulk.enableMomentumTransfer = true;
    bulk.chargeExchangeScale = 0.1;
    bulk.momentumTransferScale = 1.0;
    bulk.useBias = true;
    bulk.motherNeutralFlowSccm = {
        {"Ar", 40.0},
        {"C4F8", 200.0}
    };

    const double pressurePa = bulk.pressureMtorr * 0.133322;
    const double totalFlowSccm = 240.0;
    const double totalGasDensity = pressurePa / (K_B * bulk.gasTemp);
    for (const auto& species : Species)
        bulk.densities[species.first] = species.second.charge == 0 ? 1.0 : 0.0;

    bulk.densities["Ar"] = totalGasDensity * (40.0 / totalFlowSccm);
    bulk.densities["C4F8"] = totalGasDensity * (200.0 / totalFlowSccm);
    bulk.densities["CF3+"] = 2.0e18;
    bulk.densities["CF2+"] = 2.0e18;
    bulk.densities["Ar+"] = 2.0e18;
    bulk.densities["e-"] = 2.0e18;
    bulk.Ngas = totalGasDensity;

    const double ndotTotal = totalFlowSccm * SCCM_TO_PARTICLES_PER_SECOND;
    bulk.pump = (ndotTotal * K_B * bulk.gasTemp / pressurePa) /
        std::max(bulk.Volume, 1e-30);

    bulk.reactions =
    {
        {
            "r11_Ar",
            11.6,
            6.033e-15, 0.3287, 12.08,
            {{"Ar",1},{"e-",1}},
            {{"Ar*",1},{"e-",1}}
        },
        {
            "r12_Ar",
            15.76,
            2.160e-14, 0.6329, 16.0627,
            {{"Ar",1},{"e-",1}},
            {{"Ar+",1},{"e-",2}}
        },
        {
            "r13_Ar",
            4.43,
            1.698e-13, 0.1072, 4.4129,
            {{"Ar*",1},{"e-",1}},
            {{"Ar+",1},{"e-",2}}
        },
        {
            "r14_Ar",
            -11.6,
            3.969e-15, 0.2894, 0.7412,
            {{"Ar*",1},{"e-",1}},
            {{"Ar",1},{"e-",1}}
        },
        {
            "r15_Ar",
            0.0,
            1.20e-15, 0.0, 0.0,
            {{"Ar*",2}},
            {{"Ar+",1},{"Ar",1},{"e-",1}}
        },
        {
            "r2_CF4",
            0.15,
            3.26e-14, -0.317, 0.230,
            {{"CF4",1},{"e-",1}},
            {{"CF4*",1},{"e-",1}}
        },
        {
            "r2_C4F8",
            17.0,
            5.70e-14, 0.470, 17.480,
            {{"C4F8",1},{"e-",1}},
            {{"C2F4+",1},{"C2F4",1},{"e-",2}}
        },
        {
            "r3_C4F8",
            2.42,
            9.58e-14, 0.042, 8.572,
            {{"C4F8",1},{"e-",1}},
            {{"C2F4",2},{"e-",1}}
        },
        {
            "r4_C2F4",
            3.06,
            1.32e-15, 0.412, 6.329,
            {{"C2F4",1},{"e-",1}},
            {{"CF2",2},{"e-",1}}
        },
        {
            "r5_CF3",
            10.0,
            1.36e-15, 0.796, 9.057,
            {{"CF3",1},{"e-",1}},
            {{"CF3+",1},{"e-",2}}
        },
        {
            "r6_CF3",
            9.0,
            1.0e-16, 0.0, 0.0,
            {{"CF3",1},{"e-",1}},
            {{"CF2",1},{"F-",1}}
        },
        {
            "r7_CF2",
            10.0,
            1.10e-14, 0.393, 11.370,
            {{"CF2",1},{"e-",1}},
            {{"CF2+",1},{"e-",2}}
        },
        {
            "r8_Fm",
            13.0,
            6.27e-14, 0.193, 12.918,
            {{"F-",1},{"e-",1}},
            {{"F",1},{"e-",2}}
        },
        {
            "r9_CF2F",
            0.0,
            1.40e-20, 0.0, 0.0,
            {{"CF2",1},{"F",1}},
            {{"CF3",1}}
        },
        {
            "r10_CF3F",
            0.0,
            2.32e-18, 0.0, 0.0,
            {{"CF3",1},{"F",1}},
            {{"CF4",1}}
        }
    };
}

void advanceModel(BulkModel& bulk)
{
    std::unordered_map<std::string, double> densitiesRate;
    for (const auto& p : Species)
    {
        densitiesRate[p.first] = 0.0;
        (void)bulk.densities[p.first];
    }

    double Pinel = 0.0;
    for (const Reaction& rxn : bulk.reactions)
    {
        const double Te = std::max(bulk.Te0, 0.05);
        const double k = rxn.a * std::pow(Te, rxn.b) * std::exp(-rxn.c / Te);

        double R = k;
        for (const auto& p : rxn.reactants)
            R *= std::pow(speciesDensityForRate(bulk, p.first), p.second);

        if (!std::isfinite(R))
            continue;

        bool electronRxn = false;
        for (const auto& p : rxn.reactants)
        {
            if (p.first == "e-")
                electronRxn = true;
            densitiesRate[p.first] -= R * p.second;
        }

        if (electronRxn)
            Pinel += R * rxn.energy;

        for (const auto& p : rxn.products)
            densitiesRate[p.first] += R * p.second;
    }

    for (const auto& flow : bulk.motherNeutralFlowSccm)
        densitiesRate[flow.first] += flow.second * SCCM_TO_PARTICLES_PER_SECOND /
            std::max(bulk.Volume, 1e-30);

    for (const auto& p : Species)
    {
        densitiesRate[p.first] += bulk.inPump[p.first];

        if (isPumpedSpecies(p.first))
            densitiesRate[p.first] -= bulk.pump * speciesDensityForRate(bulk, p.first);

        if (p.second.charge > 0)
        {
            const double mass = p.second.mass * AMU;
            const double uB = bohmSpeed(bulk.Te0, mass);
            densitiesRate[p.first] -= std::max(getDensity(bulk, p.first), 0.0) *
                uB * bulk.Area / std::max(bulk.Volume, 1e-30);
        }
    }

    const double mRefIon = 50.0 * AMU;
    const double ne = std::max(getDensity(bulk, "e-"), 1.0);
    const double uBe = bohmSpeed(bulk.Te0, mRefIon);
    densitiesRate["e-"] -= ne * uBe * bulk.Area / std::max(bulk.Volume, 1e-30);

    double gammaI = 0.0;
    for (const auto& p : Species)
    {
        if (p.second.charge > 0)
        {
            const double mass = p.second.mass * AMU;
            gammaI += std::max(getDensity(bulk, p.first), 0.0) *
                bohmSpeed(bulk.Te0, mass);
        }
    }

    const double VsCoeff = std::log(std::sqrt(std::max(mRefIon / (2.0 * PI * E_MASS), 1.0)));
    const double VsEv = bulk.Te0 * VsCoeff;
    const double Qsheath = gammaI * (bulk.Area / std::max(bulk.Volume, 1e-30)) *
        (VsEv + 2.5 * bulk.Te0);
    const double powerIn = bulk.Pabs / (std::max(bulk.Volume, 1e-30) * E_CHARGE);
    const double neSafe = std::max(ne, 1e10);
    const double dTeDt =
        (powerIn - Pinel - Qsheath) / (1.5 * neSafe) -
        (bulk.Te0 / neSafe) * densitiesRate["e-"];

    bulk.Te0 += dTeDt * bulk.dt;
    if (!std::isfinite(bulk.Te0) || bulk.Te0 < 0.05)
        bulk.Te0 = 0.05;

    for (auto& p : bulk.densities)
    {
        p.second += densitiesRate[p.first] * bulk.dt;

        const auto propIt = Species.find(p.first);
        const bool neutral = propIt != Species.end() && propIt->second.charge == 0;
        const double floor = neutral ? 1.0 : 0.0;
        if (!std::isfinite(p.second) || p.second < floor)
            p.second = floor;
    }
}

void advanceModelForDuration(BulkModel& bulk)
{
    if (bulk.dt <= 0.0 || bulk.duration <= 0.0)
        return;

    constexpr std::uint64_t maxStartupSteps = 100000;
    const auto requestedSteps = static_cast<std::uint64_t>(std::ceil(bulk.duration / bulk.dt));
    const auto steps = std::min(requestedSteps, maxStartupSteps);

    for (std::uint64_t step = 0; step < steps; ++step)
    {
        advanceModel(bulk);
        if (!std::isfinite(bulk.Te0))
            break;
    }

    bulk.Ngas = totalNeutralDensity(bulk);
}

void initializeSheath(BulkModel& bulk, Sheath& sheath)
{
    const int points = std::max(bulk.sheathPoints, 2);
    const double Te = std::max(bulk.Te0, 0.05);
    const double ne = std::max(getDensity(bulk, "e-"), 1e12);
    const double period = 1.0 / std::max(bulk.biasFrequency, 1e-30);
    const double dt = period / points;
    const double M_eff = effectiveIonMassDensityWeighted(bulk);
    const double JiBulk = std::max(totalIonCurrentDensity(bulk, Te), 0.0);
    const double IiTotal = JiBulk * bulk.substrateArea;
    const double vScale = bulk.useBias ? bulk.biasVoltageGuess : bulk.residualVoltageRipple;
    const double electronSatCurrent =
        E_CHARGE * electronThermalSpeed(Te) * ne * bulk.substrateArea / 4.0;
    const double floatingVoltage = Te * std::log(
        std::max(IiTotal, 1e-30) / std::max(electronSatCurrent, 1e-30));
    const double meanDrop = std::max(std::abs(floatingVoltage) + std::abs(vScale), 1.0);
    const double rfRipple = bulk.useBias ? 0.30 * std::abs(vScale) : 0.50 * std::abs(vScale);
    const double factor = (4.0 * EPS0 / 9.0) *
        std::sqrt(2.0 * E_CHARGE / M_eff);
    const double JiForSheath = std::max(JiBulk, 1e-30);

    sheath.time.resize(points);
    sheath.voltageWaveform.resize(points);
    sheath.thicknessWaveform.resize(points);

    for (int i = 0; i < points; ++i)
    {
        const double t = i * dt;
        const double phase = std::sin(2.0 * PI * bulk.biasFrequency * t);
        const double VsAbs = std::max(meanDrop + rfRipple * phase, 1.0);
        sheath.time[i] = t;
        sheath.voltageWaveform[i] = -VsAbs;
        sheath.thicknessWaveform[i] = clampValue(
            std::sqrt(factor * std::pow(VsAbs, 1.5) / JiForSheath),
            1e-6,
            5e-3);
    }

    sheath.voltage = static_cast<float>(
        std::accumulate(
            sheath.voltageWaveform.begin(),
            sheath.voltageWaveform.end(),
            0.0) /
        std::max<std::size_t>(sheath.voltageWaveform.size(), 1));
    sheath.thickness = static_cast<float>(
        std::accumulate(
            sheath.thicknessWaveform.begin(),
            sheath.thicknessWaveform.end(),
            0.0) /
        std::max<std::size_t>(sheath.thicknessWaveform.size(), 1));

    if (!std::isfinite(sheath.voltage))
        sheath.voltage = 0.0f;
    if (!std::isfinite(sheath.thickness) || sheath.thickness <= 0.0f)
        sheath.thickness = 1e-6f;
}

TransportResult transportSpecies(
    BulkModel& bulk,
    Sheath& sheath,
    double mass,
    double Ngas,
    double ionDensity,
    int nParticles)
{
    TransportResult result;
    result.flux = 0.0;

    if (mass <= 0.0 || Ngas <= 0.0 || nParticles <= 0 ||
        sheath.time.empty() || sheath.voltageWaveform.empty() ||
        sheath.thicknessWaveform.empty())
    {
        return result;
    }

    const double uB = bohmSpeed(bulk.Te0, mass);
    result.flux = std::max(ionDensity, 0.0) * uB;

    const double dt = std::max(bulk.ionDt, 1e-12);
    const double period = sheath.time.back() +
        (sheath.time.size() > 1 ? sheath.time[1] - sheath.time[0] : dt);
    const int maxSteps = std::max(1, static_cast<int>(bulk.maxCycles * period / dt));

    std::mt19937 rng(3);
    std::uniform_real_distribution<double> phaseDist(0.0, 1.0);
    std::normal_distribution<double> bohmDist(uB, 0.2 * uB);

    for (int i = 0; i < nParticles; ++i)
    {
        double t0 = phaseDist(rng) * period;
        double x = periodicLinearInterp(sheath.time, sheath.thicknessWaveform, t0);
        double v = std::max(bohmDist(rng), 0.0);
        int cxCount = 0;
        int steps = 0;

        while (x > 0.0 && steps++ < maxSteps)
        {
            const double dNow = std::max(
                periodicLinearInterp(sheath.time, sheath.thicknessWaveform, t0),
                1e-8);
            const double VNow = periodicLinearInterp(sheath.time, sheath.voltageWaveform, t0);
            const double E = electricField(VNow, x, dNow);
            const double aE = E_CHARGE * E / mass;
            const double Eion = 0.5 * mass * v * v / E_CHARGE;
            const double vth = gasThermalSpeed(mass, bulk.gasTemp);
            const double vrel = std::max(std::abs(v), vth);

            const double sigMt = bulk.enableMomentumTransfer
                ? bulk.momentumTransferScale * sigmaMT(Eion)
                : 0.0;
            const double nuM = Ngas * sigMt * vrel;
              
            v += (aE - nuM * v) * dt;
            v = std::max(v, 0.0);
            x -= v * dt;
            t0 += dt;

            if (!std::isfinite(x) || !std::isfinite(v))
            {
                v = 0.0;
                break;
            }

            if (bulk.enableChargeExchange && x >= 0.3 * dNow && cxCount < 1)
            {
                const double sigCx = bulk.chargeExchangeScale * sigmaCX(Eion);
                const double lambda = 1.0 / std::max(Ngas * sigCx, 1e-24);
                const double probability = 1.0 - std::exp(-std::max(v, 0.0) * dt / lambda);

                if (phaseDist(rng) < probability)
                {
                    v = 0.5 * v + 0.5 * vth;
                    ++cxCount;
                }
            }
        }

        if (x <= 0.0)
            result.energies.push_back(0.5 * mass * v * v / E_CHARGE);
    }

    return result;
}

std::vector<ParticleTypeData> generateParticles(BulkModel& bulk, Sheath& sheath)
{
    std::vector<ParticleTypeData> particles;

    double totalIonDensity = 0.0;
    for (const auto& sp : Species)
    {
        if (sp.second.charge > 0)
            totalIonDensity += std::max(getDensity(bulk, sp.first), 0.0);
    }

    for (const auto& sp : Species)
    {
        const std::string& name = sp.first;
        const SpeciesProperties& prop = sp.second;

        if (prop.charge <= 0)
            continue;

        const double ni = std::max(getDensity(bulk, name), 0.0);
        if (ni <= 0.0 || totalIonDensity <= 0.0)
            continue;

        const int particleCount = std::max(
            1,
            static_cast<int>(bulk.ionCount * (ni / totalIonDensity)));

        TransportResult tr = transportSpecies(
            bulk,
            sheath,
            prop.mass * AMU,
            bulk.Ngas,
            ni,
            particleCount);

        ParticleTypeData p;
        p.name = name;
        p.count = particleCount;
        p.halfAngle = 20.0f;
        p.interval = 10;

        buildIEDF(tr, p.iedf, 200);
        if (!p.iedf.energyCenters.empty())
            p.energy = p.iedf.energyCenters[
                std::distance(
                    p.iedf.pdf.begin(),
                    std::max_element(p.iedf.pdf.begin(), p.iedf.pdf.end()))];

        particles.push_back(std::move(p));
    }

    return particles;
}

void buildIEDF(const TransportResult& result, EnergyDistribution& dist, int bins)
{
    dist.energyCenters.clear();
    dist.pdf.clear();

    if (result.energies.empty() || bins < 2)
        return;

    const double maxEnergy = *std::max_element(result.energies.begin(), result.energies.end());
    const double eMax = std::max(300.0, 1.5 * maxEnergy);
    const int centerCount = bins - 1;
    const double binWidth = eMax / centerCount;

    std::vector<double> hist(centerCount, 0.0);
    for (double energy : result.energies)
    {
        int idx = static_cast<int>(energy / binWidth);
        idx = std::clamp(idx, 0, centerCount - 1);
        hist[idx] += 1.0;
    }

    double sum = std::accumulate(hist.begin(), hist.end(), 0.0);
    for (double& v : hist)
        v /= (sum * binWidth + 1e-30);

    hist = smoothGaussian(hist, 1.5);

    for (int i = 0; i < centerCount; ++i)
    {
        const double center = (i + 0.5) * binWidth;
        if (center < 50.0)
            hist[i] *= std::pow(center / 50.0, 0.8);
    }

    sum = std::accumulate(hist.begin(), hist.end(), 0.0);
    for (double& v : hist)
        v = (result.flux / AVOGADRO) * (v / (sum * binWidth + 1e-30));

    dist.energyCenters.resize(centerCount);
    dist.pdf.resize(centerCount);
    for (int i = 0; i < centerCount; ++i)
    {
        dist.energyCenters[i] = static_cast<float>((i + 0.5) * binWidth);
        dist.pdf[i] = static_cast<float>(hist[i]);
    }
}
