#include "commandpalette.h"

#include <QAbstractItemView>
#include <QAction>
#include <QHeaderView>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLineEdit>
#include <QPair>
#include <QShowEvent>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <algorithm>

namespace
{
const int kColumnText = 0;
const int kColumnShortcut = 1;

// QAction text may carry '&' mnemonic markers; they are noise in a search list.
QString plainText(const QString& text)
{
    QString result = text;
    result.replace(QStringLiteral("&&"), QStringLiteral("\x01"));
    result.remove(QLatin1Char('&'));
    result.replace(QStringLiteral("\x01"), QStringLiteral("&"));
    return result;
}
}

CommandPalette::CommandPalette(QWidget *parent) :
    QDialog(parent),
    searchEdit(new QLineEdit(this)),
    table(new QTableWidget(this))
{
    setWindowTitle(tr("Command Palette"));
    resize(640, 420);
    setModal(true);

    searchEdit->setPlaceholderText(tr("Type a command…"));
    searchEdit->setClearButtonEnabled(true);

    table->setColumnCount(2);
    table->setShowGrid(false);
    table->setWordWrap(true);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setFocusPolicy(Qt::NoFocus);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setVisible(false);
    table->horizontalHeader()->setSectionResizeMode(kColumnText, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(kColumnShortcut, QHeaderView::ResizeToContents);
    table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);
    layout->addWidget(searchEdit);
    layout->addWidget(table, 1);

    searchEdit->installEventFilter(this);
    table->installEventFilter(this);

    connect(searchEdit, &QLineEdit::textChanged, this, &CommandPalette::applyFilter);
    connect(searchEdit, &QLineEdit::returnPressed, this, &CommandPalette::runCurrent);
    connect(table, &QTableWidget::cellDoubleClicked, this, &CommandPalette::runRow);
}

void CommandPalette::setCommands(const QVector<PaletteCommand>& commands)
{
    entries.clear();
    entries.reserve(commands.size());
    for(int i = 0; i < commands.size(); i++)
    {
        const PaletteCommand &command = commands.at(i);
        Entry entry;
        entry.section = plainText(command.section);
        entry.text = plainText(command.text);
        entry.shortcut = command.shortcut;
        entry.action = command.action;
        entry.placeholder = false;
        entry.runnable = command.action != nullptr && command.action->isEnabled();
        if(entry.shortcut.isEmpty() && command.action != nullptr)
        {
            entry.shortcut = command.action->shortcut().toString(QKeySequence::NativeText);
        }
        if(command.action != nullptr && !entry.runnable)
        {
            entry.reason = command.action->toolTip();
            if(entry.reason.isEmpty())
            {
                entry.reason = command.action->statusTip();
            }
            if(entry.reason.isEmpty())
            {
                entry.reason = command.action->whatsThis();
            }
        }
        if(entry.text.isEmpty() && command.action != nullptr)
        {
            entry.text = plainText(command.action->text());
        }
        entries.append(entry);
    }
    rebuild();
}

QString CommandPalette::normalized(const QString& text)
{
    return text.simplified().toLower();
}

void CommandPalette::focusSearch()
{
    searchEdit->setFocus(Qt::ShortcutFocusReason);
    searchEdit->selectAll();
}

bool CommandPalette::eventFilter(QObject *watched, QEvent *event)
{
    if(watched == searchEdit || watched == table)
    {
        if(event->type() == QEvent::KeyPress)
        {
            QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
            switch(keyEvent->key())
            {
            case Qt::Key_Up:
                moveSelection(-1);
                return true;
            case Qt::Key_Down:
                moveSelection(1);
                return true;
            case Qt::Key_PageUp:
                moveSelection(-10);
                return true;
            case Qt::Key_PageDown:
                moveSelection(10);
                return true;
            case Qt::Key_Return:
            case Qt::Key_Enter:
                runCurrent();
                return true;
            case Qt::Key_Escape:
                reject();
                return true;
            default:
                break;
            }
        }
    }
    return QDialog::eventFilter(watched, event);
}

void CommandPalette::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    focusSearch();
}

void CommandPalette::applyFilter(const QString& text)
{
    Q_UNUSED(text);
    rebuild();
}

