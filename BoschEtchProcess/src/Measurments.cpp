#include <Measurments.h>
#include <vector>
#include "settings.h"

void Measure::measure(Grid& g, int x, int y, int z, int dx, int dy, int dz) {

	for (int i = 0;i < Settings::X;i++) {
		int d = y;
		while (true) {
			if (g.inBounds(i, d, z))
			{
				if (g.at(i, d, z).solid) break;
				d += dy;
			}
			else {
				break;
			}
		}
		XYPlane.push_back(d);
	}

	for (int i = 0;i < Settings::Z;i++) {
		int d = y;
		while (true) {
			if (g.inBounds(x, d, i))
			{
				if (g.at(x, d, i).solid) break;
				d += dy;
			}
			else {
				break;
			}
		}
		ZYPlane.push_back(d);
	}
}