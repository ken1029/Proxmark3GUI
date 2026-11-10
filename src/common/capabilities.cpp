#include "capabilities.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcessEnvironment>

Pm3Capabilities::Pm3Capabilities()
{
    loaded = false;
    flashSize = 0;
}

Pm3Capabilities Pm3Capabilities::load(const QString& clientPath, const QString& workingDir)
{
    Pm3Capabilities caps;
    QStringList candidates;
    const QString envPath = QProcessEnvironment::systemEnvironment().value("PM3_CAPABILITIES");
    if(!envPath.isEmpty())
        candidates << envPath;

    const QDir appDir(QCoreApplication::applicationDirPath());
    candidates << appDir.absoluteFilePath("pm3_capabilities.json");

    if(!clientPath.isEmpty())
    {
        const QFileInfo clientInfo(clientPath);
        const QDir clientDir = clientInfo.absoluteDir();
        candidates << clientDir.absoluteFilePath("pm3_capabilities.json");
        candidates << clientDir.absoluteFilePath("../config/pm3_capabilities.json");
    }
    if(!workingDir.isEmpty())
    {
        const QDir workDir(workingDir);
        candidates << workDir.absoluteFilePath("pm3_capabilities.json");
        candidates << workDir.absoluteFilePath("../config/pm3_capabilities.json");
    }

    for(const QString& candidate : candidates)
    {
        QFile file(candidate);
        if(!file.exists() || !file.open(QFile::ReadOnly | QFile::Text))
            continue;
        QJsonParseError error;
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
        if(error.error != QJsonParseError::NoError || !doc.isObject())
            continue;

        const QJsonObject root = doc.object();
        const QJsonObject firmware = root.value("firmware").toObject();
        caps.version = firmware.value("version").toString();
        caps.commit = firmware.value("commit").toString();
        caps.platformName = firmware.value("platform").toString();
        caps.flashSize = firmware.value("platform_size").toInt();

        const QJsonObject features = root.value("features").toObject();
        for(auto it = features.begin(); it != features.end(); ++it)
            caps.features.insert(it.key(), it.value().toBool());

        const QJsonArray removed = root.value("removed").toArray();
        for(const QJsonValue& item : removed)
            caps.removed << item.toString();

        caps.loaded = true;
        caps.path = QFileInfo(candidate).absoluteFilePath();
        break;
    }
    return caps;
}

bool Pm3Capabilities::isLoaded() const
{
    return loaded;
}

bool Pm3Capabilities::has(const QString& feature) const
{
    if(!loaded)
        return true; // unknown, do not disable anything
    return features.value(feature, true);
}

QString Pm3Capabilities::reason(const QString& feature) const
{
    if(!loaded)
        return QString();
    return QString("Firmware %1 was built without %2").arg(version, feature.toUpper());
}

QString Pm3Capabilities::manifestPath() const
{
    return path;
}

QString Pm3Capabilities::firmwareVersion() const
{
    return version;
}

QString Pm3Capabilities::firmwareCommit() const
{
    return commit;
}

QString Pm3Capabilities::platform() const
{
    return platformName;
}

int Pm3Capabilities::platformSize() const
{
    return flashSize;
}

QStringList Pm3Capabilities::removedFeatures() const
{
    return removed;
}
