#include "dotLauncher.h"
#include "ui_dotLauncher.h"

#include <QCoreApplication>
#include <QAbstractItemView>
#include <QComboBox>
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
#include <QHash>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QMargins>
#include <QMessageBox>
#include <QProcess>
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <QPixmap>
#include <QPolygon>
#include <QPushButton>
#include <QSlider>
#include <QSize>
#include <QSizePolicy>
#include <QStandardPaths>
#include <QStyle>
#include <QToolButton>
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
const QString kFilterAllKey = QStringLiteral("__all__");
const QString kFilterUncategorizedKey = QStringLiteral("__uncategorized__");

QIcon buildPencilIcon(const QWidget *referenceWidget)
{
    QIcon icon = QIcon::fromTheme(QStringLiteral("document-edit"));
    if (!icon.isNull()) {
        return icon;
    }

    constexpr int kSize = 16;
    QPixmap pixmap(kSize, kSize);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QColor color = referenceWidget
        ? referenceWidget->palette().color(QPalette::Text)
        : QColor(60, 60, 60);

    QPen pen(color, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.drawLine(QPoint(3, 13), QPoint(13, 3));

    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    QPolygon tip;
    tip << QPoint(13, 3) << QPoint(15, 1) << QPoint(15, 5);
    painter.drawPolygon(tip);

    return QIcon(pixmap);
}
} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::dotLauncher)
{
    ui->setupUi(this);

    if (ui->gridLayout) {
        ui->gridLayout->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    }

    setupCardSizeControls();

    if (ui->manageCategoriesButton) {
        ui->manageCategoriesButton->setIcon(buildPencilIcon(this));
        ui->manageCategoriesButton->setIconSize(QSize(16, 16));
        ui->manageCategoriesButton->setToolTip(tr("Editar categorias"));
    }

    connect(ui->pushButton, &QPushButton::clicked, this, &MainWindow::openAddSoftwareDialog);
    if (ui->categoryFilterCombo) {
        connect(ui->categoryFilterCombo,
                QOverload<int>::of(&QComboBox::currentIndexChanged),
                this,
                &MainWindow::handleCategoryFilterChanged);
    }
    if (ui->manageCategoriesButton) {
        connect(ui->manageCategoriesButton, &QPushButton::clicked, this, &MainWindow::openManageCategoriesDialog);
    }

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

    QStringList availableCategories;
    QString categoriesError;
    if (!readSoftwareEntries(nullptr, &availableCategories, &categoriesError)) {
        if (!categoriesError.isEmpty()) {
            QMessageBox::warning(&dialog, tr("Categorias"), categoriesError);
        }
    }

    auto *categoryCombo = new QComboBox(&dialog);
    categoryCombo->setEditable(true);
    categoryCombo->setInsertPolicy(QComboBox::NoInsert);
    categoryCombo->setMinimumWidth(200);
    if (auto *lineEdit = categoryCombo->lineEdit()) {
        lineEdit->setPlaceholderText(tr("Sem categoria"));
    }
    for (const QString &category : availableCategories) {
        categoryCombo->addItem(category, category);
    }
    categoryCombo->setCurrentIndex(-1);
    if (auto *lineEdit = categoryCombo->lineEdit()) {
        lineEdit->setText(QString());
    }

    formLayout->addRow(tr("Categoria:"), categoryCombo);

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

        const QString category = normalizeCategory(categoryCombo->currentText());
        if (!category.isEmpty() && isReservedCategoryName(category)) {
            QMessageBox::warning(
                &dialog,
                tr("Categoria invalida"),
                tr("Escolha um nome de categoria diferente de \"Todas\" ou \"Sem categoria\"."));
            return;
        }

        QString errorMessage;
        if (!saveSoftwareEntry(name, selectedExePath, selectedIcon, category, &errorMessage)) {
            QMessageBox::critical(&dialog, tr("Erro ao salvar"), errorMessage);
            return;
        }

        dialog.accept();
    });

    if (dialog.exec() == QDialog::Accepted) {
        loadSoftwareEntries();
    }
}

