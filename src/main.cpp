#include <QApplication>
#include "ui/MainWindow.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    
    // 初始化主窗口
    MainWindow mainWindow;
    mainWindow.show();
    
    return app.exec();
}
