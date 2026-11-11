#include "ui/mfclibrarypage.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDate>
#include <QDateEdit>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTableWidget>
#include <QTime>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace
{

enum Column
{
    ColumnName = 0,
    ColumnUid,
    ColumnType,
    ColumnVersion,
    ColumnBlocks,
    ColumnKeys,
    ColumnSource,
    ColumnImported,
    ColumnSha256,
    ColumnDuplicate,
    ColumnPath,
    ColumnCount
};

QStringList dumpNameFilters()
{
    return QStringList() << QStringLiteral("*.bin") << QStringLiteral("*.dump")
                         << QStringLiteral("*.eml") << QStringLiteral("*.txt")
                         << QStringLiteral("*.json");
}

QString timestampText(const QDateTime& value)
{
    return value.isValid() ? value.toString(QStringLiteral("yyyy-MM-dd hh:mm")) : QString();
}

QTableWidgetItem* readOnlyItem(const QString& text)
{
    QTableWidgetItem* item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}

QString entryDisplayName(const Mfc::LibraryEntry& entry)
{
    if(!entry.alias.trimmed().isEmpty())
        return entry.alias.trimmed();
    if(!entry.fileName.isEmpty())
        return entry.fileName;
    return entry.uid;
}

} // namespace

MfcLibraryPage::MfcLibraryPage(QWidget* parent) : QWidget(parent)
{
    buildUi();
    setAcceptDrops(true);
    loadLibrary();
}