void MainWindow::openManageCategoriesDialog()
{
    QList<SoftwareEntry> entries;
    QStringList categories;
    QString errorMessage;
    if (!readSoftwareEntries(&entries, &categories, &errorMessage)) {
        if (errorMessage.isEmpty()) {
            errorMessage = tr("Nao foi possivel carregar as categorias.");
        }
        QMessageBox::warning(this, tr("Categorias"), errorMessage);
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Gerenciar categorias"));

    auto *layout = new QVBoxLayout(&dialog);

    auto *listWidget = new QListWidget(&dialog);
    listWidget->addItems(categories);
    listWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(listWidget);

    auto *actionsRow = new QWidget(&dialog);
    auto *actionsLayout = new QHBoxLayout(actionsRow);
    actionsLayout->setContentsMargins(0, 0, 0, 0);
    actionsLayout->setSpacing(8);

    auto *addButton = new QPushButton(tr("Adicionar"), &dialog);
    auto *renameButton = new QPushButton(tr("Renomear"), &dialog);
    auto *removeButton = new QPushButton(tr("Remover"), &dialog);

    actionsLayout->addWidget(addButton);
    actionsLayout->addWidget(renameButton);
    actionsLayout->addWidget(removeButton);
    actionsLayout->addStretch(1);
    layout->addWidget(actionsRow);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttonBox->button(QDialogButtonBox::Ok)->setText(tr("Salvar"));
    buttonBox->button(QDialogButtonBox::Cancel)->setText(tr("Cancelar"));
    layout->addWidget(buttonBox);

    auto refreshList = [&]() {
        listWidget->clear();
        listWidget->addItems(categories);
    };

    connect(addButton, &QPushButton::clicked, &dialog, [&]() {
        bool ok = false;
        const QString name = QInputDialog::getText(
            &dialog,
            tr("Nova categoria"),
            tr("Nome da categoria:"),
            QLineEdit::Normal,
            QString(),
            &ok);

        if (!ok) {
            return;
        }

        const QString trimmed = normalizeCategory(name);
        if (trimmed.isEmpty()) {
            QMessageBox::warning(&dialog, tr("Categoria invalida"), tr("Digite um nome valido."));
            return;
        }
        if (isReservedCategoryName(trimmed)) {
            QMessageBox::warning(
                &dialog,
                tr("Categoria invalida"),
                tr("Escolha um nome diferente de \"Todas\" ou \"Sem categoria\"."));
            return;
        }
        if (containsCategory(categories, trimmed, Qt::CaseInsensitive)) {
            QMessageBox::warning(&dialog, tr("Categoria duplicada"), tr("Essa categoria ja existe."));
            return;
        }

        categories.append(trimmed);
        refreshList();
        listWidget->setCurrentRow(listWidget->count() - 1);
    });

    connect(renameButton, &QPushButton::clicked, &dialog, [&]() {
        QListWidgetItem *currentItem = listWidget->currentItem();
        if (!currentItem) {
            QMessageBox::warning(&dialog, tr("Selecione"), tr("Escolha uma categoria para renomear."));
            return;
        }

        const QString oldName = currentItem->text();
        bool ok = false;
        const QString newName = QInputDialog::getText(
            &dialog,
            tr("Renomear categoria"),
            tr("Novo nome:"),
            QLineEdit::Normal,
            oldName,
            &ok);

        if (!ok) {
            return;
        }

        const QString trimmed = normalizeCategory(newName);
        if (trimmed.isEmpty()) {
            QMessageBox::warning(&dialog, tr("Categoria invalida"), tr("Digite um nome valido."));
            return;
        }
        if (isReservedCategoryName(trimmed)) {
            QMessageBox::warning(
                &dialog,
                tr("Categoria invalida"),
                tr("Escolha um nome diferente de \"Todas\" ou \"Sem categoria\"."));
            return;
        }
        if (oldName.compare(trimmed, Qt::CaseInsensitive) != 0
            && containsCategory(categories, trimmed, Qt::CaseInsensitive)) {
            QMessageBox::warning(&dialog, tr("Categoria duplicada"), tr("Essa categoria ja existe."));
            return;
        }

        for (QString &category : categories) {
            if (category.compare(oldName, Qt::CaseInsensitive) == 0) {
                category = trimmed;
                break;
            }
        }

        for (SoftwareEntry &entry : entries) {
            if (entry.category.compare(oldName, Qt::CaseInsensitive) == 0) {
                entry.category = trimmed;
            }
        }

        refreshList();
        const int row = categories.indexOf(trimmed);
        if (row >= 0) {
            listWidget->setCurrentRow(row);
        }
    });

    connect(removeButton, &QPushButton::clicked, &dialog, [&]() {
        QListWidgetItem *currentItem = listWidget->currentItem();
        if (!currentItem) {
            QMessageBox::warning(&dialog, tr("Selecione"), tr("Escolha uma categoria para remover."));
            return;
        }

        const QString name = currentItem->text();
        const auto response = QMessageBox::question(
            &dialog,
            tr("Remover categoria"),
            tr("Deseja remover a categoria \"%1\"?").arg(name));

        if (response != QMessageBox::Yes) {
            return;
        }

        for (int i = categories.size() - 1; i >= 0; --i) {
            if (categories.at(i).compare(name, Qt::CaseInsensitive) == 0) {
                categories.removeAt(i);
            }
        }

        for (SoftwareEntry &entry : entries) {
            if (entry.category.compare(name, Qt::CaseInsensitive) == 0) {
                entry.category.clear();
            }
        }

        refreshList();
    });

    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, [&]() {
        QString saveError;
        if (!writeSoftwareEntries(entries, categories, &saveError)) {
            if (saveError.isEmpty()) {
                saveError = tr("Nao foi possivel salvar as categorias.");
            }
            QMessageBox::warning(&dialog, tr("Erro ao salvar"), saveError);
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
                                   const QString &category,
                                   QString *errorMessage)
{
    SoftwareEntry entry;
    entry.name = name;
    entry.exePath = exePath;
    entry.category = normalizeCategory(category);

    if (!saveIconFile(icon, &entry.iconPath, errorMessage)) {
        return false;
    }

    return appendSoftwareEntry(entry, errorMessage);
}

bool MainWindow::appendSoftwareEntry(const SoftwareEntry &entry, QString *errorMessage)
{
    QList<SoftwareEntry> entries;
    QStringList categories;
    if (!readSoftwareEntries(&entries, &categories, errorMessage)) {
        return false;
    }

    entries.append(entry);
    const QString normalizedCategory = normalizeCategory(entry.category);
    if (!normalizedCategory.isEmpty()) {
        categories.append(normalizedCategory);
    }
    categories = normalizeCategories(categories);
    return writeSoftwareEntries(entries, categories, errorMessage);
}

bool MainWindow::readSoftwareEntries(QList<SoftwareEntry> *entries,
                                     QStringList *categories,
                                     QString *errorMessage) const
{
    if (!entries && !categories) {
        return false;
    }

    if (entries) {
        entries->clear();
    }
    if (categories) {
        categories->clear();
    }

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
    QStringList loadedCategories;
    if (doc.isArray()) {
        items = doc.array();
    } else if (doc.isObject()) {
        const QJsonObject root = doc.object();
        const QJsonValue stored = root.value(QStringLiteral("softwares"));
        if (stored.isArray()) {
            items = stored.toArray();
        }
        const QJsonValue categoriesValue = root.value(QStringLiteral("categories"));
        if (categoriesValue.isArray()) {
            const QJsonArray categoryArray = categoriesValue.toArray();
            for (const QJsonValue &categoryValue : categoryArray) {
                const QString category = categoryValue.toString().trimmed();
                if (!category.isEmpty()) {
                    loadedCategories.append(category);
                }
            }
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
        entry.category = obj.value(QStringLiteral("category")).toString().trimmed();

        if (entry.name.isEmpty()) {
            continue;
        }

        if (!entry.category.isEmpty()) {
            loadedCategories.append(entry.category);
        }

        if (entries) {
            entries->append(entry);
        }
    }

    if (categories) {
        *categories = normalizeCategories(loadedCategories);
    }

    return true;
}

bool MainWindow::writeSoftwareEntries(const QList<SoftwareEntry> &entries,
                                      const QStringList &categories,
                                      QString *errorMessage) const
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
        if (!entry.category.trimmed().isEmpty()) {
            obj.insert(QStringLiteral("category"), entry.category.trimmed());
        }
        items.append(obj);
    }

    QJsonObject root;
    const QStringList normalizedCategories = normalizeCategories(categories);
    if (!normalizedCategories.isEmpty()) {
        QJsonArray categoryArray;
        for (const QString &category : normalizedCategories) {
            if (!category.trimmed().isEmpty()) {
                categoryArray.append(category);
            }
        }
        root.insert(QStringLiteral("categories"), categoryArray);
    }
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

QStringList MainWindow::normalizeCategories(const QStringList &categories) const
{
    QStringList result;
    for (const QString &category : categories) {
        const QString trimmed = category.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }
        if (containsCategory(result, trimmed, Qt::CaseInsensitive)) {
            continue;
        }
        result.append(trimmed);
    }
    return result;
}

bool MainWindow::containsCategory(const QStringList &categories,
                                  const QString &value,
                                  Qt::CaseSensitivity sensitivity) const
{
    for (const QString &category : categories) {
        if (category.compare(value, sensitivity) == 0) {
            return true;
        }
    }
    return false;
}

bool MainWindow::isReservedCategoryName(const QString &name) const
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }
    if (trimmed.compare(QStringLiteral("Todas"), Qt::CaseInsensitive) == 0) {
        return true;
    }
    if (trimmed.compare(QStringLiteral("Sem categoria"), Qt::CaseInsensitive) == 0) {
        return true;
    }
    if (trimmed.compare(kFilterAllKey, Qt::CaseInsensitive) == 0) {
        return true;
    }
    if (trimmed.compare(kFilterUncategorizedKey, Qt::CaseInsensitive) == 0) {
        return true;
    }
    return false;
}

