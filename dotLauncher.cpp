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
#include <QMargins>
#include <QMessageBox>
#include <QProcess>
#include <QPixmap>
#include <QPushButton>
#include <QSlider>
#include <QSize>
#include <QSizePolicy>
#include <QStandardPaths>
#include <QStyle>
#include <QTimer>
#include <QtMath>
#include <QUuid>
#include <QVBoxLayout>

namespace {
constexpr int kBaseCardWidth = 140;
constexpr int kBaseCardHeight = 200;
constexpr int kBaseIconSize = 110;
constexpr int kDefaultColumnCount = 5;
constexpr int kMinCardWidth = 120;
constexpr int kMaxCardWidth = 220;
constexpr int kCardWidthStep = 5;
constexpr int kCardWidthPageStep = 10;
constexpr int kCardWidthTickInterval = 20;
} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::dotLauncher)
{
    ui->setupUi(this);

    setupCardSizeControls();

    connect(ui->pushButton, &QPushButton::clicked, this, &MainWindow::openAddSoftwareDialog);

    QTimer::singleShot(0, this, [this]() { loadSoftwareEntries(); });
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
    SoftwareEntry entry;
    entry.name = name;
    entry.exePath = exePath;

    if (!saveIconFile(icon, &entry.iconPath, errorMessage)) {
        return false;
    }

    return appendSoftwareEntry(entry, errorMessage);
}

bool MainWindow::appendSoftwareEntry(const SoftwareEntry &entry, QString *errorMessage)
{
    QList<SoftwareEntry> entries;
    if (!readSoftwareEntries(&entries, errorMessage)) {
        return false;
    }

    entries.append(entry);
    return writeSoftwareEntries(entries, errorMessage);
}

bool MainWindow::readSoftwareEntries(QList<SoftwareEntry> *entries, QString *errorMessage) const
{
    if (!entries) {
        return false;
    }

    entries->clear();

    const QString jsonPath = jsonFilePath();
    if (jsonPath.isEmpty()) {
        if (errorMessage) {
            *errorMessage = tr("Nao foi possivel obter a pasta de dados da aplicacao.");
        }
        return false;
    }
    if (!QFile::exists(jsonPath)) {
        return true;
    }

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

    for (const QJsonValue &value : items) {
        if (!value.isObject()) {
            continue;
        }

        const QJsonObject obj = value.toObject();
        SoftwareEntry entry;
        entry.name = obj.value(QStringLiteral("name")).toString().trimmed();
        entry.exePath = obj.value(QStringLiteral("exePath")).toString();
        entry.iconPath = obj.value(QStringLiteral("icon")).toString();

        if (entry.name.isEmpty()) {
            continue;
        }

        entries->append(entry);
    }

    return true;
}

