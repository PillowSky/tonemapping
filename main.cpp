#include <iostream>
#include <array>
#include <Magick++.h>
#include <sys/time.h>
#include <boost/format.hpp>

#include "pfs.h"
#include "exrio.h"
#include "tmo_fattal02.h"

using namespace std;
using namespace boost;
using namespace Magick;

typedef array<float, 3> vec3f;

vec3f simpleTonemapping(const vec3f& color);
vec3f rgb2Yxy(vec3f rgb);
vec3f Yxy2rgb(vec3f Yxy);

template<class T>
T clamp(const T v, const T minV, const T maxV);

struct timeval tpstart, tpend;
void logTime(const string& message);

int main(int argc, char* argv[]) {
	gettimeofday(&tpstart, NULL);

	float opt_alpha = 1.0f;
	float opt_beta = 0.9f;
	float opt_gamma = 0.8f;
	//float opt_saturation = 1.0f;
	float opt_noise = 0.02f;
	int   opt_detail_level = 3;
	float opt_black_point = 0.1f;
	float opt_white_point = 0.5f;
	bool  opt_fftsolver = true;

	if (argc != 5) {
		cout << format("Usage: %1% <exr image> <map image> <simple image> <fusion image>") % argv[0] << endl;
		return EXIT_FAILURE;
	}

	logTime("program inited");

	OpenEXRReader reader(argv[1]);

	logTime("image opened");

	int w = reader.getWidth();
	int h = reader.getHeight();
	int pixelCount = w * h;
	int valueCount = pixelCount * 3;
	float maxValue8 = (float)(1<<8) - 1;
	float maxValue16 = (float)(1<<16) - 1;

	logTime("constant done");

	pfs::Array2DImpl* __R = new pfs::Array2DImpl(w, h);
	pfs::Array2DImpl* __G = new pfs::Array2DImpl(w, h);
	pfs::Array2DImpl* __B = new pfs::Array2DImpl(w, h);

	pfs::Array2DImpl* X = new pfs::Array2DImpl(w, h);
	pfs::Array2DImpl* Y = new pfs::Array2DImpl(w, h);
	pfs::Array2DImpl* Z = new pfs::Array2DImpl(w, h);

	pfs::Array2DImpl* _R = new pfs::Array2DImpl(w, h);
	pfs::Array2DImpl* _G = new pfs::Array2DImpl(w, h);
	pfs::Array2DImpl* _B = new pfs::Array2DImpl(w, h);

	logTime("memory alloc");

	reader.readImage(__R, __G, __B);

	logTime("image read");

	memcpy(_R->getRawData(), __R->getRawData(), sizeof(float) * pixelCount);
	memcpy(_G->getRawData(), __G->getRawData(), sizeof(float) * pixelCount);
	memcpy(_B->getRawData(), __B->getRawData(), sizeof(float) * pixelCount);

	logTime("memory copy");

	pfs::transformColorSpace(pfs::CS_RGB, __R, __G, __B, pfs::CS_XYZ, X, Y, Z);

	if(Y == NULL || X == NULL || Z == NULL) {
		throw pfs::Exception( "Missing X, Y, Z channels in the PFS stream" );
	}

	// tone mapping
	pfs::Array2DImpl* L = new pfs::Array2DImpl(w, h);
	tmo_fattal02(w, h, Y->getRawData(), L->getRawData(), opt_alpha, opt_beta,
					opt_gamma, opt_noise, opt_detail_level,
					opt_black_point, opt_white_point, opt_fftsolver);

	logTime("tone mapped");

	// in-place color space transform
	pfs::Array2DImpl *G = new pfs::Array2DImpl(w, h); // copy for G to preserve Y
	pfs::Array2D *R = X, *B = Z;

	pfs::transformColorSpace(pfs::CS_XYZ, X, Y, Z, pfs::CS_RGB, R, G, B);
		
	// Color correction
	for (int i = 0; i < pixelCount; i++)	{
		static const float epsilon = 1e-4f;
		float y = max((*Y)(i), epsilon);
		float l = max((*L)(i), epsilon);

		(*R)(i) = max((*R)(i) / y, 0.0f) * l;
		(*G)(i) = max((*G)(i) / y, 0.0f) * l;
		(*B)(i) = max((*B)(i) / y, 0.0f) * l;
	}

	logTime("color corrected");

	// save tone mapping png file
	unsigned char* mapBuffer = new unsigned char[valueCount];
	for(int i = 0, pix = 0; pix < pixelCount; pix++ ) {
		mapBuffer[i++] = (unsigned char)(clamp((*R)(pix), 0.0f, 1.0f) * maxValue8);
		mapBuffer[i++] = (unsigned char)(clamp((*G)(pix), 0.0f, 1.0f) * maxValue8);
		mapBuffer[i++] = (unsigned char)(clamp((*B)(pix), 0.0f, 1.0f) * maxValue8);
	}

	logTime("converted to buffer");

	unsigned char* simpleBuffer = new unsigned char[valueCount];
	for(int i = 0, pix = 0; pix < pixelCount; pix++ ) {
		vec3f current = {(*_R)(pix), (*_G)(pix), (*_B)(pix)};
		vec3f simple = simpleTonemapping(current);

		simpleBuffer[i++] = (unsigned char)(clamp(simple[0], 0.0f, 1.0f) * maxValue8);
		simpleBuffer[i++] = (unsigned char)(clamp(simple[1], 0.0f, 1.0f) * maxValue8);
		simpleBuffer[i++] = (unsigned char)(clamp(simple[2], 0.0f, 1.0f) * maxValue8);
	}

	logTime("simple tone mapped");
	Magick::Image mapImage(w, h, "RGB", Magick::CharPixel, mapBuffer);
	Magick::Image simpleImage(w, h, "RGB", Magick::CharPixel, simpleBuffer);
	
	logTime("convert to Magick image");

	simpleImage.modulate(100, 115, 100);
	simpleImage.level(0, maxValue16 * 0.51, 1.0);

	logTime("enhance");

	mapImage.write(argv[2]);
	simpleImage.write(argv[3]);

	// opacity here! difference from weight
	simpleImage.opacity(maxValue16 * 0.7);
	mapImage.opacity(maxValue16 * 0.35);

	logTime("opacity");

	simpleImage.composite(mapImage, 0, 0, Magick::CompositeOperator::MultiplyCompositeOp);

	logTime("composite");

	simpleImage.opacity(0);
	simpleImage.quality(100);
	simpleImage.depth(8);

	logTime("prepare write");

	simpleImage.write(argv[4]);

	logTime("complete");

	delete[] mapBuffer;
	delete[] simpleBuffer;
	delete X;
	delete Y;
	delete Z;
	delete G;    
	delete L;
	delete _R;
	delete _G;
	delete _B;
	delete __R;
	delete __G;
	delete __B;

	return EXIT_SUCCESS;
}

