#ifndef DOTLAUNCHER_H
#define DOTLAUNCHER_H

#include <QIcon>
#include <QMainWindow>

QT_BEGIN_NAMESPACE
namespace Ui {
class dotLauncher;
}
QT_END_NAMESPACE

class QLayout;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void openAddSoftwareDialog();

private:
    bool saveSoftwareEntry(const QString &name, const QString &exePath, const QIcon &icon, QString *errorMessage = nullptr);
    QString dataDirectory() const;
    void loadSoftwareEntries();
    void clearLayout(QLayout *layout);

    Ui::dotLauncher *ui;
};
#endif // DOTLAUNCHER_H
