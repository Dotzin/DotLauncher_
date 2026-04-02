#include "dotLauncher.h"
#include "ui_dotLauncher.h"

#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QPixmap>
#include <QPushButton>
#include <QSize>
#include <QSizePolicy>
#include <QStandardPaths>
#include <QUuid>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::dotLauncher)
{
    ui->setupUi(this);

    connect(ui->pushButton, &QPushButton::clicked, this, &MainWindow::openAddSoftwareDialog);

    loadSoftwareEntries();
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::openAddSoftwareDialog()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Adicionar software"));

    auto *mainLayout = new QVBoxLayout(&dialog);

    auto *formLayout = new QFormLayout();
    auto *nameEdit = new QLineEdit(&dialog);
    nameEdit->setPlaceholderText(tr("Ex.: Chrome, VS Code"));
    formLayout->addRow(tr("Nome do software:"), nameEdit);

    auto *exePathEdit = new QLineEdit(&dialog);
    exePathEdit->setReadOnly(true);

    auto *chooseExeButton = new QPushButton(tr("Escolher .exe"), &dialog);

    auto *exeRowWidget = new QWidget(&dialog);
    auto *exeRowLayout = new QHBoxLayout(exeRowWidget);
    exeRowLayout->setContentsMargins(0, 0, 0, 0);
    exeRowLayout->setSpacing(8);
    exeRowLayout->addWidget(exePathEdit);
    exeRowLayout->addWidget(chooseExeButton);

    formLayout->addRow(tr("Arquivo .exe:"), exeRowWidget);

    auto *iconPreview = new QLabel(&dialog);
    iconPreview->setFixedSize(96, 96);
    iconPreview->setFrameShape(QFrame::Box);
    iconPreview->setAlignment(Qt::AlignCenter);
    iconPreview->setText(tr("Sem icone"));

    formLayout->addRow(tr("Icone (auto):"), iconPreview);

    mainLayout->addLayout(formLayout);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttonBox->button(QDialogButtonBox::Ok)->setText(tr("Salvar"));
    buttonBox->button(QDialogButtonBox::Cancel)->setText(tr("Cancelar"));
    mainLayout->addWidget(buttonBox);

    QString selectedExePath;
    QIcon selectedIcon;

    connect(chooseExeButton, &QPushButton::clicked, &dialog, [&]() {
        const QString exePath = QFileDialog::getOpenFileName(
            &dialog,
            tr("Selecionar executavel"),
            QString(),
            tr("Executaveis (*.exe);;Todos os arquivos (*)"));

        if (exePath.isEmpty()) {
            return;
        }

        selectedExePath = exePath;
        exePathEdit->setText(exePath);

        if (nameEdit->text().trimmed().isEmpty()) {
            QFileInfo exeInfo(exePath);
            nameEdit->setText(exeInfo.completeBaseName());
        }

        QFileIconProvider iconProvider;
        QIcon icon = iconProvider.icon(QFileInfo(exePath));
        if (icon.isNull()) {
            icon = QIcon(exePath);
        }

        selectedIcon = icon;

        const QPixmap pixmap = icon.pixmap(iconPreview->size());
        if (pixmap.isNull()) {
            iconPreview->setText(tr("Sem preview"));
            iconPreview->setPixmap(QPixmap());
            return;
        }

        iconPreview->setPixmap(pixmap);
    });

    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, [&]() {
        const QString name = nameEdit->text().trimmed();
        if (name.isEmpty()) {
            QMessageBox::warning(&dialog, tr("Nome obrigatorio"), tr("Digite o nome do software."));
            return;
        }
        if (selectedExePath.isEmpty()) {
            QMessageBox::warning(&dialog, tr("Executavel obrigatorio"), tr("Selecione o arquivo .exe."));
            return;
        }
        if (selectedIcon.isNull()) {
            QMessageBox::warning(&dialog, tr("Icone indisponivel"), tr("Nao foi possivel obter o icone do .exe."));
            return;
        }

        QString errorMessage;
        if (!saveSoftwareEntry(name, selectedExePath, selectedIcon, &errorMessage)) {
            QMessageBox::critical(&dialog, tr("Erro ao salvar"), errorMessage);
            return;
        }

        dialog.accept();
    });

    if (dialog.exec() == QDialog::Accepted) {
        loadSoftwareEntries();
    }
}

