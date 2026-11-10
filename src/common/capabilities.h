#ifndef CAPABILITIES_H
#define CAPABILITIES_H

#include <QString>
#include <QStringList>
#include <QMap>
#include <QJsonObject>

// Read-only view of the firmware capability manifest built next to the client.
// A missing manifest means "unknown", so the GUI keeps everything enabled and
// the client still reports unsupported commands as failures.
class Pm3Capabilities
{
public:
    Pm3Capabilities();

    static Pm3Capabilities load(const QString& clientPath, const QString& workingDir);

    bool isLoaded() const;
    bool has(const QString& feature) const;
    QString reason(const QString& feature) const;
    QString manifestPath() const;
    QString firmwareVersion() const;
    QString firmwareCommit() const;
    QString platform() const;
    int platformSize() const;
    QStringList removedFeatures() const;

private:
    bool loaded;
    QString path;
    QString version;
    QString commit;
    QString platformName;
    int flashSize;
    QMap<QString, bool> features;
    QStringList removed;
};

#endif // CAPABILITIES_H
