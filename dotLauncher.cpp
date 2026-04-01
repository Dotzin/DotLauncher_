#include "dotLauncher.h"
#include "ui_dotLauncher.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::dotLauncher)
{
    ui->setupUi(this);
}

MainWindow::~MainWindow()
{
    delete ui;
}
