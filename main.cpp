#include <iostream>
#include <thread>
#include <Magick++.h>
#include <boost/timer.hpp>
#include <boost/format.hpp>
#include <opencv2/core/core.hpp>

#include "pfs.h"
#include "exrio.h"
#include "tmo_fattal02.h"

using namespace std;
using namespace cv;
using namespace boost;
using namespace Magick;

void simpleTonemappingImage(unsigned char* data);

Vec3f simpleTonemapping(const Vec3f& color);
Vec3f rgb2Yxy(Vec3f rgb);
Vec3f Yxy2rgb(Vec3f Yxy);

template<class T>
inline T clamp(const T v, const T minV, const T maxV);

void logTime(const string& message);

int main(int argc, char* argv[]) {
	float opt_alpha = 1.0f;
	float opt_beta = 0.9f;
	float opt_gamma = 0.8f;
	float opt_saturation=1.0f;
	float opt_noise = 0.02f;
	int   opt_detail_level= 3;
	float opt_black_point=0.1f;
	float opt_white_point=0.5f;
	bool  opt_fftsolver=true;

	if (argc != 2) {
		cout << "Usage: " << argv[0] << " <exr image>" << endl;
		return EXIT_FAILURE;
	}

	OpenEXRReader reader(argv[1]);

	int w = reader.getWidth();
	int h = reader.getHeight();
	int pixelCount = w * h;
	int valueCount = pixelCount * 3;
	float maxValue8 = (float)(1<<8) - 1;
	float maxValue16 = (float)(1<<16) - 1;

	pfs::Array2DImpl* X = new pfs::Array2DImpl(w, h);
	pfs::Array2DImpl* Y = new pfs::Array2DImpl(w, h);
	pfs::Array2DImpl* Z = new pfs::Array2DImpl(w, h);

	pfs::Array2DImpl* _R = new pfs::Array2DImpl(w, h);
	pfs::Array2DImpl* _G = new pfs::Array2DImpl(w, h);
	pfs::Array2DImpl* _B = new pfs::Array2DImpl(w, h);

	reader.readImage(X, Y, Z);
	reader.readImage(_R, _G, _B);

	if(Y == NULL || X == NULL || Z == NULL) {
		throw pfs::Exception( "Missing X, Y, Z channels in the PFS stream" );
	}

	/*unsigned char* srcBuffer = new unsigned char[valueCount];
	for(int i = 0, pix = 0; pix < w * h; pix++) {
		srcBuffer[i++] = (unsigned char)(clamp((*X)(pix), 0.0f, 1.0f) * maxValue8);
		srcBuffer[i++] = (unsigned char)(clamp((*Y)(pix), 0.0f, 1.0f) * maxValue8);
		srcBuffer[i++] = (unsigned char)(clamp((*Z)(pix), 0.0f, 1.0f) * maxValue8);
	}

	unsigned char* simpleBuffer = new unsigned char[valueCount];
	memcpy(simpleBuffer, srcBuffer, sizeof(unsigned char) * valueCount);
	simpleTonemappingImage(simpleBuffer);

	Image srcImage(w, h, "RGB", Magick::CharPixel, srcBuffer);*/

	// tone mapping
	pfs::Array2DImpl* L = new pfs::Array2DImpl(w, h);
	tmo_fattal02(w, h, Y->getRawData(), L->getRawData(), opt_alpha, opt_beta,
					opt_gamma, opt_noise, opt_detail_level,
					opt_black_point, opt_white_point, opt_fftsolver);

	// in-place color space transform
	pfs::Array2DImpl *G = new pfs::Array2DImpl(w, h); // copy for G to preserve Y
	pfs::Array2D *R = X, *B = Z;

	pfs::transformColorSpace(pfs::CS_XYZ, X, Y, Z, pfs::CS_RGB, R, G, B);
		
	// Color correction
	for (int i = 0; i < pixelCount; i++)	{
		static const float epsilon = 1e-4f;
		float y = max((*Y)(i), epsilon);
		float l = max((*L)(i), epsilon);

		(*R)(i) = powf(max((*R)(i) / y, 0.0f), opt_saturation) * l;
		(*G)(i) = powf(max((*G)(i) / y, 0.0f), opt_saturation) * l;
		(*B)(i) = powf(max((*B)(i) / y, 0.0f), opt_saturation) * l;
	}

	pfs::transformColorSpace( pfs::CS_RGB, R, G, B, pfs::CS_XYZ, X, Y, Z );

	// save tone mapping png file
	unsigned char* mapBuffer = new unsigned char[valueCount];
	for(int i = 0, pix = 0; pix < pixelCount; pix++ ) {
		mapBuffer[i++] = (unsigned char)(clamp((*X)(pix), 0.0f, 1.0f) * maxValue8);
		mapBuffer[i++] = (unsigned char)(clamp((*Y)(pix), 0.0f, 1.0f) * maxValue8);
		mapBuffer[i++] = (unsigned char)(clamp((*Z)(pix), 0.0f, 1.0f) * maxValue8);
	}

	Magick::Image mapImage(w, h, "RGB", Magick::CharPixel, mapBuffer);

	unsigned char* simpleBuffer = new unsigned char[valueCount];
	for(int i = 0, pix = 0; pix < pixelCount; pix++ ) {
		Vec3f simple = simpleTonemapping(Vec3f((*_R)(pix), (*_G)(pix), (*_B)(pix)));

		simpleBuffer[i++] = (unsigned char)(clamp(simple[0],0.0f,1.f)*maxValue8);
		simpleBuffer[i++] = (unsigned char)(clamp(simple[1],0.0f,1.f)*maxValue8);
		simpleBuffer[i++] = (unsigned char)(clamp(simple[2],0.0f,1.f)*maxValue8);
	}

	Magick::Image simpleImage(w, h, "RGB", Magick::CharPixel, simpleBuffer);
	simpleImage.modulate(100, 115, 100);
	simpleImage.level(0, maxValue16 * 0.51, 1.0);

	simpleImage.opacity(maxValue16 * 0.7);
	mapImage.opacity(maxValue16 * 0.35);

	simpleImage.composite(mapImage, 0, 0, Magick::CompositeOperator::MultiplyCompositeOp);
	simpleImage.quality(100);
	simpleImage.depth(8);
	simpleImage.write("fusion.jpg");

	/*delete[] imgBuffer;
	delete X;
	delete Y;
	delete Z;
	delete G;    
	delete L;*/

	return EXIT_SUCCESS;
}

