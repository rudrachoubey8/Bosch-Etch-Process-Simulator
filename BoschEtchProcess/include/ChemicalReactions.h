#include <vector>
#include <unordered_map>
#include <string>
#include <cmath>
#include <random>
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

std::unordered_map<std::string, SpeciesProperties> Species;

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


	double Pabs;
	double pump;
};

struct Sheath {
	float voltage;
	float thickness;
};


void advanceModel(BulkModel& bulk);
void initializeSheath(BulkModel& bulk, Sheath& sheath);

