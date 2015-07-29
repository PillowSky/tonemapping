/**
 * @file pfstmo_fattal02.cpp
 * @brief Tone map XYZ channels using Fattal02 model
 *
 * Gradient Domain High Dynamic Range Compression
 * R. Fattal, D. Lischinski, and M. Werman
 * In ACM Transactions on Graphics, 2002.
 *
 * 
 * This file is a part of PFSTMO package.
 * ---------------------------------------------------------------------- 
 * Copyright (C) 2003,2004 Grzegorz Krawczyk
 * 
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 * ---------------------------------------------------------------------- 
 * 
 * @author Grzegorz Krawczyk, <krawczyk@mpi-sb.mpg.de>
 *
 * $Id: pfstmo_fattal02.cpp,v 1.10 2012/06/22 12:01:32 rafm Exp $
 */

#include <config.h>

#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <math.h>

#include <pfs.h>

#include <exrio.h>

#include <Magick++.h>

#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>

#include "tmo_fattal02.h"

using namespace std;
using namespace cv;

#define PROG_NAME "pfstmo_fattal02"

/// verbose mode
bool verbose = false;

Vec3f simpleTonemapping(const Vec3f& color);

template<class T>
inline T clamp( const T v, const T minV, const T maxV )
{
    if( v < minV ) return minV;
    if( v > maxV ) return maxV;
    return v;
}

class QuietException 
{
};

void printHelp()
{
	fprintf( stderr, PROG_NAME " (" PACKAGE_STRING ") : \n"
		"\t[--alpha <val>] [--beta <val>] \n"
		"\t[--gamma <val>] \n"
		"\t[--saturation <val>] \n"
		"\t[--noise <val>] \n"
		"\t[--detail-level <val>] \n"
		"\t[--black-point <val>] [--white-point <val>] \n"
		"\t[--multigrid] \n"
		"\t[--verbose] [--help] \n"
		"See man page for more information.\n" );
}

Vec3f rgb2Yxy(Vec3f rgb) {
	float X = rgb.dot(Vec3f(0.4124f, 0.3576f, 0.1805f));
	float Y = rgb.dot(Vec3f(0.2126f, 0.7152f, 0.0722f));
	float Z = rgb.dot(Vec3f(0.0193f, 0.1192f, 0.9505f));

	// convert xyz to Yxy
	return Vec3f(Y, X / (X + Y + Z), Y / (X + Y + Z));
}

Vec3f Yxy2rgb(Vec3f Yxy) {
	// First convert to xyz
	Vec3f xyz = Vec3f(Yxy[1] * (Yxy[0] / Yxy[2]), Yxy[0], (1.0f - Yxy[1] - Yxy[2]) * (Yxy[0] / Yxy[2]));

	const float R = xyz.dot(Vec3f(3.2410f, -1.5374f, -0.4986f));
	const float G = xyz.dot(Vec3f(-0.9692f, 1.8760f, 0.0416f));
	const float B = xyz.dot(Vec3f(0.0556f, -0.2040f, 1.0570f));
	return Vec3f(R, G, B);
}