int CommandPalette::rankOf(const Entry& entry, const QString& query)
{
    const QString haystack = normalized(entry.section + QLatin1Char(' ') + entry.text);
    const QString shortcut = normalized(entry.shortcut);
    if(query.isEmpty())
    {
        return 0;
    }
    if(isPrefixMatch(haystack, query) || (!shortcut.isEmpty() && shortcut.startsWith(query)))
    {
        return 0;
    }
    if(haystack.contains(query) || shortcut.contains(query))
    {
        return 1;
    }
    return -1;
}

bool CommandPalette::isPrefixMatch(const QString& haystack, const QString& query)
{
    const QStringList tokens = haystack.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for(int i = 0; i < tokens.size(); i++)
    {
        if(tokens.at(i).startsWith(query))
        {
            return true;
        }
    }
    return false;
}

void CommandPalette::rebuild()
{
    const QString query = normalized(searchEdit->text());

    QVector<QPair<int, int> > matches;
    matches.reserve(entries.size());
    for(int i = 0; i < entries.size(); i++)
    {
        const int rank = rankOf(entries.at(i), query);
        if(rank >= 0)
        {
            matches.append(qMakePair(rank, i));
        }
    }
    std::stable_sort(matches.begin(), matches.end(), [](const QPair<int, int> &left, const QPair<int, int> &right)
    {
        return left.first < right.first;
    });

    visibleEntries.clear();
    table->clearContents();

    if(matches.isEmpty())
    {
        Entry placeholder;
        placeholder.section.clear();
        placeholder.shortcut.clear();
        placeholder.reason.clear();
        placeholder.action = nullptr;
        placeholder.placeholder = true;
        placeholder.runnable = false;
        placeholder.text = tr("No matching command");
        visibleEntries.append(placeholder);
    }
    else
    {
        for(int i = 0; i < matches.size(); i++)
        {
            visibleEntries.append(entries.at(matches.at(i).second));
        }
    }

    table->setRowCount(visibleEntries.size());
    for(int row = 0; row < visibleEntries.size(); row++)
    {
        const Entry &entry = visibleEntries.at(row);
        QString label = entry.section.isEmpty()
                        ? entry.text
                        : tr("%1 — %2").arg(entry.section, entry.text);
        if(!entry.runnable && !entry.reason.isEmpty())
        {
            label += QLatin1Char('\n') + entry.reason;
        }

        QTableWidgetItem *labelItem = new QTableWidgetItem(label);
        QTableWidgetItem *shortcutItem = new QTableWidgetItem(entry.shortcut);
        shortcutItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        const Qt::ItemFlags flags = entry.runnable
                                    ? (Qt::ItemIsEnabled | Qt::ItemIsSelectable)
                                    : Qt::ItemIsSelectable;
        labelItem->setFlags(flags);
        shortcutItem->setFlags(flags);
        if(!entry.reason.isEmpty())
        {
            labelItem->setToolTip(entry.reason);
            shortcutItem->setToolTip(entry.reason);
        }
        table->setItem(row, kColumnText, labelItem);
        table->setItem(row, kColumnShortcut, shortcutItem);
    }
    table->resizeRowsToContents();
    if(table->rowCount() > 0)
    {
        selectRow(0);
    }
}

void CommandPalette::selectRow(int row)
{
    if(row < 0 || row >= table->rowCount())
    {
        return;
    }
    table->setCurrentCell(row, kColumnText);
    table->scrollToItem(table->item(row, kColumnText), QAbstractItemView::EnsureVisible);
}

void CommandPalette::moveSelection(int delta)
{
    if(table->rowCount() == 0)
    {
        return;
    }
    int row = table->currentRow();
    if(row < 0)
    {
        row = 0;
    }
    else
    {
        row += delta;
    }
    row = qBound(0, row, table->rowCount() - 1);
    selectRow(row);
}

void CommandPalette::runCurrent()
{
    runEntry(table->currentRow());
}

void CommandPalette::runRow(int row, int column)
{
    Q_UNUSED(column);
    runEntry(row);
}

void CommandPalette::runEntry(int index)
{
    if(index < 0 || index >= visibleEntries.size())
    {
        return;
    }
    const Entry &entry = visibleEntries.at(index);
    if(entry.placeholder || !entry.runnable || entry.action == nullptr)
    {
        return;
    }
    if(!entry.action->isEnabled())
    {
        return;
    }
    QAction *action = entry.action;
    close();
    action->trigger();
}
