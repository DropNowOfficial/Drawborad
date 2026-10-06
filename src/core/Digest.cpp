#include "Digest.h"
#include <algorithm>
#include <bit>
#include <cstring>
namespace drawborad {
namespace {
constexpr std::array<std::uint32_t,64> K={
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,
    0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,
    0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,
    0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,
    0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,
    0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,
    0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,0x19a4c116,
    0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,
    0xc67178f2};
}
void Sha256State::block(const std::uint8_t* p){
    std::array<std::uint32_t,64> w{};
    for(int i=0;i<16;i++)
        w[i]=(std::uint32_t(p[i*4])<<24)|(std::uint32_t(p[i*4+1])<<16)|
             (std::uint32_t(p[i*4+2])<<8)|p[i*4+3];
    for(int i=16;i<64;i++){
        const auto a=w[i-15],b=w[i-2];
        w[i]=w[i-16]+(std::rotr(a,7)^std::rotr(a,18)^(a>>3))+w[i-7]+
             (std::rotr(b,17)^std::rotr(b,19)^(b>>10));
    }
    auto [a,b,c,d,e,f,g,z]=h_;
    for(int i=0;i<64;i++){
        const auto t=z+(std::rotr(e,6)^std::rotr(e,11)^std::rotr(e,25))+
                     ((e&f)^(~e&g))+K[i]+w[i];
        const auto u=(std::rotr(a,2)^std::rotr(a,13)^std::rotr(a,22))+
                     ((a&b)^(a&c)^(b&c));
        z=g;g=f;f=e;e=d+t;d=c;c=b;b=a;a=t+u;
    }
    h_[0]+=a;h_[1]+=b;h_[2]+=c;h_[3]+=d;h_[4]+=e;h_[5]+=f;h_[6]+=g;h_[7]+=z;
}
void Sha256State::update(std::span<const std::uint8_t> input){
    totalBytes_+=std::uint64_t(input.size());
    std::size_t pos=0;
    if(buffered_){
        const auto take=std::min<std::size_t>(64-buffered_,input.size());
        std::memcpy(buffer_.data()+buffered_,input.data(),take);
        buffered_+=take;pos+=take;
        if(buffered_==64){block(buffer_.data());buffered_=0;}
    }
    while(input.size()-pos>=64){block(input.data()+pos);pos+=64;}
    const auto remain=input.size()-pos;
    if(remain){std::memcpy(buffer_.data(),input.data()+pos,remain);buffered_=remain;}
}
Hash Sha256State::finish() const{
    auto state=*this;
    const auto bits=state.totalBytes_*8;
    state.buffer_[state.buffered_++]=0x80;
    if(state.buffered_>56){
        std::fill(state.buffer_.begin()+std::ptrdiff_t(state.buffered_),state.buffer_.end(),0);
        state.block(state.buffer_.data());
        state.buffered_=0;
    }
    std::fill(state.buffer_.begin()+std::ptrdiff_t(state.buffered_),
              state.buffer_.begin()+56,0);
    for(int i=0;i<8;i++)state.buffer_[63-i]=std::uint8_t(bits>>(8*i));
    state.block(state.buffer_.data());
    Hash out;
    for(int i=0;i<8;i++)for(int j=0;j<4;j++)
        out.bytes[i*4+j]=std::uint8_t(state.h_[i]>>(24-j*8));
    return out;
}
Hash sha256(std::span<const std::uint8_t> input){
    Sha256State state;state.update(input);return state.finish();
}
}
