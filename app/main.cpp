#include "app/MainWindow.h"

#include <QApplication>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    pp::MainWindow w;
    w.show();
    return app.exec();
}

