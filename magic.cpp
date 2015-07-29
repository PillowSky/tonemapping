#include <Magick++.h> 
using namespace std; 
using namespace Magick;

int main(int argc,char **argv) { 
	InitializeMagick(*argv);

	Image image("simple.png");
	Image image2(image);
	image.oilPaint();

	image.display();
	image2.display();

	return 0;
}