#include "Test.h"
#include "codecs/ImageReader.h"
#include <filesystem>
#include <fstream>
using namespace drawborad;
namespace { std::filesystem::path put(std::string n,const Bytes&b){auto p=std::filesystem::temp_directory_path()/n;std::ofstream f(p,std::ios::binary);f.write(reinterpret_cast<const char*>(b.data()),std::streamsize(b.size()));return p;} }
TEST("C03 PNG preflight reads dimensions without decoding allocation"){Bytes b(24,0);const std::uint8_t s[8]={0x89,'P','N','G',13,10,26,10};std::copy(s,s+8,b.begin());b[12]='I';b[13]='H';b[14]='D';b[15]='R';b[19]=4;b[23]=3;auto p=put("drawborad-c03.png",b);ResourceBudget budget;auto i=preflightInput(p,budget);CHECK(i);CHECK(i.value().originalSize.width==4);CHECK(i.value().originalSize.height==3);std::filesystem::remove(p);}
TEST("C03 huge declared image is rejected by budget before decode"){Bytes b(24,0);const std::uint8_t s[8]={0x89,'P','N','G',13,10,26,10};std::copy(s,s+8,b.begin());b[12]='I';b[13]='H';b[14]='D';b[15]='R';b[16]=0x7f;b[17]=0xff;b[18]=0xff;b[19]=0xff;b[20]=0x7f;b[21]=0xff;b[22]=0xff;b[23]=0xff;auto p=put("drawborad-c03-huge.png",b);ResourceBudget budget;CHECK(!preflightInput(p,budget));std::filesystem::remove(p);}
