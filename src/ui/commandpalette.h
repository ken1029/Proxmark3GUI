#ifndef COMMANDPALETTE_H
#define COMMANDPALETTE_H

#include <QDialog>
#include <QString>
#include <QVector>

class QAction;
class QEvent;
class QLineEdit;
class QShowEvent;
class QTableWidget;

struct PaletteCommand
{
    QString section;
    QString text;
    QString shortcut;
    QAction *action;
};

class CommandPalette : public QDialog
{
    Q_OBJECT

public:
    explicit CommandPalette(QWidget *parent = nullptr);

    void setCommands(const QVector<PaletteCommand>& commands);
    static QString normalized(const QString& text);
    void focusSearch();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void showEvent(QShowEvent *event) override;

private slots:
    void applyFilter(const QString& text);
    void runCurrent();
    void runRow(int row, int column);

private:
    struct Entry
    {
        QString section;
        QString text;
        QString shortcut;
        QString reason;
        QAction *action;
        bool runnable;
        bool placeholder;
    };

    void rebuild();
    void selectRow(int row);
    void moveSelection(int delta);
    void runEntry(int index);
    static int rankOf(const Entry& entry, const QString& query);
    static bool isPrefixMatch(const QString& haystack, const QString& query);

    QLineEdit *searchEdit;
    QTableWidget *table;
    QVector<Entry> entries;
    QVector<Entry> visibleEntries;
};

#endif // COMMANDPALETTE_H
