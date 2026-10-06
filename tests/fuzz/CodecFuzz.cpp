#include "Test.h"
#include "codecs/psd/PsdReader.h"
#include <algorithm>
#include <cstdint>
using namespace drawborad;
namespace {
void put16(Bytes& b,std::uint16_t v){b.push_back(std::uint8_t(v>>8));b.push_back(std::uint8_t(v));}
void put32(Bytes& b,std::uint32_t v){b.push_back(std::uint8_t(v>>24));b.push_back(std::uint8_t(v>>16));b.push_back(std::uint8_t(v>>8));b.push_back(std::uint8_t(v));}
Bytes seed(){
    Bytes b{'8','B','P','S'};put16(b,1);b.insert(b.end(),6,0);put16(b,4);
    put32(b,8);put32(b,8);put16(b,8);put16(b,3);
    put32(b,0);put32(b,0);put32(b,0);put16(b,0);
    return b;
}
std::uint64_t next(std::uint64_t& x){x^=x<<13;x^=x>>7;x^=x<<17;return x;}
}
TEST("C07 fuzz bounded PSD parser and decoder across deterministic corruptions"){
    CodecLimits limits;
    limits.decodedBytes=8*1024*1024;
    limits.encodedBytes=8*1024*1024;
    limits.nodes=1024;
    auto good=seed();
    std::uint64_t rng=0xC07F00D5A17Eull;
    for(int iteration=0;iteration<12000;++iteration){
        auto b=good;
        const int edits=1+int(next(rng)%12);
        for(int k=0;k<edits;++k){
            if(b.empty()) break;
            const auto action=next(rng)%5;
            if(action==0 && b.size()>1){
                b.resize(std::size_t(1+next(rng)%b.size()));
            }else if(action==1 && b.size()<4096){
                const auto add=std::size_t(1+next(rng)%32);
                b.insert(b.end(),add,std::uint8_t(next(rng)));
            }else{
                const auto at=std::size_t(next(rng)%b.size());
                b[at]=std::uint8_t(next(rng));
            }
        }
        auto header=readPhotoshopHeader(b,limits);
        if(header){
            CHECK(header.value().size.width>0);
            CHECK(header.value().size.height>0);
            CHECK(header.value().layers.size()<=limits.nodes);
            auto decoded=decodePhotoshopLayers(b,ProfileId{},limits);
            if(decoded){
                CHECK(decoded.value().info.size==header.value().size);
                CHECK(decoded.value().layers.size()<=limits.nodes);
            }
        }
    }
}
