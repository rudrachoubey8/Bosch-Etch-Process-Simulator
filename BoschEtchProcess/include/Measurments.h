#include <structures.h>

class Measure {

public:

	std::vector<int> XYPlane;
	std::vector<int> ZYPlane;

	void measure(Grid& g1, int x, int y, int z, int dx, int dy, int dz);

};