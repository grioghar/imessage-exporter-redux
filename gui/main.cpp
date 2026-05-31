// Entry point for the Qt desktop app. The window does all the work; this just
// boots Qt. On macOS CMake bundles this as "iMessage Exporter.app".
#include <QApplication>
#include <QIcon>

#include "main_window.hpp"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("iMessage Exporter");
    QApplication::setOrganizationName("imessage-exporter");
    QApplication::setWindowIcon(QIcon(":/icon.svg"));

    MainWindow window;
    window.resize(560, 640);
    window.show();
    return app.exec();
}
