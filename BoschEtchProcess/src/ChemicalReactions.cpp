#include "ChemicalReactions.h"
#include "structures.h"
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
			Pinel += R * rxn.energy;
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

	Psheath = gammaI * (bulk.Area / bulk.Volume) * (bulk.Te0 * log(sqrt(bulk.mi/(2 * PI * E_MASS))) + 2.5 * bulk.Te0) ;
	double Ptotal = bulk.Pabs - (Psheath + Pinel);
	dTe_dt = Ptotal/(1.5 * bulk.densities["e"]) - bulk.Te0 / (bulk.densities["e"]) * densitiesRate["e"];
	bulk.Te0 += dTe_dt * bulk.dt;

	for (auto& p : bulk.densities)
	{
		p.second += densitiesRate[p.first] * bulk.dt;

		if (p.second < 0.0)
			p.second = 0.0;
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
        TransportResult tr =
            transportSingleSpecies(
                bulk,
                sheath,
                prop.mass * AMU,
                bulk.Ngas,
                bulk.densities[name] / 1e20);

        // Build IEDF
        buildIEDF(
            tr,
            p.iedf);

        // Number of particles to launch
        p.count =
            bulk.densities[name] / 1e17;

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