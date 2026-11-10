#include "pm3cmd.h"

namespace Pm3Cmd
{

QStringList jsonStringList(const QVariant& value)
{
    QStringList list;
    if(value.type() == QVariant::StringList)
        return value.toStringList();
    if(value.type() == QVariant::String)
    {
        if(!value.toString().isEmpty())
            list << value.toString();
        return list;
    }
    const QVariantList items = value.toList();
    for(const QVariant& item : items)
    {
        if(!item.toString().isEmpty())
            list << item.toString();
    }
    return list;
}

Util::ReturnTrigger makeWriteTrigger(const QVariantMap& config, int waitTime)
{
    Util::ReturnTrigger trigger(qMax<unsigned long>((unsigned long)waitTime, 2000));
    trigger.hardTimeout = qMax<unsigned long>(trigger.waitTime * 5, 10000);
    // Pipe mode has no trailing prompt: "rem <token>" gives the write a real
    // completion boundary instead of waiting for the idle timeout.
    trigger.requireCompletion = true;
    trigger.useCompletionMarker = true;
    trigger.expectedOutputs = jsonStringList(config["success pattern"]);
    trigger.failedOutputs = jsonStringList(config["failed flag"]);
    if(trigger.failedOutputs.isEmpty())
    {
        trigger.failedOutputs = QStringList({
            "\\[-\\]",
            "command execution time out",
            "not available in this mode",
            "Write \\( fail \\)",
            "Can't write",
            "failed",
            "error"
        });
    }
    return trigger;
}

QString traceSaveCommand(const QVariantMap& config, bool useBuffer, const QString& filename)
{
    QString cmd;
    if(useBuffer && config.contains("buffer cmd"))
        cmd = config["buffer cmd"].toString();
    else
        cmd = config["cmd"].toString();
    cmd.replace("<filename>", filename);
    return cmd;
}

QString normalizedHex(const QString& data)
{
    QString result = data.toUpper();
    result.remove(" ");
    result.remove(":");
    result.remove("\r");
    result.remove("\n");
    return result;
}

}
