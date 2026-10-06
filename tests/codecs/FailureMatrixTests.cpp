#include "Test.h"
#include "codecs/CodecLimits.h"
#include "storage/FileIO.h"
#include <filesystem>
#include <fstream>
#include <iterator>
using namespace drawborad;
TEST("C07 codec limits scale from resource budget"){ResourceBudget b;b.ramSoftBytes=8*GiB;b.scratchBytes=6*GiB;auto l=defaultCodecLimits(b);CHECK(l.decodedBytes==4*GiB);CHECK(l.encodedBytes==3*GiB);}
TEST("C07 confined path rejects traversal"){auto root=std::filesystem::temp_directory_path()/"drawborad-safe";CHECK(!confinedPath(root,"../escape.bin"));}

#include "codecs/AtomicExport.h"

namespace {
class ReplaceFailIO final : public FileIO {
  public:
    Bytes staged;
    Bytes destination{9,8,7,6};
    bool replaceCalled{};
    Result<Bytes> read(const ProjectPath&, std::uint64_t maxBytes) const override {
        if (staged.size() > maxBytes)
            return error(ErrorCode::BudgetExceeded, "test.read-limit");
        return staged;
    }
    Result<void> write(const ProjectPath&, std::span<const std::uint8_t>, bool) override {
        return error(ErrorCode::IoError, "test.unexpected-write");
    }
    Result<void> flush(const ProjectPath&) override { return {}; }
    Result<void> replace(const ProjectPath&, const ProjectPath&) override {
        replaceCalled = true;
        return error(ErrorCode::IoError, "test.replace-failed");
    }
};
}

TEST("C07 failed atomic replace leaves existing destination unchanged") {
    ReplaceFailIO io;
    io.staged={1,2,3,4,5};
    const auto before=io.destination;
    StagedExport staged{"safe/output.tmp",sha256(io.staged)};
    DestinationPolicy policy{"safe/output.psd",true,true};
    auto result=commitExport(io,staged,policy,1024);
    CHECK(!result);
    CHECK(io.replaceCalled);
    CHECK(io.destination==before);
}

TEST("C07 staged hash mismatch never attempts replace") {
    ReplaceFailIO io;
    io.staged={1,2,3};
    StagedExport staged{"safe/output.tmp",Hash{}};
    DestinationPolicy policy{"safe/output.psd",true,true};
    auto result=commitExport(io,staged,policy,1024);
    CHECK(!result);
    CHECK(result.error().code==ErrorCode::DigestMismatch);
    CHECK(!io.replaceCalled);
}

namespace {
class DestinationAppearsDuringVerifyIO final : public FileIO {
  public:
    ProjectPath stagedPath;
    ProjectPath destinationPath;
    Bytes staged{1,2,3,4,5};
    Bytes intruder{91,92,93,94};
    mutable bool injected{};
    bool replaceCalled{};

    Result<Bytes> read(const ProjectPath& p, std::uint64_t maxBytes) const override {
        if (p != stagedPath)
            return error(ErrorCode::IoError, "test.unexpected-read");
        if (staged.size() > maxBytes)
            return error(ErrorCode::BudgetExceeded, "test.read-limit");
        if (!injected) {
            std::ofstream out(destinationPath, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(intruder.data()),
                      std::streamsize(intruder.size()));
            injected = true;
        }
        return staged;
    }
    Result<void> write(const ProjectPath&, std::span<const std::uint8_t>, bool) override {
        return error(ErrorCode::IoError, "test.unexpected-write");
    }
    Result<void> flush(const ProjectPath&) override { return {}; }
    Result<void> replace(const ProjectPath&, const ProjectPath& destination) override {
        replaceCalled = true;
        std::ofstream out(destination, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(staged.data()),
                  std::streamsize(staged.size()));
        return {};
    }
};
}

TEST("C07 destination appearing during verification is never overwritten without consent") {
    auto root=std::filesystem::temp_directory_path()/"drawborad-c07-race";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    DestinationAppearsDuringVerifyIO io;
    io.stagedPath=root/"output.tmp";
    io.destinationPath=root/"output.psd";
    StagedExport staged{io.stagedPath,sha256(io.staged)};
    DestinationPolicy policy{io.destinationPath,false,true};
    auto result=commitExport(io,staged,policy,1024);
    CHECK(!result);
    CHECK(!io.replaceCalled);
    std::ifstream in(io.destinationPath,std::ios::binary);
    Bytes actual((std::istreambuf_iterator<char>(in)),{});
    CHECK(actual==io.intruder);
    in.close();
    std::filesystem::remove_all(root);
}

namespace {
class NativeDestinationAppearsIO final : public NativeFileIO {
  public:
    ProjectPath destinationPath;
    Bytes intruder{41,42,43,44};
    mutable bool injected{};

    Result<Hash> hash(const ProjectPath& p, std::uint64_t maxBytes) const override {
        if(!injected){
            NativeFileIO writer;
            auto written=writer.write(destinationPath,intruder,true);
            if(!written) return written.error();
            injected=true;
        }
        return NativeFileIO::hash(p,maxBytes);
    }
};
}

TEST("C07 native no-clobber publish rejects destination created after precheck") {
    auto root=std::filesystem::temp_directory_path()/"drawborad-c07-native-race";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    auto stagedPath=root/"output.tmp";
    auto destinationPath=root/"output.psd";
    Bytes stagedBytes{1,3,5,7,9};
    NativeFileIO setup;
    CHECK(setup.write(stagedPath,stagedBytes,true));
    CHECK(setup.flush(stagedPath));

    NativeDestinationAppearsIO io;
    io.destinationPath=destinationPath;
    StagedExport staged{stagedPath,sha256(stagedBytes)};
    DestinationPolicy policy{destinationPath,false,true};
    auto result=commitExport(io,staged,policy,1024);
    CHECK(!result);
    CHECK(std::filesystem::exists(stagedPath));
    auto actual=setup.read(destinationPath,1024);
    CHECK(actual);
    CHECK(actual.value()==io.intruder);
    std::filesystem::remove_all(root);
}


TEST("C07 native staged hash streams across multiple chunks") {
    auto path=std::filesystem::temp_directory_path()/"drawborad-c07-hash-stream.bin";
    Bytes bytes(3*1024*1024+17);
    for(std::size_t i=0;i<bytes.size();++i) bytes[i]=std::uint8_t((i*37u+11u)&0xffu);
    NativeFileIO io;
    CHECK(io.write(path,bytes,true));
    auto streamed=io.hash(path,4*1024*1024);
    CHECK(streamed);
    CHECK(streamed.value()==sha256(bytes));
    std::filesystem::remove(path);
}