void pfstmo_fattal02( int argc, char* argv[] )
{
	pfs::DOMIO pfsio;

	//--- default tone mapping parameters;
	float opt_alpha = 1.0f;
	float opt_beta = 0.9f;
	float opt_gamma = 0.8f;    // not set (0.8 for fft solver, 1.0 otherwise)
	float opt_saturation=1.0f;
	float opt_noise = 0.02f;    // not set 
	int   opt_detail_level= 3;  // not set (2 for fft solver, 0 otherwise)
	float opt_black_point=0.1f;
	float opt_white_point=0.5f;

	// Use multigrid if FFTW lib not available
#if !defined(HAVE_FFTW3) || !defined(HAVE_OpenMP)
	bool  opt_fftsolver=false;
#else  
	bool  opt_fftsolver=true;
#endif

	VERBOSE_STR << "threshold gradient (alpha): " << opt_alpha << endl;
	VERBOSE_STR << "strengh of modification (beta): " << opt_beta << endl;
	VERBOSE_STR << "gamma: " << opt_gamma << endl;
	VERBOSE_STR << "noise floor: " << opt_noise << endl;  
	VERBOSE_STR << "saturation: " << opt_saturation << endl;
	VERBOSE_STR << "detail level: " << opt_detail_level << endl;
	VERBOSE_STR << "white point: " << opt_white_point << "%" << endl;
	VERBOSE_STR << "black point: " << opt_black_point << "%" << endl;
	VERBOSE_STR << "use fft pde solver: " << opt_fftsolver << endl;

	pfs::Array2DImpl* X = new pfs::Array2DImpl(3200, 2400);
	pfs::Array2DImpl* Y = new pfs::Array2DImpl(3200, 2400);
	pfs::Array2DImpl* Z = new pfs::Array2DImpl(3200, 2400);

	pfs::Array2DImpl* _R = new pfs::Array2DImpl(3200, 2400);
	pfs::Array2DImpl* _G = new pfs::Array2DImpl(3200, 2400);
	pfs::Array2DImpl* _B = new pfs::Array2DImpl(3200, 2400);

	OpenEXRReader reader("render_result_nm_ori_3.exr");

	reader.readImage(X, Y, Z);
	reader.readImage(_R, _G, _B);

	if( Y==NULL || X==NULL || Z==NULL)
		throw pfs::Exception( "Missing X, Y, Z channels in the PFS stream" );

	int w = Y->getCols();
	int h = Y->getRows();
	int pixelCount = w*h*3;
	float maxValue = (float)(1<<8) - 1;

	// save original png file
	unsigned char* oriBuffer = new unsigned char[pixelCount];
    int i = 0;
    for( int pix = 0; pix < w*h; pix++ ) {
        oriBuffer[i++] = (unsigned char)(clamp((*_R)(pix),0.0f,1.f)*maxValue);
        oriBuffer[i++] = (unsigned char)(clamp((*_G)(pix),0.0f,1.f)*maxValue);
        oriBuffer[i++] = (unsigned char)(clamp((*_B)(pix),0.0f,1.f)*maxValue);
    }

    Magick::Image oriImage(3200, 2400, "RGB", Magick::CharPixel, oriBuffer);
    oriImage.quality(100);
    oriImage.depth(8);
    oriImage.write("ori.png");

	// tone mapping
	pfs::Array2DImpl* L = new pfs::Array2DImpl(w,h);
	tmo_fattal02(w, h, Y->getRawData(), L->getRawData(), opt_alpha, opt_beta,
					opt_gamma, opt_noise, opt_detail_level,
					opt_black_point, opt_white_point, opt_fftsolver);

	// in-place color space transform
	pfs::Array2DImpl *G = new pfs::Array2DImpl( w, h ); // copy for G to preserve Y
	pfs::Array2D *R = X, *B = Z;

	pfs::transformColorSpace( pfs::CS_XYZ, X, Y, Z, pfs::CS_RGB, R, G, B );
		
	// Color correction
	float sum = 0;
	for(i=0; i < w*h; i++ )
	{
		const float epsilon = 1e-4f;
		float y = max( (*Y)(i), epsilon );
		float l = max( (*L)(i), epsilon );
		sum += l;

		(*R)(i) = powf( max((*R)(i)/y,0.f), opt_saturation ) * l;
		(*G)(i) = powf( max((*G)(i)/y,0.f), opt_saturation ) * l;
		(*B)(i) = powf( max((*B)(i)/y,0.f), opt_saturation ) * l;
	}

	pfs::transformColorSpace( pfs::CS_RGB, R, G, B, pfs::CS_XYZ, X, Y, Z );

	float mean = sum / (w*h);
	cout << "Sum: " << sum << endl;
	cout << "Mean: " << mean << endl;

	// save tone mapping png file
    unsigned char* mapBuffer = new unsigned char[pixelCount];
    i = 0;
    for( int pix = 0; pix < w*h; pix++ ) {
        mapBuffer[i++] = (unsigned char)(clamp((*X)(pix),0.0f,1.f)*maxValue);
        mapBuffer[i++] = (unsigned char)(clamp((*Y)(pix),0.0f,1.f)*maxValue);
        mapBuffer[i++] = (unsigned char)(clamp((*Z)(pix),0.0f,1.f)*maxValue);
    }

    Magick::Image mapImage(3200, 2400, "RGB", Magick::CharPixel, mapBuffer);
    mapImage.quality(100);
    mapImage.depth(8);
    mapImage.write("map.png");

    unsigned char* simpleBuffer = new unsigned char[pixelCount];
    i = 0;

    for( int pix = 0; pix < w*h; pix++ ) {
    	Vec3f origin((*_R)(pix), (*_G)(pix), (*_B)(pix));
    	Vec3f simple = simpleTonemapping(origin);

        simpleBuffer[i++] = (unsigned char)(clamp(simple[0],0.0f,1.f)*maxValue);
        simpleBuffer[i++] = (unsigned char)(clamp(simple[1],0.0f,1.f)*maxValue);
        simpleBuffer[i++] = (unsigned char)(clamp(simple[2],0.0f,1.f)*maxValue);
    }

    Magick::Image simpleImage(3200, 2400, "RGB", Magick::CharPixel, simpleBuffer);
    simpleImage.modulate(100, 115, 100);
	simpleImage.level(0, 65535 * 0.51, 1.0);
    simpleImage.quality(100);
    simpleImage.depth(8);
    simpleImage.write("simple.png");

    simpleImage.opacity(65535 * 0.7);
    mapImage.opacity(65535 * 0.35);

    simpleImage.composite(mapImage, 0, 0, Magick::CompositeOperator::MultiplyCompositeOp);
	simpleImage.write("fusion.jpg");

	/*Mat resultMat(2400, 3200, CV_8UC3, imgBuffer);
	//imwrite("output.png", resultMat);

	unsigned char* simpleBuffer = new unsigned char[pixelCount];

	i = 0;
	for (int pix = 0; pix < w*h; pix++) {
		simpleBuffer[i++] = (*X)(pix);
		simpleBuffer[i++] = (*Y)(pix);
		simpleBuffer[i++] = (*Z)(pix);
	}

	Mat initMat(2400, 3200, CV_32FC3, initImageBuffer);
	Mat initMatOut(initMat.size(), CV_8UC3);

	for(int i = 0; i < initMat.rows; i++) {
		for(int j = 0; j < initMat.cols; j++) {
			Vec3f current =  initMat.at<Vec3f>(i,j);
			initMatOut.at<Vec3b>(i,j) = Vec3b(clamp(current[2], 0.0f, 1.0f) * 255, clamp(current[1], 0.0f, 1.0f) * 255, clamp(current[0], 0.0f, 1.0f) * 255);
		}
	}
	//imshow("simple-before", initMatOut);
	imwrite("simple-before.png", initMatOut);
	//resize(initMat, initMat, Size(800, 600));
	//imshow("output", initMat);

	for(int i = 0; i < initMat.rows; i++) {
		for(int j = 0; j < initMat.cols; j++) {
			Vec3f current = initMat.at<Vec3f>(i,j);
			initMat.at<Vec3f>(i,j) = simpleTonemapping(current, 1.0f);
		}
	}

	for(int i = 0; i < initMat.rows; i++) {
		for(int j = 0; j < initMat.cols; j++) {
			Vec3f current =  initMat.at<Vec3f>(i,j);

			initMatOut.at<Vec3b>(i,j) = Vec3b(clamp(current[2] * 1.5f, 0.0f, 1.0f) * 255, clamp(current[1] * 1.5f, 0.0f, 1.0f) * 255, clamp(current[0] * 1.5f, 0.0f, 1.0f) * 255);
		}
	}
	//imshow("simple-after", initMat);

	imwrite("simple-after.png", initMatOut);

	Mat merge = resultMat * 0.3 + initMatOut * 0.75;
	imwrite("merge.png", initMatOut);

	waitKey(0);*/
    /*delete[] imgBuffer;
	delete X;
	delete Y;
	delete Z;
	delete G;    
	delete L;*/
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

int main( int argc, char* argv[] )
{
	try {
		pfstmo_fattal02( argc, argv );
	}
	catch( pfs::Exception ex ) {
		fprintf( stderr, PROG_NAME " error: %s\n", ex.getMessage() );
		return EXIT_FAILURE;
	}        
	catch( QuietException  ex ) {
		return EXIT_FAILURE;
	}        
	return EXIT_SUCCESS;
}
