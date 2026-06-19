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
			if (p.first == "e") {
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
			double uB = sqrt(E_CHARGE * bulk.Te0 / p.second.mass);
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
        bulk.densities["e"],
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



TransportResult transportSpecies(
    BulkModel& bulk,
    Sheath& sheath,
    double mass,
    double Ngas,
    int nParticles)
{
    TransportResult result;

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

        while (x > 0.0)
        {
            //-----------------------------------
            // electric acceleration
            //-----------------------------------

            double E =
                electricField(
                    sheath.voltage,
                    x,
                    sheath.thickness);

            double a =
                E_CHARGE * E / mass;

            //-----------------------------------
            // momentum transfer
            //-----------------------------------

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

            //-----------------------------------
            // charge exchange
            //-----------------------------------

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