void MfcLibraryPage::buildUi()
{
    QVBoxLayout* outer = new QVBoxLayout(this);

    // --- library root + import actions --------------------------------
    QHBoxLayout* rootRow = new QHBoxLayout();
    QLabel* rootCaption = new QLabel(tr("Library:"), this);
    rootLabel = new QLabel(this);
    rootLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    rootLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    QPushButton* chooseButton = new QPushButton(tr("Choose folder..."), this);
    QPushButton* importFilesButton = new QPushButton(tr("Import files..."), this);
    QPushButton* importFolderButton = new QPushButton(tr("Import folder..."), this);
    QPushButton* refreshButton = new QPushButton(tr("Refresh"), this);
    connect(chooseButton, &QPushButton::clicked, this, &MfcLibraryPage::chooseFolder);
    connect(importFilesButton, &QPushButton::clicked, this, &MfcLibraryPage::importFiles);
    connect(importFolderButton, &QPushButton::clicked, this, &MfcLibraryPage::importFolder);
    connect(refreshButton, &QPushButton::clicked, this, &MfcLibraryPage::refresh);
    rootRow->addWidget(rootCaption);
    rootRow->addWidget(rootLabel, 1);
    rootRow->addWidget(chooseButton);
    rootRow->addWidget(importFilesButton);
    rootRow->addWidget(importFolderButton);
    rootRow->addWidget(refreshButton);
    outer->addLayout(rootRow);

    // --- filter row ---------------------------------------------------
    QGroupBox* filterBox = new QGroupBox(tr("Filter"), this);
    QHBoxLayout* filterRow = new QHBoxLayout(filterBox);
    searchEdit = new QLineEdit(filterBox);
    searchEdit->setPlaceholderText(tr("Search alias, notes, tags, UID or file name"));
    searchEdit->setClearButtonEnabled(true);
    typeCombo = new QComboBox(filterBox);
    typeCombo->addItem(tr("Any type"), -1);
    typeCombo->addItem(tr("Mini"), static_cast<int>(Mfc::CardType::Mini));
    typeCombo->addItem(tr("1K"), static_cast<int>(Mfc::CardType::M1K));
    typeCombo->addItem(tr("2K"), static_cast<int>(Mfc::CardType::M2K));
    typeCombo->addItem(tr("4K"), static_cast<int>(Mfc::CardType::M4K));
    uidEdit = new QLineEdit(filterBox);
    uidEdit->setPlaceholderText(tr("UID"));
    uidEdit->setMaximumWidth(140);
    uidEdit->setClearButtonEnabled(true);
    completenessCombo = new QComboBox(filterBox);
    completenessCombo->addItem(tr("Any completeness"), -1);
    completenessCombo->addItem(tr("Complete"), 100);
    completenessCombo->addItem(tr("Partial"), 0);
    duplicatesCheck = new QCheckBox(tr("Duplicates only"), filterBox);
    fromCheck = new QCheckBox(tr("From"), filterBox);
    fromEdit = new QDateEdit(filterBox);
    fromEdit->setCalendarPopup(true);
    fromEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    fromEdit->setDate(QDate::currentDate());
    fromEdit->setEnabled(false);
    toCheck = new QCheckBox(tr("To"), filterBox);
    toEdit = new QDateEdit(filterBox);
    toEdit->setCalendarPopup(true);
    toEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    toEdit->setDate(QDate::currentDate());
    toEdit->setEnabled(false);

    filterRow->addWidget(searchEdit, 1);
    filterRow->addWidget(typeCombo);
    filterRow->addWidget(uidEdit);
    filterRow->addWidget(completenessCombo);
    filterRow->addWidget(duplicatesCheck);
    filterRow->addSpacing(8);
    filterRow->addWidget(fromCheck);
    filterRow->addWidget(fromEdit);
    filterRow->addWidget(toCheck);
    filterRow->addWidget(toEdit);
    outer->addWidget(filterBox);

    connect(searchEdit, &QLineEdit::textChanged, this, &MfcLibraryPage::applyFilter);
    connect(uidEdit, &QLineEdit::textChanged, this, &MfcLibraryPage::applyFilter);
    connect(typeCombo, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, &MfcLibraryPage::applyFilter);
    connect(completenessCombo,
            static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, &MfcLibraryPage::applyFilter);
    connect(duplicatesCheck, &QCheckBox::toggled, this, &MfcLibraryPage::applyFilter);
    connect(fromCheck, &QCheckBox::toggled, fromEdit, &QDateEdit::setEnabled);
    connect(fromCheck, &QCheckBox::toggled, this, &MfcLibraryPage::applyFilter);
    connect(toCheck, &QCheckBox::toggled, toEdit, &QDateEdit::setEnabled);
    connect(toCheck, &QCheckBox::toggled, this, &MfcLibraryPage::applyFilter);
    connect(fromEdit, &QDateEdit::dateChanged, this, &MfcLibraryPage::applyFilter);
    connect(toEdit, &QDateEdit::dateChanged, this, &MfcLibraryPage::applyFilter);

    // --- table + detail panel -----------------------------------------
    QSplitter* splitter = new QSplitter(Qt::Horizontal, this);

    table = new QTableWidget(0, ColumnCount, splitter);
    table->setHorizontalHeaderLabels(QStringList()
                                     << tr("Name / alias") << tr("UID") << tr("Type")
                                     << tr("Version") << tr("Blocks") << tr("Keys")
                                     << tr("Source") << tr("Imported") << tr("SHA-256")
                                     << tr("Duplicate") << tr("Path"));
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setAlternatingRowColors(true);
    table->setSortingEnabled(true);
    table->verticalHeader()->setVisible(false);
    QHeaderView* header = table->horizontalHeader();
    header->setSectionResizeMode(ColumnName, QHeaderView::Interactive);
    header->setSectionResizeMode(ColumnUid, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(ColumnType, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(ColumnVersion, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(ColumnBlocks, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(ColumnKeys, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(ColumnSource, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(ColumnImported, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(ColumnSha256, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(ColumnDuplicate, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(ColumnPath, QHeaderView::Stretch);
    table->setColumnWidth(ColumnName, 200);
    connect(table, &QTableWidget::itemSelectionChanged, this,
            &MfcLibraryPage::selectionChanged);
    connect(table, &QTableWidget::itemDoubleClicked, this, &MfcLibraryPage::openInWorkbench);
    splitter->addWidget(table);

    QGroupBox* detailBox = new QGroupBox(tr("Entry details"), splitter);
    QVBoxLayout* detailLayout = new QVBoxLayout(detailBox);
    QFormLayout* form = new QFormLayout();
    aliasEdit = new QLineEdit(detailBox);
    aliasEdit->setPlaceholderText(tr("Name shown in the library"));
    notesEdit = new QPlainTextEdit(detailBox);
    notesEdit->setPlaceholderText(tr("Notes about this backup"));
    notesEdit->setMinimumHeight(70);
    tagsEdit = new QLineEdit(detailBox);
    tagsEdit->setPlaceholderText(tr("tag1, tag2"));
    form->addRow(tr("Alias:"), aliasEdit);
    form->addRow(tr("Notes:"), notesEdit);
    form->addRow(tr("Tags:"), tagsEdit);
    detailLayout->addLayout(form);

    saveButton = new QPushButton(tr("Save details"), detailBox);
    openButton = new QPushButton(tr("Open in workbench"), detailBox);
    compareButton = new QPushButton(tr("Compare selected two"), detailBox);
    exportButton = new QPushButton(tr("Export..."), detailBox);
    removeButton = new QPushButton(tr("Remove from library"), detailBox);
    connect(saveButton, &QPushButton::clicked, this, &MfcLibraryPage::saveDetails);
    connect(openButton, &QPushButton::clicked, this, &MfcLibraryPage::openInWorkbench);
    connect(compareButton, &QPushButton::clicked, this, &MfcLibraryPage::compareSelected);
    connect(exportButton, &QPushButton::clicked, this, &MfcLibraryPage::exportSelected);
    connect(removeButton, &QPushButton::clicked, this, &MfcLibraryPage::removeSelected);
    detailLayout->addWidget(saveButton);
    detailLayout->addWidget(openButton);
    detailLayout->addWidget(compareButton);
    detailLayout->addWidget(exportButton);
    detailLayout->addWidget(removeButton);
    detailLayout->addStretch(1);
    splitter->addWidget(detailBox);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 1);
    outer->addWidget(splitter, 1);

    statusLabel = new QLabel(this);
    statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    statusLabel->setWordWrap(true);
    outer->addWidget(statusLabel);

    updateDetailPanel();
}

void MfcLibraryPage::setLibraryRoot(const QString& dir)
{
    m_library.setRootDir(dir);
    loadLibrary();
}

void MfcLibraryPage::refresh()
{
    loadLibrary();
}

void MfcLibraryPage::loadLibrary()
{
    m_loadError.clear();
    QString error;
    if(!m_library.load(&error))
        m_loadError = error.isEmpty() ? tr("the library index cannot be read") : error;
    m_detailId.clear();
    if(rootLabel != nullptr)
        rootLabel->setText(m_library.rootDir().isEmpty()
                               ? tr("No folder selected")
                               : QDir::toNativeSeparators(m_library.rootDir()));
    applyFilter();
}

void MfcLibraryPage::applyFilter()
{
    if(searchEdit == nullptr)
        return;

    Mfc::LibraryFilter filter;
    filter.text = searchEdit->text().trimmed();
    const int typeValue = typeCombo->currentData().toInt();
    if(typeValue >= 0)
    {
        filter.hasType = true;
        filter.type = static_cast<Mfc::CardType>(typeValue);
    }
    filter.uid = Mfc::Document::normalizeHex(uidEdit->text().trimmed());
    filter.duplicatesOnly = duplicatesCheck->isChecked();
    if(fromCheck->isChecked())
        filter.from = QDateTime(fromEdit->date(), QTime(0, 0, 0));
    if(toCheck->isChecked())
        filter.to = QDateTime(toEdit->date(), QTime(23, 59, 59));

    const int completeness = completenessCombo->currentData().toInt();
    if(completeness == 100)
        filter.minCompletePercent = 100;

    const QStringList keep = selectedIds();
    m_visible = m_library.filtered(filter);
    if(completeness == 0)
    {
        QVector<Mfc::LibraryEntry> partial;
        for(const Mfc::LibraryEntry& entry : m_visible)
            if(entry.blockCount <= 0 || entry.completeBlocks < entry.blockCount)
                partial.append(entry);
        m_visible = partial;
    }

    populateTable();
    selectIds(keep);
    updateDetailPanel();
}

void MfcLibraryPage::populateTable()
{
    const bool sorting = table->isSortingEnabled();
    table->setSortingEnabled(false);
    table->clearContents();
    table->setRowCount(m_visible.size());

    for(int row = 0; row < m_visible.size(); row++)
    {
        const Mfc::LibraryEntry& entry = m_visible.at(row);
        const bool duplicate = m_library.isDuplicate(entry.sha256, entry.id);

        QTableWidgetItem* nameItem = readOnlyItem(entryDisplayName(entry));
        nameItem->setData(Qt::UserRole, entry.id);
        if(entry.missing)
        {
            nameItem->setForeground(QColor(150, 150, 150));
            nameItem->setToolTip(tr("The stored file is missing."));
        }
        else if(!entry.notes.isEmpty())
        {
            nameItem->setToolTip(entry.notes);
        }
        table->setItem(row, ColumnName, nameItem);

        table->setItem(row, ColumnUid, readOnlyItem(entry.uid));

        QTableWidgetItem* typeItem = readOnlyItem(
            Mfc::Spec::get(entry.cardType).displayName());
        table->setItem(row, ColumnType, typeItem);

        table->setItem(row, ColumnVersion, readOnlyItem(QString::number(entry.version)));
        table->setItem(row, ColumnBlocks,
                       readOnlyItem(entry.blockCount > 0
                                        ? QStringLiteral("%1/%2").arg(entry.completeBlocks)
                                              .arg(entry.blockCount)
                                        : QStringLiteral("-")));
        table->setItem(row, ColumnKeys, readOnlyItem(QString::number(entry.verifiedKeys)));
        table->setItem(row, ColumnSource, readOnlyItem(entry.source));

        QTableWidgetItem* importedItem = readOnlyItem(timestampText(entry.importedAt));
        if(entry.importedAt.isValid())
            importedItem->setToolTip(entry.importedAt.toString(Qt::ISODate));
        table->setItem(row, ColumnImported, importedItem);

        QTableWidgetItem* shaItem = readOnlyItem(entry.sha256.left(12));
        shaItem->setToolTip(entry.sha256);
        table->setItem(row, ColumnSha256, shaItem);

        QTableWidgetItem* duplicateItem =
            readOnlyItem(duplicate ? tr("yes") : QString());
        if(duplicate)
            duplicateItem->setBackground(QColor(255, 240, 170));
        table->setItem(row, ColumnDuplicate, duplicateItem);

        QTableWidgetItem* pathItem = readOnlyItem(QDir::toNativeSeparators(entry.filePath));
        pathItem->setToolTip(QDir::toNativeSeparators(entry.filePath));
        table->setItem(row, ColumnPath, pathItem);
    }

    table->setSortingEnabled(sorting);
    if(sorting)
        table->sortItems(ColumnImported, Qt::DescendingOrder);

    if(m_loadError.isEmpty())
        setStatus(tr("%1 of %2 entries").arg(m_visible.size()).arg(m_library.entries().size()));
    else
        setStatus(tr("Library problem: %1").arg(m_loadError));
}

void MfcLibraryPage::updateDetailPanel()
{
    const QVector<Mfc::LibraryEntry> selected = selectedEntries();

    Mfc::LibraryEntry entry;
    const bool single = selected.size() == 1;
    if(single)
        entry = selected.first();

    m_detailId = single ? entry.id : QString();
    {
        const QSignalBlocker aliasBlocker(aliasEdit);
        const QSignalBlocker notesBlocker(notesEdit);
        const QSignalBlocker tagsBlocker(tagsEdit);
        aliasEdit->setText(single ? entry.alias : QString());
        notesEdit->setPlainText(single ? entry.notes : QString());
        tagsEdit->setText(single ? entry.tags.join(QStringLiteral(", ")) : QString());
    }
    aliasEdit->setEnabled(single);
    notesEdit->setEnabled(single);
    tagsEdit->setEnabled(single);
    saveButton->setEnabled(single);
    openButton->setEnabled(single);
    compareButton->setEnabled(selected.size() == 2);
    exportButton->setEnabled(!selected.isEmpty());
    removeButton->setEnabled(!selected.isEmpty());
}

void MfcLibraryPage::selectionChanged()
{
    updateDetailPanel();
}

void MfcLibraryPage::setStatus(const QString& text)
{
    if(statusLabel != nullptr)
        statusLabel->setText(text);
    emit statusMessage(text);
}

QString MfcLibraryPage::currentEntryId() const
{
    const QList<QTableWidgetItem*> rows = table->selectedItems();
    if(rows.isEmpty())
        return QString();
    return rows.first()->data(Qt::UserRole).toString();
}

QStringList MfcLibraryPage::selectedIds() const
{
    QStringList ids;
    QList<int> rows;
    const QModelIndexList indexes = table->selectionModel() != nullptr
                                        ? table->selectionModel()->selectedRows()
                                        : QModelIndexList();
    for(const QModelIndex& index : indexes)
        rows.append(index.row());
    std::sort(rows.begin(), rows.end());
    for(int row : rows)
    {
        QTableWidgetItem* item = table->item(row, ColumnName);
        if(item == nullptr)
            continue;
        const QString id = item->data(Qt::UserRole).toString();
        if(!id.isEmpty() && !ids.contains(id))
            ids.append(id);
    }
    return ids;
}

QVector<Mfc::LibraryEntry> MfcLibraryPage::selectedEntries() const
{
    QVector<Mfc::LibraryEntry> result;
    for(const QString& id : selectedIds())
    {
        Mfc::LibraryEntry entry;
        if(entryById(id, &entry))
            result.append(entry);
    }
    return result;
}

bool MfcLibraryPage::entryById(const QString& id, Mfc::LibraryEntry* out) const
{
    if(id.isEmpty())
        return false;
    return m_library.entry(id, out);
}

void MfcLibraryPage::selectIds(const QStringList& ids)
{
    if(ids.isEmpty() || table->selectionModel() == nullptr)
        return;
    bool first = true;
    for(int row = 0; row < table->rowCount(); row++)
    {
        QTableWidgetItem* item = table->item(row, ColumnName);
        if(item == nullptr)
            continue;
        if(!ids.contains(item->data(Qt::UserRole).toString()))
            continue;
        QItemSelectionModel::SelectionFlags flags = QItemSelectionModel::Rows;
        flags |= first ? QItemSelectionModel::ClearAndSelect : QItemSelectionModel::Select;
        table->selectionModel()->select(table->model()->index(row, 0), flags);
        first = false;
    }
}

void MfcLibraryPage::chooseFolder()
{
    const QString start = m_library.rootDir().isEmpty() ? QDir::homePath()
                                                        : m_library.rootDir();
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Choose backup library folder"), start);
    if(dir.isEmpty())
        return;
    m_library.setRootDir(dir);
    loadLibrary();
    if(m_loadError.isEmpty())
        setStatus(tr("Library folder: %1").arg(QDir::toNativeSeparators(dir)));
}

void MfcLibraryPage::importFiles()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Import backups"),
        m_library.rootDir().isEmpty() ? QDir::homePath() : m_library.rootDir(),
        tr("MIFARE Classic dumps (*.bin *.dump *.eml *.txt *.json);;All files (*)"));
    if(files.isEmpty())
        return;
    importPaths(files);
}

void MfcLibraryPage::importFolder()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Import backups from folder"),
        m_library.rootDir().isEmpty() ? QDir::homePath() : m_library.rootDir());
    if(dir.isEmpty())
        return;
    const QStringList files = collectDumps(dir);
    if(files.isEmpty())
    {
        QMessageBox::information(this, tr("Import"),
                                 tr("No supported dump files were found in that folder."));
        return;
    }
    importPaths(files);
}

QStringList MfcLibraryPage::collectDumps(const QString& directory)
{
    QStringList files;
    QDirIterator it(directory, dumpNameFilters(), QDir::Files,
                    QDirIterator::Subdirectories);
    while(it.hasNext())
        files.append(it.next());
    files.sort();
    return files;
}

void MfcLibraryPage::importPaths(const QStringList& files)
{
    if(m_library.rootDir().isEmpty())
    {
        QMessageBox::information(
            this, tr("Import"),
            tr("Choose a backup library folder before importing files."));
        return;
    }
    if(files.isEmpty())
        return;

    int imported = 0;
    QStringList duplicates;
    QStringList failures;
    for(const QString& file : files)
    {
        Mfc::LibraryEntry entry;
        QString error;
        bool duplicate = false;
        if(m_library.importFile(file, &entry, &error, &duplicate))
        {
            if(duplicate)
                duplicates.append(QFileInfo(file).fileName());
            else
                imported++;
        }
        else
        {
            failures.append(tr("%1: %2").arg(
                QFileInfo(file).fileName(),
                error.isEmpty() ? tr("not a MIFARE Classic dump") : error));
        }
    }

    refresh();
    if(imported > 0)
        setStatus(tr("Imported %1 file(s).").arg(imported));

    QStringList details;
    if(!duplicates.isEmpty())
        details.append(tr("Already in the library:\n%1").arg(duplicates.join(QLatin1Char('\n'))));
    if(!failures.isEmpty())
        details.append(tr("Skipped invalid files:\n%1").arg(failures.join(QLatin1Char('\n'))));

    if(!details.isEmpty())
    {
        QMessageBox box(this);
        box.setIcon(failures.isEmpty() ? QMessageBox::Information : QMessageBox::Warning);
        box.setWindowTitle(tr("Import summary"));
        box.setText(tr("Imported %1 of %2 file(s).")
                        .arg(imported)
                        .arg(files.size()));
        box.setDetailedText(details.join(QStringLiteral("\n\n")));
        box.setInformativeText(tr("%1 duplicate(s), %2 invalid file(s). The source files were kept.")
                                   .arg(duplicates.size())
                                   .arg(failures.size()));
        box.exec();
    }
}

void MfcLibraryPage::dragEnterEvent(QDragEnterEvent* event)
{
    if(event->mimeData() != nullptr && event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void MfcLibraryPage::dropEvent(QDropEvent* event)
{
    if(event->mimeData() == nullptr || !event->mimeData()->hasUrls())
    {
        event->ignore();
        return;
    }
    QStringList files;
    const QList<QUrl> urls = event->mimeData()->urls();
    for(const QUrl& url : urls)
    {
        if(!url.isLocalFile())
            continue;
        const QString path = url.toLocalFile();
        const QFileInfo info(path);
        if(info.isDir())
            files.append(collectDumps(path));
        else if(info.isFile())
            files.append(path);
    }
    event->acceptProposedAction();
    if(files.isEmpty())
    {
        setStatus(tr("No supported dump files were dropped."));
        return;
    }
    importPaths(files);
}

void MfcLibraryPage::saveDetails()
{
    if(m_detailId.isEmpty())
        return;
    Mfc::LibraryEntry entry;
    if(!m_library.entry(m_detailId, &entry))
    {
        QMessageBox::warning(this, tr("Save details"),
                             tr("The selected entry is no longer in the library."));
        refresh();
        return;
    }
    entry.alias = aliasEdit->text().trimmed();
    entry.notes = notesEdit->toPlainText().trimmed();
    QStringList tags;
    QStringList rawTags;
#if (QT_VERSION <= QT_VERSION_CHECK(5,14,0))
    rawTags = tagsEdit->text().split(QLatin1Char(','), QString::SkipEmptyParts);
#else
    rawTags = tagsEdit->text().split(QLatin1Char(','), Qt::SkipEmptyParts);
#endif
    for(const QString& tag : rawTags)
    {
        const QString trimmed = tag.trimmed();
        if(!trimmed.isEmpty() && !tags.contains(trimmed))
            tags.append(trimmed);
    }
    entry.tags = tags;

    QString error;
    if(!m_library.updateEntry(entry, &error))
    {
        QMessageBox::warning(this, tr("Save details"),
                             tr("Cannot save the entry: %1").arg(error));
        return;
    }
    refresh();
    selectIds(QStringList() << entry.id);
    setStatus(tr("Saved details for %1.").arg(entryDisplayName(entry)));
}

void MfcLibraryPage::openInWorkbench()
{
    const QString id = m_detailId.isEmpty() ? currentEntryId() : m_detailId;
    Mfc::LibraryEntry entry;
    if(!entryById(id, &entry))
    {
        setStatus(tr("Select one entry to open in the workbench."));
        return;
    }

    Mfc::Document doc;
    const Mfc::IoResult loaded =
        Mfc::Document::loadDataFile(entry.filePath, QStringLiteral("auto"), &doc);
    if(!loaded.ok)
    {
        QMessageBox::warning(this, tr("Open in workbench"),
                             tr("Cannot read %1:\n%2")
                                 .arg(QDir::toNativeSeparators(entry.filePath), loaded.error));
        return;
    }
    const QString name = entry.alias.trimmed().isEmpty()
                             ? QFileInfo(entry.fileName).completeBaseName()
                             : entry.alias.trimmed();
    setStatus(tr("Opened %1 in the workbench.").arg(name));
    emit documentActivated(doc, name, QStringLiteral("library"));
}

void MfcLibraryPage::compareSelected()
{
    const QVector<Mfc::LibraryEntry> selected = selectedEntries();
    if(selected.size() != 2)
    {
        QMessageBox::information(this, tr("Compare"),
                                 tr("Select exactly two entries to compare."));
        return;
    }
    Mfc::Document a;
    Mfc::Document b;
    const Mfc::IoResult loadedA =
        Mfc::Document::loadDataFile(selected.at(0).filePath, QStringLiteral("auto"), &a);
    if(!loadedA.ok)
    {
        QMessageBox::warning(this, tr("Compare"),
                             tr("Cannot read %1:\n%2")
                                 .arg(QDir::toNativeSeparators(selected.at(0).filePath),
                                      loadedA.error));
        return;
    }
    const Mfc::IoResult loadedB =
        Mfc::Document::loadDataFile(selected.at(1).filePath, QStringLiteral("auto"), &b);
    if(!loadedB.ok)
    {
        QMessageBox::warning(this, tr("Compare"),
                             tr("Cannot read %1:\n%2")
                                 .arg(QDir::toNativeSeparators(selected.at(1).filePath),
                                      loadedB.error));
        return;
    }
    emit compareRequested(a, b);
}

void MfcLibraryPage::exportSelected()
{
    const QVector<Mfc::LibraryEntry> entries = selectedEntries();
    if(entries.isEmpty())
    {
        QMessageBox::information(this, tr("Export"),
                                 tr("Select at least one entry to export."));
        return;
    }
    const QString start = m_lastExportDir.isEmpty()
                              ? (m_library.rootDir().isEmpty() ? QDir::homePath()
                                                               : m_library.rootDir())
                              : m_lastExportDir;
    const QString directory = QFileDialog::getExistingDirectory(
        this, tr("Export directory"), start);
    if(directory.isEmpty())
        return;
    m_lastExportDir = directory;

    const QStringList formats = QStringList() << QStringLiteral("BIN")
                                              << QStringLiteral("EML")
                                              << QStringLiteral("JSON");
    bool accepted = false;
    const QString chosen = QInputDialog::getItem(this, tr("Export format"),
                                                 tr("Format:"), formats, 0, false,
                                                 &accepted);
    if(!accepted || chosen.isEmpty())
        return;
    const QString format = chosen.toLower();

    QString error;
    const QVector<Mfc::LibraryExportItem> items =
        m_library.planExport(entries, directory, format, &error);
    if(!error.isEmpty())
    {
        QMessageBox::warning(this, tr("Export"),
                             tr("Cannot plan the export: %1").arg(error));
        return;
    }

    int conflicts = 0;
    QStringList conflictNames;
    for(const Mfc::LibraryExportItem& item : items)
    {
        if(item.exists || item.duplicateName)
        {
            conflicts++;
            conflictNames.append(QFileInfo(item.targetPath).fileName());
        }
    }

    bool overwrite = true;
    if(conflicts > 0)
    {
        QMessageBox box(this);
        box.setIcon(QMessageBox::Warning);
        box.setWindowTitle(tr("Export conflicts"));
        box.setText(tr("%n target file(s) already exist or collide in the export directory.",
                       "", conflicts));
        box.setInformativeText(tr("Existing or duplicate targets:\n%1")
                                   .arg(conflictNames.join(QLatin1Char('\n'))));
        QPushButton* overwriteButton = box.addButton(tr("Overwrite"), QMessageBox::AcceptRole);
        QPushButton* skipButton = box.addButton(tr("Skip"), QMessageBox::RejectRole);
        QPushButton* cancelButton = box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(skipButton);
        box.exec();
        if(box.clickedButton() == nullptr || box.clickedButton() == cancelButton)
            return;
        overwrite = box.clickedButton() == overwriteButton;
    }

    int written = 0;
    int skipped = 0;
    QStringList failures;
    for(const Mfc::LibraryExportItem& item : items)
    {
        if(!overwrite && (item.exists || item.duplicateName))
        {
            skipped++;
            continue;
        }
        Mfc::LibraryEntry entry;
        if(!m_library.entry(item.id, &entry))
        {
            failures.append(tr("%1: the entry is no longer in the library")
                                .arg(QFileInfo(item.targetPath).fileName()));
            continue;
        }
        QString itemError;
        if(!Mfc::Library::exportEntry(entry, item.targetPath, format, &itemError))
        {
            failures.append(tr("%1: %2").arg(
                QFileInfo(item.targetPath).fileName(),
                itemError.isEmpty() ? tr("unknown export failure") : itemError));
            continue;
        }
        written++;
    }

    if(failures.isEmpty())
    {
        if(skipped > 0)
            setStatus(tr("Exported %1 file(s), skipped %2 conflict(s).")
                          .arg(written)
                          .arg(skipped));
        else
            setStatus(tr("Exported %1 file(s) to %2.")
                          .arg(written)
                          .arg(QDir::toNativeSeparators(directory)));
        return;
    }

    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(tr("Export summary"));
    box.setText(tr("Exported %1 file(s), %2 failed, %3 skipped.")
                    .arg(written)
                    .arg(failures.size())
                    .arg(skipped));
    box.setInformativeText(
        tr("A binary export refuses to write blocks that were never read, so some "
           "entries cannot be written as BIN. Export those as EML or JSON instead."));
    box.setDetailedText(failures.join(QLatin1Char('\n')));
    box.exec();
    setStatus(tr("Exported %1 file(s), %2 failed.").arg(written).arg(failures.size()));
}

void MfcLibraryPage::removeSelected()
{
    const QStringList ids = selectedIds();
    if(ids.isEmpty())
        return;

    QMessageBox box(this);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(tr("Remove from library"));
    box.setText(tr("Remove %n selected entries from the library?", "", ids.size()));
    box.setInformativeText(tr("The stored backup files are only deleted when you choose "
                              "to delete them."));
    QPushButton* entryOnlyButton = box.addButton(tr("Remove entry only"),
                                                 QMessageBox::AcceptRole);
    QPushButton* withFileButton = box.addButton(tr("Remove entry and file"),
                                                QMessageBox::DestructiveRole);
    QPushButton* cancelButton = box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(entryOnlyButton);
    box.exec();
    if(box.clickedButton() == nullptr || box.clickedButton() == cancelButton)
        return;
    const bool deleteFiles = box.clickedButton() == withFileButton;

    int removed = 0;
    QStringList failures;
    for(const QString& id : ids)
    {
        QString error;
        if(m_library.remove(id, deleteFiles, &error))
            removed++;
        else
            failures.append(error.isEmpty() ? id : error);
    }
    m_detailId.clear();
    refresh();
    if(!failures.isEmpty())
        QMessageBox::warning(this, tr("Remove from library"),
                             tr("Removed %1 entries. Failed:\n%2")
                                 .arg(removed)
                                 .arg(failures.join(QLatin1Char('\n'))));
    else
        setStatus(tr("Removed %1 entries from the library.").arg(removed));
}
