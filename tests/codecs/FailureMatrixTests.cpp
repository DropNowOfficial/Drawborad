#include "Test.h"
#include "codecs/CodecLimits.h"
#include "storage/FileIO.h"
#include <filesystem>
using namespace drawborad;
TEST("C07 codec limits scale from resource budget"){ResourceBudget b;b.ramSoftBytes=8*GiB;b.scratchBytes=6*GiB;auto l=defaultCodecLimits(b);CHECK(l.decodedBytes==4*GiB);CHECK(l.encodedBytes==3*GiB);}
TEST("C07 confined path rejects traversal"){auto root=std::filesystem::temp_directory_path()/"drawborad-safe";CHECK(!confinedPath(root,"../escape.bin"));}
