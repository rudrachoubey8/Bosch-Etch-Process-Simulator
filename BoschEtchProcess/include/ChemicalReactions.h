#pragma once
#include <vector>
#include <unordered_map>
#include <string>
#include <cmath>
#include <random>
#include "structures.h"
const double PI = 3.14159265358979323846;
const double E = 2.71828182845904523536;
const double E_CHARGE = 1.602176634e-19;
const double E_MASS = 9.10938356e-31;
const double AMU = 1.66053906660e-27;
const double EPS0 = 8.854187817e-12;
const double K_B = 1.380649e-23;

struct SpeciesProperties {
	// in e
	int charge = 0;

	// In AMU
	double mass = 1;
};

inline std::unordered_map<std::string, SpeciesProperties> Species =
{
	{"Ar",      { 0, 39.948 }},
	{"Ar*",     { 0, 39.948 }},
	{"Ar+",     {+1, 39.948 }},

	{"C4F8",    { 0, 200.0 }},
	{"CF4",     { 0, 88.0 }},
	{"CF4*",    { 0, 88.0 }},
	{"C2F4",    { 0, 100.0 }},
	{"CF3",     { 0, 69.0 }},
	{"CF2",     { 0, 50.0 }},
	{"F",       { 0, 19.0 }},

	{"CF3+",    {+1, 69.0 }},
	{"CF2+",    {+1, 50.0 }},
	{"C2F4+",   {+1, 100.0 }},
	{"F-",      {-1, 19.0 }},

	{"e-",      {-1, 5.485799e-4 }}
};

struct Reaction {
	std::string name;

	// in eV
	double energy;

	double a, b, c;

	std::unordered_map<std::string, int> reactants;
	std::unordered_map<std::string, int> products;

};

struct BulkModel
{
	// densities
	std::unordered_map<std::string, double> densities;

	// In density terms
	std::unordered_map<std::string, double> inPump;

	// Inlet flow in SCCM for the mother neutrals.
	std::unordered_map<std::string, double> motherNeutralFlowSccm;

	// chemistry
	std::vector<Reaction> reactions;

	// integration
	double dt;
	double duration;

	// electron temperature (eV)
	double Te0 = 3.0;

	// mass in AMU
	double mi = 1;

	// in m terms
	double Area = 1;
	double Volume = 1;

	double Ngas = 2.6e20;
	double Pabs;
	double pump;

	double pressureMtorr = 10.0;
	double gasTemp = 373.0;
	double substrateArea = 0.01;
	double biasPower = 200.0;
	double biasFrequency = 2.0e6;
	double biasVoltageGuess = 100.0;
	double residualVoltageRipple = 20.0;
	int sheathPoints = 240;
	int sheathIterations = 20;
	int ionCount = 5000;
	double ionDt = 1e-9;
	int maxCycles = 20;
	bool enableChargeExchange = true;
	bool enableMomentumTransfer = true;
	double chargeExchangeScale = 0.1;
	double momentumTransferScale = 1.0;
	bool useBias = true;
};

struct Sheath {
	float voltage;
	float thickness;
	std::vector<double> time;
	std::vector<double> voltageWaveform;
	std::vector<double> thicknessWaveform;
};


void advanceModel(BulkModel& bulk);

void advanceModelForDuration(BulkModel& bulk);

void initializeSheath(BulkModel& bulk, Sheath& sheath);

std::vector<ParticleTypeData> generateParticles(BulkModel& bulk, Sheath& sheath);

void buildIEDF(const TransportResult& result, EnergyDistribution& dist, int bins);
