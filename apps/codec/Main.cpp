#include "codecs/ImageReader.h"
#include <iostream>
int main(int argc,char**argv){if(argc!=2)return 2;drawborad::ResourceBudget b;auto r=drawborad::preflightInput(argv[1],b);if(!r){std::cerr<<r.error().messageKey<<"\n";return 1;}std::cout<<r.value().originalSize.width<<"x"<<r.value().originalSize.height<<"\n";return 0;}
