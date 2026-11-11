#ifndef DEVICEIDENTITY_H
#define DEVICEIDENTITY_H

#include <QString>
#include <QVector>

class QSettings;
class QSerialPortInfo;

/*
 * Stable identity of a serial device.
 *
 * Windows renumbers COM ports freely, so the port name alone is not an
 * identity.  The USB serial number, VID and PID are; when the OS does not
 * provide a serial number the port name is all that is left, and the caller
 * must ask the user instead of guessing between several Proxmarks.
 */
namespace Device
{

struct Identity
{
    QString portName;
    QString serial;
    QString description;
    QString manufacturer;
    QString systemLocation;
    quint16 vid = 0;
    quint16 pid = 0;
    bool hasVidPid = false;

    bool isNull() const { return portName.isEmpty(); }
    bool isProxmark() const;
    // Identity without the port name.  Empty when the OS gave no serial number.
    QString stableKey() const;
    // Full identity including the port name (what was last used on this PC).
    QString key() const;
    QString displayName() const;

    static Identity fromPortInfo(const QSerialPortInfo& info);
    static QVector<Identity> availablePorts();
    // Index of the port that matches the stored identity, or -1 when the device
    // is absent or ambiguous (several ports share the same stable key).
    static int match(const QVector<Identity>& ports, const Identity& last);

    static Identity load(QSettings& settings);
    void save(QSettings& settings) const;
};

} // namespace Device

#endif // DEVICEIDENTITY_H
