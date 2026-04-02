#ifndef DOTLAUNCHER_H
#define DOTLAUNCHER_H

#include <QIcon>
#include <QList>
#include <QMainWindow>
#include <QString>
#include <QStringList>

QT_BEGIN_NAMESPACE
namespace Ui {
class dotLauncher;
}
QT_END_NAMESPACE

class QLayout;
class QFrame;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void openAddSoftwareDialog();

private:
    struct SoftwareEntry {
        QString name;
        QString exePath;
        QString iconPath;
    };

    bool saveSoftwareEntry(const QString &name, const QString &exePath, const QIcon &icon, QString *errorMessage = nullptr);
    bool appendSoftwareEntry(const SoftwareEntry &entry, QString *errorMessage = nullptr);
    bool readSoftwareEntries(QList<SoftwareEntry> *entries, QString *errorMessage = nullptr) const;
    bool writeSoftwareEntries(const QList<SoftwareEntry> &entries, QString *errorMessage = nullptr) const;
    bool removeSoftwareEntry(const SoftwareEntry &entry, QString *errorMessage = nullptr);
    QString dataDirectory() const;
    QString jsonFilePath() const;
    QString iconsDirectoryPath() const;
    bool ensureDirectory(const QString &path, const QString &failureMessage, QString *errorMessage) const;
    bool saveIconFile(const QIcon &icon, QString *relativePath, QString *errorMessage) const;
    QString resolveIconPath(const QString &iconValue, const QString &baseDirPath) const;
    bool deleteIconIfLocal(const SoftwareEntry &entry, const QString &baseDirPath, QString *errorMessage = nullptr) const;
    QFrame *createSoftwareCard(const SoftwareEntry &entry, const QString &baseDirPath);
    void setupCardSizeControls();
    void handleCardSizeChanged(int value);
    int cardWidth() const;
    int cardHeight() const;
    int iconSize() const;
    int calculateColumnCount(int cardWidth) const;
    void updateCardSizeLabel(int value);
    void configureGridColumns(int columns);
    void loadSoftwareEntries();
    void clearLayout(QLayout *layout);

    int m_cardWidth = 140;
    Ui::dotLauncher *ui;
};
#endif // DOTLAUNCHER_H