QString MainWindow::normalizeCategory(const QString &name) const
{
    return name.trimmed();
}

bool MainWindow::isCategoryCollapsed(const QString &key) const
{
    const QString normalized = normalizeCategory(key).toLower();
    if (normalized.isEmpty()) {
        return false;
    }
    return m_collapsedCategories.value(normalized, false);
}

void MainWindow::setCategoryCollapsed(const QString &key, bool collapsed)
{
    const QString normalized = normalizeCategory(key).toLower();
    if (normalized.isEmpty()) {
        return;
    }
    if (collapsed) {
        m_collapsedCategories.insert(normalized, true);
    } else {
        m_collapsedCategories.remove(normalized);
    }
}

QString MainWindow::selectedCategoryFilterKey() const
{
    if (!ui || !ui->categoryFilterCombo) {
        return kFilterAllKey;
    }

    const QVariant data = ui->categoryFilterCombo->currentData();
    if (data.isValid()) {
        return data.toString();
    }

    return kFilterAllKey;
}

void MainWindow::updateCategoryFilterCombo(const QStringList &categories)
{
    if (!ui || !ui->categoryFilterCombo) {
        return;
    }

    const QString currentKey = ui->categoryFilterCombo->currentData().toString();
    const QString currentText = ui->categoryFilterCombo->currentText();

    ui->categoryFilterCombo->blockSignals(true);
    ui->categoryFilterCombo->clear();
    ui->categoryFilterCombo->addItem(tr("Todas"), kFilterAllKey);
    ui->categoryFilterCombo->addItem(tr("Sem categoria"), kFilterUncategorizedKey);

    const QStringList normalized = normalizeCategories(categories);
    for (const QString &category : normalized) {
        ui->categoryFilterCombo->addItem(category, category);
    }

    int restoreIndex = ui->categoryFilterCombo->findData(currentKey);
    if (restoreIndex < 0 && !currentText.isEmpty()) {
        restoreIndex = ui->categoryFilterCombo->findText(currentText, Qt::MatchFixedString);
    }
    if (restoreIndex < 0) {
        restoreIndex = 0;
    }

    ui->categoryFilterCombo->setCurrentIndex(restoreIndex);
    ui->categoryFilterCombo->blockSignals(false);
}

