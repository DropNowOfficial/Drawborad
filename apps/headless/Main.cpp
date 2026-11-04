#include <QCoreApplication>
#include <QTextStream>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    QTextStream err(stderr);
    const auto args = app.arguments();
    if (args.size() == 2 && (args[1] == "--help" || args[1] == "-h")) {
        out << "DrawboradHeadless\n"
               "Usage:\n"
               "  DrawboradHeadless run --manifest PATH --output DIR --accept-all\n"
               "  DrawboradHeadless smoke --output DIR\n";
        return 0;
    }
    err << "Invalid arguments. Run DrawboradHeadless --help.\n";
    return 2;
}