void simpleTonemappingImage(Image& image) {

}

Vec3f simpleTonemapping(const Vec3f& color) {
	Vec3f bloomed_Yxy = rgb2Yxy(color);
	float scaled_Y = bloomed_Yxy[0];
	float mapped_Y;

	if (color[0] < 1e-7f && color[1] < 1e-7f && color[2] < 1e-7f) {
		return color;
	}

	mapped_Y = scaled_Y / (scaled_Y + 1.0);

	Vec3f mapped_Yxy = Vec3f(mapped_Y, bloomed_Yxy[1], bloomed_Yxy[2]);
	Vec3f mapped_rgb = Yxy2rgb(mapped_Yxy);
	return mapped_rgb;
}

inline Vec3f rgb2Yxy(Vec3f rgb) {
	float X = rgb.dot(Vec3f(0.4124f, 0.3576f, 0.1805f));
	float Y = rgb.dot(Vec3f(0.2126f, 0.7152f, 0.0722f));
	float Z = rgb.dot(Vec3f(0.0193f, 0.1192f, 0.9505f));

	return Vec3f(Y, X / (X + Y + Z), Y / (X + Y + Z));
}

inline Vec3f Yxy2rgb(Vec3f Yxy) {
	// First convert to xyz
	Vec3f xyz = Vec3f(Yxy[1] * (Yxy[0] / Yxy[2]), Yxy[0], (1.0f - Yxy[1] - Yxy[2]) * (Yxy[0] / Yxy[2]));

	float R = xyz.dot(Vec3f(3.2410f, -1.5374f, -0.4986f));
	float G = xyz.dot(Vec3f(-0.9692f, 1.8760f, 0.0416f));
	float B = xyz.dot(Vec3f(0.0556f, -0.2040f, 1.0570f));

	return Vec3f(R, G, B);
}

template<class T>
inline T clamp(const T v, const T minV, const T maxV) {
	if( v < minV ) return minV;
	if( v > maxV ) return maxV;
	return v;
}

void logTime(const string& message) {
	static timer t;
	cout << boost::format("[%1%] %2%") % t.elapsed() % message << endl;
}
