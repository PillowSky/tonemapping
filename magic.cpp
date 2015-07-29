#include <Magick++.h> 
using namespace std; 
using namespace Magick;

int main(int argc,char **argv) { 
	InitializeMagick(*argv);
	Image image;
	image.read("simple.png");
    image.depth(8);
    
	//image.modulate(100, 115, 100);
    image.level(0, 65535 * 0.51, 1.0);
	image.display();
	return 0;
}