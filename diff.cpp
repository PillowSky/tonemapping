#include <iostream>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>

using namespace std;
using namespace cv;

double getMSE(Mat I1, Mat I2) {
	Mat s1;
	absdiff(I1, I2, s1);       // |I1 - I2|
	s1.convertTo(s1, CV_32F);  // cannot make a square on 8 bits
	s1 = s1.mul(s1);           // |I1 - I2|^2

	Scalar s = sum(s1);        // sum elements per channel

	double sse = s.val[0] + s.val[1] + s.val[2]; // sum channels


    double mse  = sse / (double)(I1.channels() * I1.total());

   	return mse;

 }

int main () {
	Mat image1 = imread("fusion.jpg");
	Mat image2 = imread("dahuang.png");

	resize(image1, image1, Size(800, 600));
	resize(image2, image2, Size(800, 600));

	Mat diff = image1- image2;
	cout << "MSE: " << getMSE(image2, image1) << endl;
	imshow("Image1", image1);
	imshow("Image2", image2);
	imshow("Diff", diff);

	cv::waitKey(0);
}