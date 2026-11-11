#ifndef DEMOMODE_H
#define DEMOMODE_H

#include <QHash>
#include <QString>

#include "module/mfcdocument.h"

class QWidget;

/*
 * Device-free demonstration mode.
 *
 * The provider answers the same client commands the workbench sends, so the
 * normal buttons, parsers and write-plan preview run unchanged.  Everything it
 * produces is marked as demo data: the documents carry a demo UID and the
 * workbench labels the session.  No real card, port or client process is
 * touched and the write overlay only exists in memory.
 */
namespace Demo
{

class Provider
{
public:
    static Provider& instance();

    bool active() const { return m_active; }
    void setActive(bool active);
    // Restores the canned card and drops any simulated writes.
    void reset();

    // Response for one client command (empty when the command is not part of
    // the demonstration).  The text uses the client's own prefixes so the
    // existing parsers see normal output.
    QString respond(const QString& command);
    bool handles(const QString& command) const;

    const Mfc::Document& card() const { return m_card; }
    Mfc::Document dumpA() const { return m_dumpA; }
    Mfc::Document dumpB() const { return m_dumpB; }
    QString mfuInfoText() const;
    QString mfuDumpText() const;
    QString mfuNdefText() const;
    QString traceText() const;

    // Writes the two demo dumps into `dir` and returns their paths.
    bool writeDumpFiles(const QString& dir, QString* pathA, QString* pathB,
                        QString* error = nullptr) const;

    static const char* const kDemoUid;
    static const quint64 kDemoSession = 0xD3D0ULL;

private:
    Provider();
    void buildCard();
    QString blockLine(int block) const;
    QString rdblResponse(int block, const QString& key, char type) const;
    QString wrblResponse(int block, const QString& key, char type, const QString& data);
    QString keyTable() const;

    bool m_active = false;
    Mfc::Document m_card;
    Mfc::Document m_dumpA;
    Mfc::Document m_dumpB;
    QHash<int, QString> m_written;
};

} // namespace Demo

#endif // DEMOMODE_H
