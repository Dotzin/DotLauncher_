#ifndef DOTLAUNCHER_H
#define DOTLAUNCHER_H

#include <QMainWindow>

QT_BEGIN_NAMESPACE
namespace Ui {
class dotLauncher;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private:
    Ui::dotLauncher *ui;
};
#endif // DOTLAUNCHER_H
