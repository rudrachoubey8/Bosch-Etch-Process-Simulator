#include <structures.h>
#include <vector>
#include "settings.h"
class Measure {

public:

	std::vector<int> XYPlane;
	std::vector<int> ZYPlane;

	void measure(Grid& g1, int x, int y, int z, int dx, int dy, int dz);
	float getDepth(std::vector<float> &data);
	float getWidth(std::vector<float> & data, int depth);
	std::vector<float> convolve(std::vector<int>& data, int k);

private:
	float findMax(std::vector<float> &data);
};