inline vec3f simpleTonemapping(const vec3f& color) {
	vec3f bloomed_Yxy = rgb2Yxy(color);
	float scaled_Y = bloomed_Yxy[0];

	if (color[0] < 1e-7f && color[1] < 1e-7f && color[2] < 1e-7f) {
		return color;
	} else {
		vec3f result = {scaled_Y / (scaled_Y + 1.0f), bloomed_Yxy[1], bloomed_Yxy[2]};
		return Yxy2rgb(result);
	}
}

inline vec3f rgb2Yxy(vec3f rgb) {
	float X = rgb[0] * 0.4124f + rgb[1] * 0.3576f + rgb[2] * 0.1805f;
	float Y = rgb[0] * 0.2126f + rgb[1] * 0.7152f + rgb[2] * 0.0722f;
	float Z = rgb[0] * 0.0193f + rgb[1] * 0.1192f + rgb[2] * 0.9505f;

	vec3f result = {Y, X / (X + Y + Z), Y / (X + Y + Z)};
	return result;
}

inline vec3f Yxy2rgb(vec3f Yxy) {
	// First convert to xyz
	vec3f xyz = {Yxy[1] * (Yxy[0] / Yxy[2]), Yxy[0], (1.0f - Yxy[1] - Yxy[2]) * (Yxy[0] / Yxy[2])};

	float R = xyz[0] *  3.2410f + xyz[1] * -1.5374f + xyz[2] * -0.4986f;
	float G = xyz[0] * -0.9692f + xyz[1] *  1.8760f + xyz[2] *  0.0416f;
	float B = xyz[0] *  0.0556f + xyz[1] * -0.2040f + xyz[2] *  1.0570f;

	vec3f result = {R, G, B};
	return result;
}

template<class T>
inline T clamp(const T v, const T minV, const T maxV) {
	if( v < minV ) return minV;
	if( v > maxV ) return maxV;
	return v;
}

void logTime(const string& message) {
	gettimeofday(&tpend, NULL);
	double timeuse = (1000000 * (tpend.tv_sec - tpstart.tv_sec) + tpend.tv_usec - tpstart.tv_usec) / 1000000.0;
	cout << format("[%1%] %2%") % timeuse % message << endl;
}
