#include "common/deviceidentity.h"

#include <QSettings>
#include <QStringList>
#include <QtSerialPort/QSerialPortInfo>

#include <algorithm>

namespace Device
{

bool Identity::isProxmark() const
{
    if(hasVidPid && ((vid == 0x9AC4 && pid == 0x4B8F) || (vid == 0x2D2D && pid == 0x504D)))
        return true;
    const QString text = (description + QLatin1Char(' ') + manufacturer).toLower();
    return text.contains(QStringLiteral("proxmark"))
           || text.contains(QStringLiteral("iceman"));
}

QString Identity::stableKey() const
{
    if(serial.isEmpty())
        return QString();
    if(hasVidPid)
        return QStringLiteral("%1:%2:%3").arg(vid, 4, 16, QLatin1Char('0'))
               .arg(pid, 4, 16, QLatin1Char('0')).arg(serial);
    return QStringLiteral("*:*:%1").arg(serial);
}

QString Identity::key() const
{
    const QString stable = stableKey();
    if(!stable.isEmpty())
        return stable + QLatin1Char('@') + portName;
    if(hasVidPid)
        return QStringLiteral("%1:%2@%3").arg(vid, 4, 16, QLatin1Char('0'))
               .arg(pid, 4, 16, QLatin1Char('0')).arg(portName);
    return QStringLiteral("%1|%2@%3").arg(description, manufacturer, portName);
}

QString Identity::displayName() const
{
    QString text = portName;
    if(!description.isEmpty())
        text += QStringLiteral(" - ") + description;
    if(!serial.isEmpty())
        text += QStringLiteral(" [") + serial + QLatin1Char(']');
    return text;
}

Identity Identity::fromPortInfo(const QSerialPortInfo& info)
{
    Identity identity;
    if(info.isNull())
        return identity;
    identity.portName = info.portName();
    identity.serial = info.serialNumber().trimmed();
    identity.description = info.description().trimmed();
    identity.manufacturer = info.manufacturer().trimmed();
    identity.systemLocation = info.systemLocation();
    if(info.hasVendorIdentifier() && info.hasProductIdentifier())
    {
        identity.vid = info.vendorIdentifier();
        identity.pid = info.productIdentifier();
        identity.hasVidPid = true;
    }
    return identity;
}

QVector<Identity> Identity::availablePorts()
{
    QVector<Identity> result;
    for(const QSerialPortInfo& info : QSerialPortInfo::availablePorts())
    {
        if(info.isNull())
            continue;
        result.append(fromPortInfo(info));
    }
    // Proxmark ports first, then a stable name order so the list does not jump.
    std::stable_sort(result.begin(), result.end(), [](const Identity& a, const Identity& b) {
        if(a.isProxmark() != b.isProxmark())
            return a.isProxmark();
        return a.portName < b.portName;
    });
    return result;
}

int Identity::match(const QVector<Identity>& ports, const Identity& last)
{
    if(last.portName.isEmpty())
        return -1;
    // The same port is still there: that is the strongest match.
    for(int i = 0; i < ports.size(); i++)
        if(ports[i].portName == last.portName && ports[i].key() == last.key())
            return i;
    if(last.serial.isEmpty())
        return -1;
    int found = -1;
    for(int i = 0; i < ports.size(); i++)
    {
        if(ports[i].serial.isEmpty() || ports[i].stableKey() != last.stableKey())
            continue;
        if(found >= 0)
            return -1; // several candidates: the user has to choose
        found = i;
    }
    return found;
}

Identity Identity::load(QSettings& settings)
{
    Identity identity;
    settings.beginGroup(QStringLiteral("Device"));
    identity.portName = settings.value(QStringLiteral("port")).toString();
    identity.serial = settings.value(QStringLiteral("serial")).toString();
    identity.description = settings.value(QStringLiteral("description")).toString();
    identity.manufacturer = settings.value(QStringLiteral("manufacturer")).toString();
    identity.systemLocation = settings.value(QStringLiteral("location")).toString();
    identity.vid = static_cast<quint16>(settings.value(QStringLiteral("vid")).toUInt());
    identity.pid = static_cast<quint16>(settings.value(QStringLiteral("pid")).toUInt());
    identity.hasVidPid = settings.value(QStringLiteral("hasVidPid"), false).toBool();
    settings.endGroup();
    return identity;
}

void Identity::save(QSettings& settings) const
{
    settings.beginGroup(QStringLiteral("Device"));
    settings.setValue(QStringLiteral("port"), portName);
    settings.setValue(QStringLiteral("serial"), serial);
    settings.setValue(QStringLiteral("description"), description);
    settings.setValue(QStringLiteral("manufacturer"), manufacturer);
    settings.setValue(QStringLiteral("location"), systemLocation);
    settings.setValue(QStringLiteral("vid"), vid);
    settings.setValue(QStringLiteral("pid"), pid);
    settings.setValue(QStringLiteral("hasVidPid"), hasVidPid);
    settings.endGroup();
}

} // namespace Device