bool MainWindow::saveSoftwareEntry(const QString &name,
                                   const QString &exePath,
                                   const QIcon &icon,
                                   QString *errorMessage)
{
    const QString baseDirPath = dataDirectory();
    if (baseDirPath.isEmpty()) {
        if (errorMessage) {
            *errorMessage = tr("Nao foi possivel obter a pasta de dados da aplicacao.");
        }
        return false;
    }

    QDir baseDir(baseDirPath);
    if (!baseDir.exists() && !baseDir.mkpath(".")) {
        if (errorMessage) {
            *errorMessage = tr("Nao foi possivel criar a pasta de dados.");
        }
        return false;
    }

    const QString iconsDirPath = baseDir.filePath("icons");
    QDir iconsDir(iconsDirPath);
    if (!iconsDir.exists() && !iconsDir.mkpath(".")) {
        if (errorMessage) {
            *errorMessage = tr("Nao foi possivel criar a pasta de icones.");
        }
        return false;
    }

    QString iconFileName = QUuid::createUuid().toString(QUuid::Id128) + ".png";

    const QString destinationIconPath = iconsDir.filePath(iconFileName);

    const QPixmap iconPixmap = icon.pixmap(128, 128);
    if (iconPixmap.isNull()) {
        if (errorMessage) {
            *errorMessage = tr("Nao foi possivel gerar o icone do executavel.");
        }
        return false;
    }

    if (!iconPixmap.save(destinationIconPath, "PNG")) {
        if (errorMessage) {
            *errorMessage = tr("Nao foi possivel salvar o icone.");
        }
        return false;
    }

    const QString jsonPath = baseDir.filePath("software.json");
    QJsonArray items;

    if (QFile::exists(jsonPath)) {
        QFile inputFile(jsonPath);
        if (!inputFile.open(QIODevice::ReadOnly)) {
            if (errorMessage) {
                *errorMessage = tr("Nao foi possivel ler o arquivo JSON.");
            }
            return false;
        }

        const QByteArray data = inputFile.readAll();
        inputFile.close();

        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            if (errorMessage) {
                *errorMessage = tr("JSON invalido: %1").arg(parseError.errorString());
            }
            return false;
        }

        if (doc.isArray()) {
            items = doc.array();
        } else if (doc.isObject()) {
            const QJsonObject root = doc.object();
            const QJsonValue stored = root.value(QStringLiteral("softwares"));
            if (stored.isArray()) {
                items = stored.toArray();
            }
        }
    }

    QJsonObject entry;
    entry.insert(QStringLiteral("name"), name);
    entry.insert(QStringLiteral("exePath"), exePath);
    entry.insert(QStringLiteral("icon"),
                 QDir(baseDirPath).relativeFilePath(destinationIconPath).replace("\\", "/"));
    items.append(entry);

    QJsonObject root;
    root.insert(QStringLiteral("softwares"), items);

    QFile outputFile(jsonPath);
    if (!outputFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage) {
            *errorMessage = tr("Nao foi possivel gravar o arquivo JSON.");
        }
        return false;
    }

    outputFile.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    outputFile.close();

    return true;
}

QString MainWindow::dataDirectory() const
{
    QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (path.isEmpty()) {
        path = QCoreApplication::applicationDirPath();
    }
    return path;
}

void MainWindow::loadSoftwareEntries()
{
    if (!ui || !ui->gridLayout) {
        return;
    }

    clearLayout(ui->gridLayout);

    const QString baseDirPath = dataDirectory();
    if (baseDirPath.isEmpty()) {
        return;
    }

    const QString jsonPath = QDir(baseDirPath).filePath("software.json");
    if (!QFile::exists(jsonPath)) {
        return;
    }

    QFile inputFile(jsonPath);
    if (!inputFile.open(QIODevice::ReadOnly)) {
        return;
    }

    const QByteArray data = inputFile.readAll();
    inputFile.close();

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return;
    }

    QJsonArray items;
    if (doc.isArray()) {
        items = doc.array();
    } else if (doc.isObject()) {
        const QJsonObject root = doc.object();
        const QJsonValue stored = root.value(QStringLiteral("softwares"));
        if (stored.isArray()) {
            items = stored.toArray();
        }
    }

    const int columns = 5;
    int index = 0;

    for (const QJsonValue &value : items) {
        if (!value.isObject()) {
            continue;
        }

        const QJsonObject obj = value.toObject();
        const QString name = obj.value(QStringLiteral("name")).toString().trimmed();
        const QString exePath = obj.value(QStringLiteral("exePath")).toString();
        const QString iconValue = obj.value(QStringLiteral("icon")).toString();

        if (name.isEmpty()) {
            continue;
        }

        auto *frame = new QFrame(ui->scrollAreaWidgetContents);
        frame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        frame->setMinimumSize(QSize(140, 200));
        frame->setFrameShape(QFrame::StyledPanel);
        frame->setFrameShadow(QFrame::Raised);

        auto *layout = new QVBoxLayout(frame);
        layout->setSpacing(6);
        layout->setContentsMargins(8, 8, 8, 8);

        auto *iconLabel = new QLabel(frame);
        iconLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        iconLabel->setMinimumSize(QSize(120, 120));
        iconLabel->setFrameShape(QFrame::Box);
        iconLabel->setAlignment(Qt::AlignCenter);
        iconLabel->setScaledContents(true);

        QString iconPath = iconValue;
        if (!iconValue.isEmpty() && QFileInfo(iconValue).isRelative()) {
            iconPath = QDir(baseDirPath).filePath(iconValue);
        }

        QPixmap pixmap(iconPath);
        if (pixmap.isNull()) {
            iconLabel->setText(tr("Sem icone"));
        } else {
            iconLabel->setPixmap(pixmap.scaled(iconLabel->size(),
                                               Qt::KeepAspectRatio,
                                               Qt::SmoothTransformation));
        }

        auto *titleLabel = new QLabel(name, frame);
        titleLabel->setAlignment(Qt::AlignCenter);

        auto *openButton = new QPushButton(tr("Abrir"), frame);

        layout->addWidget(iconLabel);
        layout->addWidget(titleLabel);
        layout->addWidget(openButton);

        connect(openButton, &QPushButton::clicked, this, [this, exePath]() {
            if (exePath.isEmpty() || !QFile::exists(exePath)) {
                QMessageBox::warning(this, tr("Executavel ausente"), tr("Caminho do .exe nao encontrado."));
                return;
            }

            if (!QProcess::startDetached(exePath)) {
                QMessageBox::warning(this, tr("Falha ao abrir"), tr("Nao foi possivel abrir o executavel."));
            }
        });

        const int row = index / columns;
        const int col = index % columns;
        ui->gridLayout->addWidget(frame, row, col);
        ++index;
    }

    for (int col = 0; col < columns; ++col) {
        ui->gridLayout->setColumnStretch(col, 1);
    }
}

void MainWindow::clearLayout(QLayout *layout)
{
    if (!layout) {
        return;
    }

    while (QLayoutItem *item = layout->takeAt(0)) {
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        if (item->layout()) {
            clearLayout(item->layout());
        }
        delete item;
    }
}