void MainWindow::handleCategoryFilterChanged(int)
{
    loadSoftwareEntries();
}

bool MainWindow::removeSoftwareEntry(const SoftwareEntry &entry, QString *errorMessage)
{
    QList<SoftwareEntry> entries;
    QStringList categories;
    if (!readSoftwareEntries(&entries, &categories, errorMessage)) {
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
    if (!writeSoftwareEntries(entries, categories, errorMessage)) {
        return false;
    }

    const QString baseDirPath = dataDirectory();
    if (!baseDirPath.isEmpty()) {
        deleteIconIfLocal(entry, baseDirPath, nullptr);
    }

    return true;
}

bool MainWindow::updateSoftwareEntryCategory(const SoftwareEntry &entry,
                                             const QString &category,
                                             QString *errorMessage)
{
    QList<SoftwareEntry> entries;
    QStringList categories;
    if (!readSoftwareEntries(&entries, &categories, errorMessage)) {
        return false;
    }

    bool updated = false;
    const QString normalizedCategory = normalizeCategory(category);
    for (SoftwareEntry &current : entries) {
        if (current.name == entry.name
            && current.exePath == entry.exePath
            && current.iconPath == entry.iconPath) {
            current.category = normalizedCategory;
            updated = true;
            break;
        }
    }

    if (!updated) {
        if (errorMessage) {
            *errorMessage = tr("Nao foi possivel localizar o software para editar.");
        }
        return false;
    }

    if (!normalizedCategory.isEmpty()) {
        categories.append(normalizedCategory);
    }
    categories = normalizeCategories(categories);

    return writeSoftwareEntries(entries, categories, errorMessage);
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

    const QString categoryText = normalizeCategory(entry.category);
    auto *categoryLabel = new QLabel(frame);
    QFont categoryFont = categoryLabel->font();
    categoryFont.setPointSizeF(qMax(6.0, categoryFont.pointSizeF() - 1.0));
    categoryLabel->setFont(categoryFont);
    categoryLabel->setText(categoryText.isEmpty() ? tr("Sem categoria") : categoryText);
    categoryLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    categoryLabel->setStyleSheet(QStringLiteral("color: #6e6e6e;"));

    auto *openButton = new QPushButton(tr("Abrir"), frame);
    openButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto *editCategoryButton = new QPushButton(frame);
    editCategoryButton->setToolTip(tr("Editar categoria"));
    editCategoryButton->setIcon(buildPencilIcon(this));
    editCategoryButton->setIconSize(QSize(16, 16));
    editCategoryButton->setFixedSize(QSize(28, 28));

    auto *deleteButton = new QPushButton(frame);
    deleteButton->setToolTip(tr("Remover do launcher"));
    deleteButton->setIcon(style()->standardIcon(QStyle::SP_TrashIcon));
    deleteButton->setFixedSize(QSize(28, 28));

    auto *buttonRow = new QWidget(frame);
    auto *buttonLayout = new QHBoxLayout(buttonRow);
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    buttonLayout->setSpacing(6);
    buttonLayout->addWidget(openButton);
    buttonLayout->addWidget(editCategoryButton);
    buttonLayout->addWidget(deleteButton);

    layout->addWidget(iconLabel, 0, Qt::AlignHCenter);
    layout->addWidget(titleLabel);
    layout->addWidget(categoryLabel);
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

    connect(editCategoryButton, &QPushButton::clicked, this, [this, entry]() {
        openEditCategoryDialog(entry);
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

void MainWindow::openEditCategoryDialog(const SoftwareEntry &entry)
{
    QStringList categories;
    QString errorMessage;
    if (!readSoftwareEntries(nullptr, &categories, &errorMessage)) {
        if (errorMessage.isEmpty()) {
            errorMessage = tr("Nao foi possivel carregar as categorias.");
        }
        QMessageBox::warning(this, tr("Categorias"), errorMessage);
        return;
    }

    QStringList available = normalizeCategories(categories);
    const QString currentCategory = normalizeCategory(entry.category);
    if (!currentCategory.isEmpty()
        && !containsCategory(available, currentCategory, Qt::CaseInsensitive)) {
        available.append(currentCategory);
    }
    available = normalizeCategories(available);

    QStringList items = available;
    if (!containsCategory(items, tr("Sem categoria"), Qt::CaseInsensitive)) {
        items.prepend(tr("Sem categoria"));
    }

    int currentIndex = 0;
    for (int i = 0; i < items.size(); ++i) {
        if (items.at(i).compare(currentCategory, Qt::CaseInsensitive) == 0) {
            currentIndex = i;
            break;
        }
    }

    bool ok = false;
    QString selected = QInputDialog::getItem(
        this,
        tr("Editar categoria"),
        tr("Categoria:"),
        items,
        currentIndex,
        true,
        &ok);
    if (!ok) {
        return;
    }

    QString normalized = normalizeCategory(selected);
    if (normalized.compare(QStringLiteral("Sem categoria"), Qt::CaseInsensitive) == 0) {
        normalized.clear();
    }
    if (!normalized.isEmpty() && isReservedCategoryName(normalized)) {
        QMessageBox::warning(
            this,
            tr("Categoria invalida"),
            tr("Escolha um nome diferente de \"Todas\" ou \"Sem categoria\"."));
        return;
    }

    QString saveError;
    if (!updateSoftwareEntryCategory(entry, normalized, &saveError)) {
        if (saveError.isEmpty()) {
            saveError = tr("Nao foi possivel atualizar a categoria.");
        }
        QMessageBox::warning(this, tr("Erro ao salvar"), saveError);
        return;
    }

    loadSoftwareEntries();
}

QWidget *MainWindow::createCategoryHeader(const QString &title, const QString &key)
{
    if (!ui || !ui->scrollAreaWidgetContents) {
        return nullptr;
    }

    auto *container = new QWidget(ui->scrollAreaWidgetContents);
    auto *layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 8, 0, 0);
    layout->setSpacing(8);

    const bool collapsed = isCategoryCollapsed(key);

    auto *toggleButton = new QToolButton(container);
    toggleButton->setArrowType(collapsed ? Qt::RightArrow : Qt::DownArrow);
    toggleButton->setAutoRaise(true);
    toggleButton->setToolTip(collapsed ? tr("Expandir") : tr("Recolher"));
    toggleButton->setFixedSize(QSize(18, 18));

    auto *label = new QLabel(title, container);
    QFont font = label->font();
    font.setBold(true);
    font.setPointSizeF(font.pointSizeF() + 2.0);
    label->setFont(font);
    label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    auto *line = new QFrame(container);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    line->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    line->setFixedHeight(1);

    layout->addWidget(toggleButton);
    layout->addWidget(label);
    layout->addWidget(line);

    connect(toggleButton, &QToolButton::clicked, this, [this, key]() {
        const bool nowCollapsed = isCategoryCollapsed(key);
        setCategoryCollapsed(key, !nowCollapsed);
        loadSoftwareEntries();
    });

    return container;
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
    QStringList categories;
    if (!readSoftwareEntries(&entries, &categories)) {
        return;
    }

    updateCategoryFilterCombo(categories);

    const QString baseDirPath = dataDirectory();
    if (baseDirPath.isEmpty()) {
        return;
    }

    const int columns = calculateColumnCount(cardWidth());
    const QString filterKey = selectedCategoryFilterKey();

    QHash<QString, QList<SoftwareEntry>> groupedEntries;
    QHash<QString, QString> displayNames;

    for (const SoftwareEntry &entry : entries) {
        const QString normalized = normalizeCategory(entry.category);
        const QString key = normalized.isEmpty() ? kFilterUncategorizedKey : normalized.toLower();
        groupedEntries[key].append(entry);
        if (!displayNames.contains(key)) {
            displayNames.insert(key, normalized.isEmpty() ? tr("Sem categoria") : normalized);
        }
    }

    QStringList orderedKeys;
    const QStringList normalizedCategories = normalizeCategories(categories);
    for (const QString &category : normalizedCategories) {
        const QString normalized = normalizeCategory(category);
        if (normalized.isEmpty()) {
            continue;
        }
        const QString key = normalized.toLower();
        if (!orderedKeys.contains(key)) {
            orderedKeys.append(key);
        }
        if (!displayNames.contains(key)) {
            displayNames.insert(key, normalized);
        }
    }

    if (filterKey == kFilterAllKey) {
        for (auto it = groupedEntries.constBegin(); it != groupedEntries.constEnd(); ++it) {
            if (it.key() == kFilterUncategorizedKey) {
                continue;
            }
            if (!orderedKeys.contains(it.key())) {
                orderedKeys.append(it.key());
                if (!displayNames.contains(it.key()) && !it.value().isEmpty()) {
                    const QString fallback = normalizeCategory(it.value().first().category);
                    if (!fallback.isEmpty()) {
                        displayNames.insert(it.key(), fallback);
                    }
                }
            }
        }

        if (groupedEntries.contains(kFilterUncategorizedKey)
            && !groupedEntries.value(kFilterUncategorizedKey).isEmpty()) {
            orderedKeys.append(kFilterUncategorizedKey);
            if (!displayNames.contains(kFilterUncategorizedKey)) {
                displayNames.insert(kFilterUncategorizedKey, tr("Sem categoria"));
            }
        }
    } else if (filterKey == kFilterUncategorizedKey) {
        orderedKeys = QStringList{kFilterUncategorizedKey};
        if (!displayNames.contains(kFilterUncategorizedKey)) {
            displayNames.insert(kFilterUncategorizedKey, tr("Sem categoria"));
        }
    } else {
        const QString normalizedFilter = normalizeCategory(filterKey);
        const QString key = normalizedFilter.toLower();
        if (!key.isEmpty()) {
            orderedKeys = QStringList{key};
            if (!displayNames.contains(key)) {
                QString display = normalizedFilter;
                for (const QString &category : normalizedCategories) {
                    if (category.compare(normalizedFilter, Qt::CaseInsensitive) == 0) {
                        display = category;
                        break;
                    }
                }
                if (!display.isEmpty()) {
                    displayNames.insert(key, display);
                }
            }
        }
    }

    int row = 0;
    for (const QString &key : orderedKeys) {
        const bool isUncategorized = (key == kFilterUncategorizedKey);
        const QList<SoftwareEntry> categoryEntries = groupedEntries.value(key);
        const bool hasEntries = !categoryEntries.isEmpty();

        if (filterKey == kFilterAllKey && isUncategorized && !hasEntries) {
            continue;
        }

        const QString headerText = displayNames.value(
            key,
            isUncategorized ? tr("Sem categoria") : key);
        QWidget *header = createCategoryHeader(headerText, key);
        if (header) {
            ui->gridLayout->addWidget(header, row, 0, 1, columns);
            ++row;
        }

        if (isCategoryCollapsed(key)) {
            continue;
        }

        int col = 0;
        for (const SoftwareEntry &entry : categoryEntries) {
            QFrame *frame = createSoftwareCard(entry, baseDirPath);
            if (!frame) {
                continue;
            }

            ui->gridLayout->addWidget(frame, row, col);
            ++col;
            if (col >= columns) {
                col = 0;
                ++row;
            }
        }

        if (col != 0) {
            ++row;
        }
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
