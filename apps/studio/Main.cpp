#include "platform/WinDiagnostics.h"
#include "storage/FileIO.h"
#include <iostream>
#include <vector>
#ifdef _WIN32
// Windows SDK extension headers require windows.h first.
// clang-format off
#include <windows.h>
#include <shellapi.h>
// clang-format on
#endif
#ifdef DRAWBORAD_HAS_QT
#include "QtBridge.h"
#include "OriginalReferenceProvider.h"
#include "platform/CanvasItem.h"
#include <qqml.h>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTimer>
#include <d3d11.h>
#include <dxgi.h>
#include <iomanip>
#include <sstream>
#endif
using namespace drawborad;
static int writeReport(const std::filesystem::path& p,const DeviceReport& r){
    auto json=diagnosticJson(r);if(p.empty()){std::cout<<json<<'\n';return 0;}NativeFileIO io;
    auto saved=atomicWrite(io,p,{reinterpret_cast<const std::uint8_t*>(json.data()),json.size()});return saved?0:2;
}
static std::vector<std::string> arguments(int argc,char**argv){
    std::vector<std::string> result;
#ifdef _WIN32
    (void)argc;(void)argv;int count=0;auto wide=CommandLineToArgvW(GetCommandLineW(),&count);if(!wide)return result;
    for(int i=0;i<count;++i){int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide[i],-1,nullptr,0,nullptr,nullptr);if(n<=0){LocalFree(wide);return{};}std::string v(static_cast<std::size_t>(n),'\0');WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide[i],-1,v.data(),n,nullptr,nullptr);v.pop_back();result.push_back(std::move(v));}
    LocalFree(wide);
#else
    for(int i=0;i<argc;++i)result.emplace_back(argv[i]);
#endif
    return result;
}
int main(int argc,char**argv){
    std::filesystem::path path;bool smoke=false,warp=false;auto args=arguments(argc,argv);if(args.empty())return 2;
    for(std::size_t i=1;i<args.size();i++){std::string a=args[i];if(a=="--diagnostics"&&i+1<args.size()){const auto&value=args[++i];path=std::filesystem::path(reinterpret_cast<const char8_t*>(value.c_str()));}else if(a=="--smoke")smoke=true;else if(a=="--warp")warp=true;else{std::cerr<<"Usage: --diagnostics path [--smoke] [--warp]\n";return 2;}}
#ifdef DRAWBORAD_HAS_QT
    if(!smoke&&!path.empty())return writeReport(path,collectDeviceReport());
    if(warp)qputenv("QSG_RHI_PREFER_SOFTWARE_RENDERER","1");
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
    QGuiApplication app(argc,argv);app.setApplicationName("Drawborad");
    qmlRegisterType<CanvasItem>("Drawborad.Native",1,0,"NativeCanvas");
    QtBridge bridge;QQmlApplicationEngine engine;engine.addImageProvider("original-reference",new OriginalReferenceProvider(bridge));engine.rootContext()->setContextProperty("studio",&bridge);engine.loadFromModule("Drawborad","Main");if(engine.rootObjects().isEmpty())return 2;
    auto window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());if(!window)return 2;
    if(smoke){
        auto count=std::make_shared<int>(0);
        QObject::connect(window,&QQuickWindow::frameSwapped,window,[&,count]{
            if(++*count!=2)return;std::optional<std::string> active;auto iface=window->rendererInterface();auto raw=static_cast<ID3D11Device*>(iface->getResource(window,QSGRendererInterface::DeviceResource));
            if(raw){IDXGIDevice*device=nullptr;if(SUCCEEDED(raw->QueryInterface(__uuidof(IDXGIDevice),reinterpret_cast<void**>(&device)))){IDXGIAdapter*adapter=nullptr;if(SUCCEEDED(device->GetAdapter(&adapter))){DXGI_ADAPTER_DESC desc{};if(SUCCEEDED(adapter->GetDesc(&desc))){std::ostringstream s;s<<std::hex<<std::setfill('0')<<std::setw(8)<<std::uint32_t(desc.AdapterLuid.HighPart)<<std::setw(8)<<desc.AdapterLuid.LowPart;active=s.str();}adapter->Release();}device->Release();}}
            QMetaObject::invokeMethod(&app,[&,active]{auto r=collectDeviceReport();r.activeRenderLuid=active;r.sceneGraph="Direct3D11";int code=writeReport(path,r);auto canvas=window->findChild<QQuickItem*>("nativeCanvas");if(!active||!r.moduleScanCompleted||!r.webviewModules.empty()||window->width()!=1280||window->height()!=800||!canvas||canvas->width()<600||canvas->height()<500)code=1;app.exit(code);},Qt::QueuedConnection);
        },Qt::DirectConnection);
        QTimer::singleShot(30000,&app,[&]{app.exit(3);});QTimer::singleShot(100,window,[window]{window->update();});
    }
    return app.exec();
#else
    (void)warp;if(smoke){std::cerr<<"BLOCKED: Windows Qt/D3D11 not built\n";return 77;}return writeReport(path,collectDeviceReport());
#endif
}