bool MainWindow::writeSoftwareEntries(const QList<SoftwareEntry> &entries, QString *errorMessage) const
{
    const QString baseDirPath = dataDirectory();
    if (baseDirPath.isEmpty()) {
        if (errorMessage) {
            *errorMessage = tr("Nao foi possivel obter a pasta de dados da aplicacao.");
        }
        return false;
    }

    if (!ensureDirectory(baseDirPath, tr("Nao foi possivel criar a pasta de dados."), errorMessage)) {
        return false;
    }

    QJsonArray items;
    for (const SoftwareEntry &entry : entries) {
        if (entry.name.trimmed().isEmpty()) {
            continue;
        }

        QJsonObject obj;
        obj.insert(QStringLiteral("name"), entry.name);
        obj.insert(QStringLiteral("exePath"), entry.exePath);
        obj.insert(QStringLiteral("icon"), entry.iconPath);
        items.append(obj);
    }

    QJsonObject root;
    root.insert(QStringLiteral("softwares"), items);

    const QString jsonPath = jsonFilePath();
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

bool MainWindow::removeSoftwareEntry(const SoftwareEntry &entry, QString *errorMessage)
{
    QList<SoftwareEntry> entries;
    if (!readSoftwareEntries(&entries, errorMessage)) {
        return false;
    }

    int removeIndex = -1;
    for (int i = 0; i < entries.size(); ++i) {
        const SoftwareEntry &current = entries.at(i);
        if (current.name == entry.name
            && current.exePath == entry.exePath
            && current.iconPath == entry.iconPath) {
            removeIndex = i;
            break;
        }
    }

    if (removeIndex < 0) {
        return true;
    }

    entries.removeAt(removeIndex);
    if (!writeSoftwareEntries(entries, errorMessage)) {
        return false;
    }

    const QString baseDirPath = dataDirectory();
    if (!baseDirPath.isEmpty()) {
        deleteIconIfLocal(entry, baseDirPath, nullptr);
    }

    return true;
}

QString MainWindow::jsonFilePath() const
{
    const QString baseDirPath = dataDirectory();
    if (baseDirPath.isEmpty()) {
        return QString();
    }
    return QDir(baseDirPath).filePath("software.json");
}

QString MainWindow::iconsDirectoryPath() const
{
    const QString baseDirPath = dataDirectory();
    if (baseDirPath.isEmpty()) {
        return QString();
    }
    return QDir(baseDirPath).filePath("icons");
}

bool MainWindow::ensureDirectory(const QString &path, const QString &failureMessage, QString *errorMessage) const
{
    if (path.isEmpty()) {
        if (errorMessage) {
            *errorMessage = failureMessage;
        }
        return false;
    }

    QDir dir(path);
    if (!dir.exists() && !dir.mkpath(".")) {
        if (errorMessage) {
            *errorMessage = failureMessage;
        }
        return false;
    }

    return true;
}

bool MainWindow::saveIconFile(const QIcon &icon, QString *relativePath, QString *errorMessage) const
{
    const QString baseDirPath = dataDirectory();
    if (baseDirPath.isEmpty()) {
        if (errorMessage) {
            *errorMessage = tr("Nao foi possivel obter a pasta de dados da aplicacao.");
        }
        return false;
    }

    if (!ensureDirectory(baseDirPath, tr("Nao foi possivel criar a pasta de dados."), errorMessage)) {
        return false;
    }

    const QString iconsDirPath = iconsDirectoryPath();
    if (iconsDirPath.isEmpty()) {
        if (errorMessage) {
            *errorMessage = tr("Nao foi possivel obter a pasta de dados da aplicacao.");
        }
        return false;
    }
    if (!ensureDirectory(iconsDirPath, tr("Nao foi possivel criar a pasta de icones."), errorMessage)) {
        return false;
    }

    const QString iconFileName = QUuid::createUuid().toString(QUuid::Id128) + ".png";
    const QString destinationIconPath = QDir(iconsDirPath).filePath(iconFileName);

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

    if (relativePath) {
        *relativePath = QDir(baseDirPath).relativeFilePath(destinationIconPath).replace("\\", "/");
    }

    return true;
}

void MainWindow::setupCardSizeControls()
{
    if (!ui || !ui->cardSizeSlider) {
        m_cardWidth = kBaseCardWidth;
        return;
    }

    ui->cardSizeSlider->setMinimum(kMinCardWidth);
    ui->cardSizeSlider->setMaximum(kMaxCardWidth);
    ui->cardSizeSlider->setSingleStep(kCardWidthStep);
    ui->cardSizeSlider->setPageStep(kCardWidthPageStep);
    ui->cardSizeSlider->setTickPosition(QSlider::TicksBelow);
    ui->cardSizeSlider->setTickInterval(kCardWidthTickInterval);

    m_cardWidth = ui->cardSizeSlider->value();
    updateCardSizeLabel(m_cardWidth);

    connect(ui->cardSizeSlider, &QSlider::valueChanged, this, &MainWindow::handleCardSizeChanged);
}

void MainWindow::handleCardSizeChanged(int value)
{
    m_cardWidth = value;
    updateCardSizeLabel(value);
    loadSoftwareEntries();
}

int MainWindow::cardWidth() const
{
    return qBound(kMinCardWidth, m_cardWidth, kMaxCardWidth);
}

int MainWindow::cardHeight() const
{
    const double ratio = static_cast<double>(kBaseCardHeight) / static_cast<double>(kBaseCardWidth);
    return qRound(cardWidth() * ratio);
}

int MainWindow::iconSize() const
{
    const double ratio = static_cast<double>(kBaseIconSize) / static_cast<double>(kBaseCardWidth);
    return qRound(cardWidth() * ratio);
}

int MainWindow::calculateColumnCount(int cardWidth) const
{
    if (!ui || !ui->scrollArea || !ui->gridLayout) {
        return kDefaultColumnCount;
    }

    const int viewportWidth = ui->scrollArea->viewport()->width();
    if (viewportWidth <= 0) {
        return kDefaultColumnCount;
    }

    const QMargins margins = ui->gridLayout->contentsMargins();
    const int availableWidth = viewportWidth - margins.left() - margins.right();
    const int spacing = ui->gridLayout->spacing();
    if (availableWidth <= 0) {
        return kDefaultColumnCount;
    }

    const int columnWidth = cardWidth + spacing;
    if (columnWidth <= 0) {
        return kDefaultColumnCount;
    }

    return qMax(1, (availableWidth + spacing) / columnWidth);
}

void MainWindow::updateCardSizeLabel(int value)
{
    if (!ui || !ui->cardSizeValueLabel) {
        return;
    }

    ui->cardSizeValueLabel->setText(QString::number(value));
}

QString MainWindow::dataDirectory() const
{
    QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (path.isEmpty()) {
        path = QCoreApplication::applicationDirPath();
    }
    return path;
}

QString MainWindow::resolveIconPath(const QString &iconValue, const QString &baseDirPath) const
{
    if (iconValue.isEmpty()) {
        return QString();
    }

    if (QFileInfo(iconValue).isRelative()) {
        return QDir(baseDirPath).filePath(iconValue);
    }

    return iconValue;
}

bool MainWindow::deleteIconIfLocal(const SoftwareEntry &entry, const QString &baseDirPath, QString *errorMessage) const
{
    if (entry.iconPath.isEmpty() || baseDirPath.isEmpty()) {
        return true;
    }

    const bool isRelative = QFileInfo(entry.iconPath).isRelative();
    QString absolutePath = entry.iconPath;
    if (isRelative) {
        absolutePath = QDir(baseDirPath).filePath(entry.iconPath);
    }

    const QString baseAbsolute = QDir(baseDirPath).absolutePath();
    const QString targetAbsolute = QFileInfo(absolutePath).absoluteFilePath();
    if (!targetAbsolute.startsWith(baseAbsolute + QDir::separator())) {
        return true;
    }

    if (!QFile::exists(targetAbsolute)) {
        return true;
    }

    if (!QFile::remove(targetAbsolute)) {
        if (errorMessage) {
            *errorMessage = tr("Nao foi possivel remover o icone.");
        }
        return false;
    }

    return true;
}

QFrame *MainWindow::createSoftwareCard(const SoftwareEntry &entry, const QString &baseDirPath)
{
    if (!ui || !ui->scrollAreaWidgetContents) {
        return nullptr;
    }

    if (entry.name.trimmed().isEmpty()) {
        return nullptr;
    }

    const QSize cardSize(cardWidth(), cardHeight());

    auto *frame = new QFrame(ui->scrollAreaWidgetContents);
    frame->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    frame->setMinimumSize(cardSize);
    frame->setMaximumSize(cardSize);
    frame->setFrameShape(QFrame::StyledPanel);
    frame->setFrameShadow(QFrame::Raised);

    auto *layout = new QVBoxLayout(frame);
    layout->setSpacing(6);
    layout->setContentsMargins(8, 8, 8, 8);

    const int iconSide = iconSize();

    auto *iconLabel = new QLabel(frame);
    iconLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    iconLabel->setMinimumSize(QSize(iconSide, iconSide));
    iconLabel->setMaximumSize(QSize(iconSide, iconSide));
    iconLabel->setFrameShape(QFrame::Box);
    iconLabel->setAlignment(Qt::AlignCenter);
    iconLabel->setScaledContents(true);

    const QString iconPath = resolveIconPath(entry.iconPath, baseDirPath);
    const QPixmap pixmap(iconPath);
    if (pixmap.isNull()) {
        iconLabel->setText(tr("Sem icone"));
    } else {
        iconLabel->setPixmap(pixmap.scaled(iconLabel->size(),
                                           Qt::KeepAspectRatio,
                                           Qt::SmoothTransformation));
    }

    auto *titleLabel = new QLabel(entry.name, frame);
    QFont titleFont = titleLabel->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() + 2.0);
    titleLabel->setFont(titleFont);
    titleLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    auto *openButton = new QPushButton(tr("Abrir"), frame);
    openButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto *deleteButton = new QPushButton(frame);
    deleteButton->setToolTip(tr("Remover do launcher"));
    deleteButton->setIcon(style()->standardIcon(QStyle::SP_TrashIcon));
    deleteButton->setFixedSize(QSize(28, 28));

    auto *buttonRow = new QWidget(frame);
    auto *buttonLayout = new QHBoxLayout(buttonRow);
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    buttonLayout->setSpacing(6);
    buttonLayout->addWidget(openButton);
    buttonLayout->addWidget(deleteButton);

    layout->addWidget(iconLabel, 0, Qt::AlignHCenter);
    layout->addWidget(titleLabel);
    layout->addWidget(buttonRow);

    const QString exePath = entry.exePath;
    connect(openButton, &QPushButton::clicked, this, [this, exePath]() {
        if (exePath.isEmpty() || !QFile::exists(exePath)) {
            QMessageBox::warning(this, tr("Executavel ausente"), tr("Caminho do .exe nao encontrado."));
            return;
        }

        if (!QProcess::startDetached(exePath)) {
            QMessageBox::warning(this, tr("Falha ao abrir"), tr("Nao foi possivel abrir o executavel."));
        }
    });

    connect(deleteButton, &QPushButton::clicked, this, [this, entry]() {
        const auto response = QMessageBox::question(
            this,
            tr("Remover software"),
            tr("Deseja remover \"%1\" do launcher?").arg(entry.name));
        if (response != QMessageBox::Yes) {
            return;
        }

        QString errorMessage;
        if (!removeSoftwareEntry(entry, &errorMessage)) {
            if (errorMessage.isEmpty()) {
                errorMessage = tr("Nao foi possivel remover o software.");
            }
            QMessageBox::warning(this, tr("Erro ao remover"), errorMessage);
            return;
        }

        loadSoftwareEntries();
    });

    return frame;
}

void MainWindow::configureGridColumns(int columns)
{
    if (!ui || !ui->gridLayout) {
        return;
    }

    const int maxColumns = qMax(columns, 12);
    for (int col = 0; col < maxColumns; ++col) {
        ui->gridLayout->setColumnStretch(col, col < columns ? 1 : 0);
    }
}

void MainWindow::loadSoftwareEntries()
{
    if (!ui || !ui->gridLayout) {
        return;
    }

    clearLayout(ui->gridLayout);

    QList<SoftwareEntry> entries;
    if (!readSoftwareEntries(&entries)) {
        return;
    }

    const QString baseDirPath = dataDirectory();
    if (baseDirPath.isEmpty()) {
        return;
    }

    const int columns = calculateColumnCount(cardWidth());
    int index = 0;

    for (const SoftwareEntry &entry : entries) {
        QFrame *frame = createSoftwareCard(entry, baseDirPath);
        if (!frame) {
            continue;
        }

        const int row = index / columns;
        const int col = index % columns;
        ui->gridLayout->addWidget(frame, row, col);
        ++index;
    }

    configureGridColumns(columns);
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
