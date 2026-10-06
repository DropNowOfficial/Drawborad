#include "codecs/CodecProcessProtocol.h"
#include "codecs/ImageReader.h"
#include "storage/FileIO.h"
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

namespace {
std::vector<std::string> commandLine(int argc,char** argv) {
#ifdef _WIN32
    (void)argc;
    (void)argv;
    int count=0;
    auto wide=CommandLineToArgvW(GetCommandLineW(),&count);
    if(!wide) return {};
    std::vector<std::string> out;
    out.reserve(std::size_t(count));
    for(int i=0;i<count;++i) {
        const int bytes=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide[i],-1,
                                            nullptr,0,nullptr,nullptr);
        if(bytes<=0) {
            LocalFree(wide);
            return {};
        }
        std::string value(std::size_t(bytes),0);
        WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide[i],-1,
                            value.data(),bytes,nullptr,nullptr);
        value.pop_back();
        out.push_back(std::move(value));
    }
    LocalFree(wide);
    return out;
#else
    std::vector<std::string> out;
    out.reserve(std::size_t(argc));
    for(int i=0;i<argc;++i) out.emplace_back(argv[i]);
    return out;
#endif
}

std::filesystem::path pathFromUtf8(const std::string& value) {
#ifdef _WIN32
    return std::filesystem::path(
        std::u8string(reinterpret_cast<const char8_t*>(value.data()),
                      reinterpret_cast<const char8_t*>(value.data()+value.size())));
#else
    return std::filesystem::path(value);
#endif
}
}

int main(int argc,char** argv) {
    using namespace drawborad;
    const auto args=commandLine(argc,argv);
    if(args.size()!=4 || args[1]!="--decode") {
        std::cerr<<"usage: DrawboradCodec --decode INPUT OUTPUT\n";
        return 2;
    }

    const auto input=pathFromUtf8(args[2]);
    const auto output=pathFromUtf8(args[3]);
    ResourceBudget budget;
    DecodeOptions options;
    options.limits=defaultCodecLimits(budget);

    auto decoded=decodeImage(input,options);
    if(!decoded) {
        std::cerr<<decoded.error().messageKey<<"\n";
        return 1;
    }
    if(!decoded.value().decodedPixels) {
        std::cerr<<"codec.worker-format-not-raster\n";
        return 1;
    }

    CodecPayload payload{decoded.value().info,*decoded.value().decodedPixels};
    auto encoded=encodeCodecPayload(payload,options.limits);
    if(!encoded) {
        std::cerr<<encoded.error().messageKey<<"\n";
        return 1;
    }

    NativeFileIO io;
    auto written=atomicWrite(io,output,encoded.value());
    if(!written) {
        std::cerr<<written.error().messageKey<<"\n";
        return 1;
    }
    return 0;
}
