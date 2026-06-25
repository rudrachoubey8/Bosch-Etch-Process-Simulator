#include "ChemicalReactions.h"
#include <algorithm>
#include <iostream>
double sigmaCX(double E)
{
    E = std::max(E, 0.1);
    return 2.5e-19 * (1.0 + 0.15 * exp(-(E - 60.0) * (E - 60.0) / 3000.0));
}

double sigmaMT(double E)

{
    E = std::max(E, 0.1);
    return 1.4e-19 * (1.0 + 0.10 * exp(-(E - 30.0) * (E - 30.0) / 2000.0));
}

double electricField(double V, double x, double d)
{
    // E = -(3/2)*(V/d)*sqrt(x/d)
    if (d <= 1e-12)
        return 0.0;

    double xi = std::clamp(x / d, 0.0, 1.0);
    
    return -(1.5 * V / d) * sqrt(xi);
}

void advanceModel(BulkModel& bulk) {
	std::unordered_map<std::string, double> densitiesRate;
	
	double dTe_dt = 0;

	for (const auto& p : bulk.densities) {
		densitiesRate[p.first] = 0.0;
	}
	double Pinel = 0;
	for (int i = 0;i < bulk.reactions.size();i++)
	{
		Reaction& rxn = bulk.reactions[i];
		
		// rxn = A+B -> C+D
		// k = a * (Te)^b * (e^(-c/Te))
		double k = rxn.a * pow(bulk.Te0, rxn.b) * exp(-rxn.c / bulk.Te0);

		// Rate = k[A]^(stoiA)[B]^stoi[b]
		double R = k;
		for (auto& p : rxn.reactants) {
			R *= pow(bulk.densities[p.first], p.second);
		}

		if (!std::isfinite(R))
			continue;

		// dn_i/dt = (stoichometric difference) * R
		// subtract reactants
		bool electronRxn = 0;
		for (auto& p : rxn.reactants) {
			if (p.first == "e-") {
				electronRxn = 1;
			}
			densitiesRate[p.first] -= R * p.second;
		}
		if(electronRxn)
			Pinel += R * rxn.energy * E_CHARGE;
		// add products
		for (auto& p : rxn.products) {
			densitiesRate[p.first] += R * p.second;
		}

	}


	double Psheath = 0;
	double gammaI = 0.0;

	for (auto& p : Species)
	{	
		densitiesRate[p.first] += bulk.inPump[p.first];
		if (p.second.charge > 0)
		{
			double uB = sqrt(E_CHARGE * bulk.Te0 / (p.second.mass * AMU));
			densitiesRate[p.first] -= bulk.densities[p.first] * uB * bulk.Area / bulk.Volume;
			gammaI += bulk.densities[p.first] * uB;
		}
		if (p.second.charge == 0) {
			densitiesRate[p.first] -= bulk.pump * bulk.densities[p.first];
		}
	}

	double effectiveIonMass = 0.0;
	double ionDensity = 0.0;
	for (const auto& p : Species)
	{
		if (p.second.charge > 0)
		{
			const double density = bulk.densities[p.first];
			effectiveIonMass += density * p.second.mass * AMU;
			ionDensity += density;
		}
	}
	effectiveIonMass =
		std::max(
			effectiveIonMass / std::max(ionDensity, 1e-30),
			AMU);

	const double sheathEnergy =
		bulk.Te0 * log(sqrt(effectiveIonMass / (2 * PI * E_MASS))) +
		2.5 * bulk.Te0;
	Psheath =
		gammaI *
		(bulk.Area / bulk.Volume) *
		sheathEnergy *
		E_CHARGE;

	const double absorbedPowerDensity = bulk.Pabs / bulk.Volume;
	const double Ptotal = absorbedPowerDensity - (Psheath + Pinel);
	const double electronDensity = std::max(bulk.densities["e-"], 1e12);
	dTe_dt =
		Ptotal / (1.5 * electronDensity * E_CHARGE) -
		bulk.Te0 / electronDensity * densitiesRate["e-"];
    
	bulk.Te0 += dTe_dt * bulk.dt;
	if (!std::isfinite(bulk.Te0) || bulk.Te0 < 0.01)
		bulk.Te0 = 0.01;
	for (auto& p : bulk.densities)
	{
		p.second += densitiesRate[p.first] * bulk.dt;

		if (!std::isfinite(p.second) || p.second < 0.0)
			p.second = 0.0;
	}
}

