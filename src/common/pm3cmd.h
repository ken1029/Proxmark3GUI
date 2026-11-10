#ifndef PM3CMD_H
#define PM3CMD_H

#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

#include "util.h"

// Helpers shared by the command modules and the offline tests.
namespace Pm3Cmd
{
QStringList jsonStringList(const QVariant& value);

// Build the completion/failure rules for a MIFARE write command.
Util::ReturnTrigger makeWriteTrigger(const QVariantMap& config, int waitTime);

// Pick "trace save -f" (device) or "trace save --buffer -f" (client buffer).
QString traceSaveCommand(const QVariantMap& config, bool useBuffer, const QString& filename);

// Empty result means "no success marker" (timeout or failure).
QString normalizedHex(const QString& data);
}

#endif // PM3CMD_H
