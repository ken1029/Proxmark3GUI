#ifndef MFCLIBRARYPAGE_H
#define MFCLIBRARYPAGE_H

#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include "module/mfcdocument.h"
#include "module/mfclibrary.h"

class QCheckBox;
class QComboBox;
class QDateEdit;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;
class QDragEnterEvent;
class QDropEvent;

/*
 * MIFARE Classic backup library browser.
 *
 * The page is a thin view over Mfc::Library: it filters, shows the stored
 * backups, edits their descriptive fields and hands documents to the rest of
 * the GUI.  It performs no device access and never writes library.json by
 * hand; every change goes through Library::updateEntry()/remove() and is saved
 * atomically by the model.
 */
class MfcLibraryPage : public QWidget
{
    Q_OBJECT
public:
    explicit MfcLibraryPage(QWidget* parent = nullptr);

    // Directory that holds library.json and files/.  Reloads the index.
    void setLibraryRoot(const QString& dir);
    Mfc::Library& library() { return m_library; }

    // Re-reads the index from disk and rebuilds the table.
    void refresh();

signals:
    void documentActivated(const Mfc::Document& doc, const QString& name, const QString& sourceKind);
    void compareRequested(const Mfc::Document& a, const Mfc::Document& b);
    void statusMessage(const QString& text);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void chooseFolder();
    void importFiles();
    void importFolder();
    void applyFilter();
    void selectionChanged();
    void saveDetails();
    void openInWorkbench();
    void compareSelected();
    void exportSelected();
    void removeSelected();

private:
    void buildUi();
    void loadLibrary();
    void populateTable();
    void updateDetailPanel();
    void setStatus(const QString& text);

    QStringList selectedIds() const;
    QVector<Mfc::LibraryEntry> selectedEntries() const;
    bool entryById(const QString& id, Mfc::LibraryEntry* out) const;
    QString currentEntryId() const;
    void selectIds(const QStringList& ids);

    void importPaths(const QStringList& files);
    static QStringList collectDumps(const QString& directory);

    Mfc::Library m_library;
    QVector<Mfc::LibraryEntry> m_visible;
    QString m_detailId;
    QString m_loadError;
    QString m_lastExportDir;

    QLabel* rootLabel = nullptr;
    QLineEdit* searchEdit = nullptr;
    QComboBox* typeCombo = nullptr;
    QLineEdit* uidEdit = nullptr;
    QComboBox* completenessCombo = nullptr;
    QCheckBox* duplicatesCheck = nullptr;
    QCheckBox* fromCheck = nullptr;
    QDateEdit* fromEdit = nullptr;
    QCheckBox* toCheck = nullptr;
    QDateEdit* toEdit = nullptr;
    QTableWidget* table = nullptr;
    QLineEdit* aliasEdit = nullptr;
    QPlainTextEdit* notesEdit = nullptr;
    QLineEdit* tagsEdit = nullptr;
    QPushButton* saveButton = nullptr;
    QPushButton* openButton = nullptr;
    QPushButton* compareButton = nullptr;
    QPushButton* exportButton = nullptr;
    QPushButton* removeButton = nullptr;
    QLabel* statusLabel = nullptr;
};

#endif // MFCLIBRARYPAGE_H