void advanceModelForDuration(BulkModel& bulk)
{
	if (bulk.dt <= 0.0 || bulk.duration <= 0.0)
		return;

	constexpr std::uint64_t maxStartupSteps = 10000;
	const auto requestedSteps = static_cast<std::uint64_t>(
		std::ceil(bulk.duration / bulk.dt));
	const auto steps = std::min(requestedSteps, maxStartupSteps);

	for (std::uint64_t step = 0; step < steps; ++step)
	{
		advanceModel(bulk);

		if (!std::isfinite(bulk.Te0))
			break;
	}
}
void initializeSheath(BulkModel& bulk, Sheath& sheath)
{
    // Electron density
    double ne = std::max(
        bulk.densities["e-"],
        1e12
    );

    // Effective ion mass
    double sumNi = 0.0;
    double sumNiMi = 0.0;

    for (auto& p : Species)
    {
        if (p.second.charge > 0)
        {
            double ni = bulk.densities[p.first];

            sumNi += ni;

            sumNiMi +=
                ni *
                p.second.mass *
                AMU;
        }
    }

    double M_eff =
        sumNiMi /
        std::max(sumNi, 1e-30);
    M_eff = std::max(M_eff, AMU);

    // Bohm speed
    double uB =
        sqrt(
            E_CHARGE *
            bulk.Te0 /
            M_eff
        );

    // Bohm current density
    double Ji =
        E_CHARGE *
        ne *
        uB;

    // Floating sheath voltage
    sheath.voltage =
        bulk.Te0 *
        log(
            sqrt(
                M_eff /
                (2.0 * PI * E_MASS)
            )
        );

    // Child-Langmuir thickness
    double factor =
        (4.0 * EPS0 / 9.0)
        *
        sqrt(
            E_CHARGE /
            (2.0 * M_eff)
        );

    sheath.thickness =
        sqrt(
            factor *
            pow(sheath.voltage, 1.5)
            /
            std::max(Ji, 1e-30)
        );
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
    int nParticles)
{
    TransportResult result;
    if (mass <= 0.0 || Ngas <= 0.0 || nParticles <= 0)
        return result;

    double uB =
        sqrt(E_CHARGE * bulk.Te0 / mass);

    double dt = 1e-9;

    std::mt19937 rng(1234);

    std::uniform_real_distribution<double> phaseDist(
        0.0,
        1.0);

    std::normal_distribution<double> bohmDist(
        uB,
        0.2 * uB);

    for (int i = 0;i < nParticles;i++)
    {
        double x = sheath.thickness;

        double v =
            std::max(
                bohmDist(rng),
                0.0);

        int cxCount = 0;

        constexpr int maxTransportSteps = 100000;
        int transportStep = 0;

        while (x > 0.0 && transportStep++ < maxTransportSteps)
        {
            // electric acceleration
            double E =
                electricField(
                    sheath.voltage,
                    x,
                    sheath.thickness);

            double a =
                -E_CHARGE * E / mass;

            // momentum transfer
            double Eion =
                0.5 * mass * v * v / E_CHARGE;

            double sigma =
                sigmaMT(Eion);

            double nu =
                Ngas * sigma * std::abs(v);

            v += (a - nu * v) * dt;

            if (v < 0)
                v = 0;

            x -= v * dt;

            if (!std::isfinite(x) || !std::isfinite(v))
            {
                v = 0.0;
                break;
            }

            // charge exchange
            if (cxCount == 0)
            {
                double sigCX =
                    sigmaCX(Eion);

                double lambda =
                    1.0 / (Ngas * sigCX);

                double P =
                    1.0 - exp(
                        -std::abs(v) * dt / lambda);

                if (phaseDist(rng) < P)
                {
                    double vth =
                        sqrt(
                            8 * K_B * 373 /
                            (PI * mass));

                    v = 0.5 * v + 0.5 * vth;

                    cxCount++;
                }
            }
        }

        double Ef =
            0.5 * mass * v * v / E_CHARGE;

        result.energies.push_back(Ef);
    }
    return result;
}


std::vector<ParticleTypeData>
generateParticles(
    BulkModel& bulk,
    Sheath& sheath)
{
    std::vector<ParticleTypeData> particles;

    for (auto& sp : Species)
    {
        const std::string& name = sp.first;
        const SpeciesProperties& prop = sp.second;

        // only positive ions
        if (prop.charge <= 0)
            continue;

        // species absent
        if (bulk.densities[name] <= 0.0)
            continue;

        ParticleTypeData p;

        // Monte-Carlo transport
        constexpr double densityPerMacroParticle = 1e18;
        constexpr int maxMacroParticlesPerSpecies = 100000;
        
        const double scaledParticleCount = std::clamp(
            bulk.densities[name] / densityPerMacroParticle,
            1.0,
            static_cast<double>(maxMacroParticlesPerSpecies));

        const int particleCount =
            static_cast<int>(scaledParticleCount);

        TransportResult tr =
            transportSpecies(
                bulk,
                sheath,
                prop.mass * AMU,
                bulk.Ngas,
                particleCount);
        
        
        // Build IEDF
        buildIEDF(
            tr,
            p.iedf, 200);

        p.name = name;
        
        // Number of particles to launch
        p.count = particleCount;

        // Beam spread
        p.halfAngle = 20.0f;

        // Launch interval
        p.interval = 10;

        particles.push_back(
            std::move(p));
    }

    return particles;
}

void buildIEDF(
    const TransportResult& result,
    EnergyDistribution& dist, 
    int bins = 200)
{
    dist.energyCenters.clear();
    dist.pdf.clear();

    if (result.energies.empty())
        return;

    // Find maximum energy
    double Emax =
        *std::max_element(
            result.energies.begin(),
            result.energies.end());
    
    Emax = std::max(Emax, 1.0);

    // Allocate
    dist.energyCenters.resize(bins);
    dist.pdf.assign(bins, 0.0f);

    double dE = Emax / bins;

    // Histogram
    for (double E : result.energies)
    {
        int idx = static_cast<int>(E / dE);

        idx = std::clamp(idx, 0, bins - 1);

        dist.pdf[idx] += 1.0f;

    }
    // Normalize PDF
    float sum = 0.0f;

    for (float p : dist.pdf)
        sum += p;

    if (sum > 0.0f)
    {
        for (float& p : dist.pdf)
            p /= sum;
    }

    // Bin centers
    for (int i = 0; i < bins; i++)
    {
        dist.energyCenters[i] =
            static_cast<float>((i + 0.5) * dE);
    }

}
