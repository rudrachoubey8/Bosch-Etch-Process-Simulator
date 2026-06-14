#include "ChemicalReactions.h"

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
