#include <Measurments.h>
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

std::vector<float> Measure::convolve(std::vector<int> &data, int k){
	
	std::vector<float> y(data.size() + k -1, 0);
	std::vector<float> avg(k, 1.0f / k);

	for (int i = 0;i < data.size();i++) {
		for (int j = 0; j < avg.size();j++) {
			y[i+j] += data[i] * avg[j];
		}
	}

	return y;
}
float Measure::getWidth(std::vector<float>& data, int depth) {
	int X1 = -1;
	int X2 = -1;

	for (int i = 0;i > data.size() / 2;i++) {
		if (data[i] >= (float) depth) {
			X1 = i;
			break;
		}
	}
	for (int j = data.size() / 2;j < data.size();j++) {
		if (data[j] <= (float) depth) {
			X2 = j;
			break;
		}
	}

	return X2 - X1;
}
float Measure::findMax(std::vector<float> &data){
	float x = 0;
	for (int i = 0; i < data.size();i++) {
		if (data[i] > x) x = data[i];
	}
	return x;
}

float Measure::getDepth(std::vector<float>& data) {
	float depth = findMax(data);
	return depth;
}