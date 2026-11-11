#include "mfckeyrecovery.h"

#include <QRegularExpression>
#include <QVariantMap>

namespace MfcRecovery
{

QString methodName(Method method)
{
    switch(method)
    {
    case Method::KnownKeys:
        return QStringLiteral("known keys");
    case Method::Dictionary:
        return QStringLiteral("dictionary");
    case Method::AutoPwn:
        return QStringLiteral("autopwn");
    case Method::Nested:
        return QStringLiteral("nested");
    case Method::StaticNested:
        return QStringLiteral("staticnested");
    case Method::Hardnested:
        return QStringLiteral("hardnested");
    case Method::Darkside:
        return QStringLiteral("darkside");
    case Method::Sen:
        return QStringLiteral("sen");
    case Method::Isen:
        return QStringLiteral("isen");
    }
    return QStringLiteral("unknown");
}

QString methodKey(Method method)
{
    switch(method)
    {
    case Method::KnownKeys:
        return QStringLiteral("known");
    case Method::Dictionary:
        return QStringLiteral("check");
    case Method::AutoPwn:
        return QStringLiteral("autopwn");
    case Method::Nested:
        return QStringLiteral("nested");
    case Method::StaticNested:
        return QStringLiteral("staticnested");
    case Method::Hardnested:
        return QStringLiteral("hardnested");
    case Method::Darkside:
        return QStringLiteral("darkside");
    case Method::Sen:
        return QStringLiteral("sen");
    case Method::Isen:
        return QStringLiteral("isen");
    }
    return QStringLiteral("unknown");
}

QString nonceTypeName(NonceType type)
{
    switch(type)
    {
    case NonceType::Unknown:
        return QStringLiteral("unknown");
    case NonceType::Normal:
        return QStringLiteral("normal");
    case NonceType::Static:
        return QStringLiteral("static");
    case NonceType::SuperStatic:
        return QStringLiteral("superstatic");
    case NonceType::StaticEncrypted:
        return QStringLiteral("static-encrypted");
    }
    return QStringLiteral("unknown");
}

QString taskStateName(TaskState state)
{
    switch(state)
    {
    case TaskState::Idle:
        return QStringLiteral("idle");
    case TaskState::Running:
        return QStringLiteral("running");
    case TaskState::Cancelling:
        return QStringLiteral("cancelling");
    case TaskState::Completed:
        return QStringLiteral("completed");
    case TaskState::Partial:
        return QStringLiteral("partial");
    case TaskState::Failed:
        return QStringLiteral("failed");
    case TaskState::Cancelled:
        return QStringLiteral("cancelled");
    case TaskState::Aborted:
        return QStringLiteral("aborted");
    }
    return QStringLiteral("idle");
}

static Mfc::KeySource sourceFromMarker(char marker)
{
    switch(marker)
    {
    case 'D':
        return Mfc::KeySource::Dictionary;
    case 'S':
    case 'N':
    case 'H':
    case 'C':
        return Mfc::KeySource::Recovery;
    case 'U':
    case 'A':
        return Mfc::KeySource::Manual;
    default:
        return Mfc::KeySource::Recovery;
    }
}

static bool isFoundMarker(const QString& marker)
{
    if(marker.isEmpty())
        return false;
    const QChar c = marker.at(0);
    return c != QLatin1Char('0') && c != QLatin1Char('-') && c != QLatin1Char('?');
}

ParseResult parseOutput(const QString& cleanOutput, const Mfc::Spec& spec)
{
    ParseResult result;

    QRegularExpression nonceEnc(QStringLiteral("Static\\s+enc(?:rypted)?\\s+nonce\\s*\\.*\\s*(?:yes|YES)"),
                                QRegularExpression::CaseInsensitiveOption);
    QRegularExpression nonceSuper(QStringLiteral("Static\\s+nonce\\s*\\.*\\s*yes\\s*,\\s*even\\s+when\\s+nested"),
                                  QRegularExpression::CaseInsensitiveOption);
    QRegularExpression nonceStatic(QStringLiteral("Static\\s+nonce\\s*\\.*\\s*(?:yes|YES)"),
                                   QRegularExpression::CaseInsensitiveOption);
    // "Static nonce....... 009080a2" - the client prints the nonce itself
    QRegularExpression nonceStaticValue(QStringLiteral("Static\\s+nonce\\s*\\.*\\s*[0-9A-Fa-f]{8,}"),
                                        QRegularExpression::CaseInsensitiveOption);
    QRegularExpression nonceDetectedEnc(QStringLiteral("Static\\s+encrypted\\s+nonce\\s+detected"),
                                        QRegularExpression::CaseInsensitiveOption);
    QRegularExpression nonceDetected(QStringLiteral("Static\\s+nonce\\s+detected"),
                                     QRegularExpression::CaseInsensitiveOption);
    QRegularExpression prng(QStringLiteral("Prng\\s*\\.*\\s*(?:weak|hard)"),
                            QRegularExpression::CaseInsensitiveOption);

    if(nonceEnc.match(cleanOutput).hasMatch() || nonceDetectedEnc.match(cleanOutput).hasMatch())
        result.nonce = NonceType::StaticEncrypted;
    else if(nonceSuper.match(cleanOutput).hasMatch())
        result.nonce = NonceType::SuperStatic;
    else if(nonceStatic.match(cleanOutput).hasMatch() || nonceDetected.match(cleanOutput).hasMatch()
            || nonceStaticValue.match(cleanOutput).hasMatch())
        result.nonce = NonceType::Static;
    else if(prng.match(cleanOutput).hasMatch())
        result.nonce = NonceType::Normal;

    // Sector | trailer block | key A | result | key B | result
    // The sector and block columns are authoritative; row order is not.
    // The client prefixes every line with "[+] " (or "[=] ", "[-] "), so the
    // prefix must be optional or no row ever matches.
    QRegularExpression row(QStringLiteral("^[^\\d\\n]*(\\d{1,3})\\s*\\|\\s*(\\d{1,3})\\s*\\|"
                                          "\\s*([0-9A-Fa-f]{12}|-{3,})\\s*\\|\\s*([0-9A-Za-z*])\\s*\\|"
                                          "\\s*([0-9A-Fa-f]{12}|-{3,})\\s*\\|\\s*([0-9A-Za-z*])"),
                           QRegularExpression::MultilineOption);

    QRegularExpressionMatchIterator it = row.globalMatch(cleanOutput);
    while(it.hasNext())
    {
        const QRegularExpressionMatch match = it.next();
        const int sector = match.captured(1).toInt();
        const int block = match.captured(2).toInt();
        if(sector < 0 || sector >= spec.sectorCount)
        {
            result.warnings << QStringLiteral("row with sector %1 is outside the card; ignored")
                               .arg(sector);
            continue;
        }
        if(block != spec.trailerBlockOfSector(sector))
        {
            result.warnings << QStringLiteral("row sector %1 block %2 is not its trailer (%3); ignored")
                               .arg(sector).arg(block).arg(spec.trailerBlockOfSector(sector));
            continue;
        }
        result.sawTable = true;
        const QString keyA = match.captured(3).toUpper();
        const QString keyB = match.captured(5).toUpper();
        const QString resA = match.captured(4);
        const QString resB = match.captured(6);

        Candidate a;
        a.sector = sector;
        a.type = 'A';
        a.key = keyA;
        a.marker = resA.isEmpty() ? '\0' : resA.at(0).toLatin1();
        a.found = Mfc::Document::isKeyText(keyA) && isFoundMarker(resA);
        if(a.found)
            a.source = sourceFromMarker(a.marker);
        result.keys.append(a);

        Candidate b;
        b.sector = sector;
        b.type = 'B';
        b.key = keyB;
        b.marker = resB.isEmpty() ? '\0' : resB.at(0).toLatin1();
        b.found = Mfc::Document::isKeyText(keyB) && isFoundMarker(resB);
        if(b.found)
            b.source = sourceFromMarker(b.marker);
        result.keys.append(b);
    }

    if(cleanOutput.contains(QStringLiteral("Found valid key"))
       || cleanOutput.contains(QStringLiteral("key not found"))
       || cleanOutput.contains(QStringLiteral("Done!"))
       || cleanOutput.contains(QStringLiteral("time in ")))
        result.sawCompletion = true;
    if(cleanOutput.contains(QStringLiteral("No tag found"))
       || cleanOutput.contains(QStringLiteral("Can't select card"))
       || cleanOutput.contains(QStringLiteral("command execution time out")))
        result.sawFailure = true;

    return result;
}

MergeResult merge(const ParseResult& parsed, Mfc::Document* document,
                  bool markVerified, Mfc::KeySource fallbackSource, quint64 session)
{
    MergeResult result;
    if(document == nullptr)
        return result;

    for(const Candidate& candidate : parsed.keys)
    {
        if(candidate.sector < 0 || candidate.sector >= document->sectorCount())
        {
            result.notes << QStringLiteral("sector %1 out of range; ignored").arg(candidate.sector);
            continue;
        }
        if(!Mfc::Document::isKeyText(candidate.key))
            continue;

        const Mfc::KeyEntry current = document->key(candidate.sector, candidate.type);
        const QString where = QStringLiteral("sector %1 key %2").arg(candidate.sector).arg(candidate.type);

        if(candidate.found)
        {
            if(current.value == candidate.key)
            {
                if(markVerified && current.state != Mfc::KeyState::Verified)
                {
                    document->setKeyVerified(candidate.sector, candidate.type, candidate.key,
                                             candidate.source == Mfc::KeySource::None ? fallbackSource : candidate.source,
                                             session);
                    result.updated++;
                }
                continue;
            }
            if(current.state == Mfc::KeyState::Verified)
            {
                result.conflicts++;
                result.notes << QStringLiteral("%1: verified %2 kept, recovered %3 ignored")
                                .arg(where, current.value, candidate.key);
                continue;
            }
            if(current.value.isEmpty())
                result.added++;
            else
                result.updated++;
            if(markVerified)
                document->setKeyVerified(candidate.sector, candidate.type, candidate.key,
                                         candidate.source == Mfc::KeySource::None ? fallbackSource : candidate.source,
                                         session);
            else
                document->setKey(candidate.sector, candidate.type, candidate.key, Mfc::KeyState::Pending,
                                 candidate.source == Mfc::KeySource::None ? fallbackSource : candidate.source);
        }
        else if(!candidate.key.isEmpty() && current.value == candidate.key
                && current.state == Mfc::KeyState::Pending)
        {
            // The table explicitly reports "not found" for a candidate we
            // imported; record the failed authentication instead of keeping it
            // silently pending.
            document->setKey(candidate.sector, candidate.type, candidate.key,
                             Mfc::KeyState::Failed, current.source);
        }
    }
    return result;
}

QVector<Advice> advise(const Mfc::Document& document, NonceType nonce)
{
    QVector<Advice> list;
    int values = 0;
    for(int s = 0; s < document.sectorCount(); s++)
        for(char type : {'A', 'B'})
            if(document.key(s, type).hasValue())
                values++;

    const int verified = document.verifiedKeyCount();
    const int total = document.keyCount();
    const bool hasKey = values > 0;
    const bool keysComplete = verified == total;
    const bool staticNonce = nonce == NonceType::Static || nonce == NonceType::SuperStatic;

    auto add = [&list](Method method, bool applicable, bool unverified,
                       const QString& reason, const QStringList& requirements = QStringList()) {
        Advice advice;
        advice.method = method;
        advice.applicable = applicable;
        advice.preconditionUnverified = unverified;
        advice.reason = reason;
        advice.requirements = requirements;
        list.append(advice);
    };

    add(Method::KnownKeys, verified > 0, false,
        verified > 0
        ? QStringLiteral("%1 of %2 keys are authenticated on this card").arg(verified).arg(total)
        : QStringLiteral("no key has been authenticated yet"),
        QStringList({QStringLiteral("a known key or a verified recovery result")}));

    add(Method::Dictionary, !keysComplete, false,
        keysComplete ? QStringLiteral("all keys are already verified")
                     : QStringLiteral("try the built-in defaults or a custom dictionary; "
                                      "only a hit is a candidate until it is verified"),
        QStringList({QStringLiteral("ISO14443A card")}));

    add(Method::AutoPwn, !keysComplete, nonce == NonceType::Unknown,
        keysComplete ? QStringLiteral("all keys are already verified")
                     : QStringLiteral("wraps fchk/chk/darkside/nested/hardnested/staticnested; "
                                      "use it when no method has been chosen yet"),
        QStringList({QStringLiteral("ISO14443A card"), QStringLiteral("known or recoverable key")}));

    add(Method::Nested, hasKey && !staticNonce && nonce != NonceType::StaticEncrypted,
        hasKey && nonce == NonceType::Unknown,
        !hasKey ? QStringLiteral("needs at least one authenticated key on the card")
                : staticNonce ? QStringLiteral("card presents a static nonce; use staticnested")
                              : nonce == NonceType::StaticEncrypted
                                ? QStringLiteral("card presents a static encrypted nonce; use sen")
                                : QStringLiteral("uses a known key to recover the others"),
        QStringList({QStringLiteral("one authenticated key"),
                     QStringLiteral("normal PRNG (nonce type not static)")}));

    add(Method::StaticNested, staticNonce, nonce == NonceType::Unknown,
        staticNonce ? QStringLiteral("card presents a static nonce")
                    : nonce == NonceType::Unknown
                      ? QStringLiteral("nonce type is not known yet; run info/isen first")
                      : QStringLiteral("only applies to static-nonce cards"),
        QStringList({QStringLiteral("static nonce or static nonce even when nested")}));

    add(Method::Hardnested, hasKey && !staticNonce && nonce != NonceType::StaticEncrypted,
        hasKey && nonce == NonceType::Unknown,
        !hasKey ? QStringLiteral("needs at least one authenticated key on the card")
                : staticNonce ? QStringLiteral("static-nonce card: hardnested is not the right tool")
                              : nonce == NonceType::StaticEncrypted
                                ? QStringLiteral("static encrypted nonce: use sen")
                                : QStringLiteral("recovers a key with a known key and nonce collection"),
        QStringList({QStringLiteral("one authenticated key"),
                     QStringLiteral("normal or hard PRNG")}));

    add(Method::Darkside, !hasKey && !staticNonce && nonce != NonceType::StaticEncrypted,
        !hasKey && nonce == NonceType::Unknown,
        hasKey ? QStringLiteral("a key is already available; nested/hardnested is cheaper")
               : staticNonce ? QStringLiteral("static-nonce card: darkside will not work")
                             : nonce == NonceType::StaticEncrypted
                               ? QStringLiteral("static encrypted nonce: darkside will not work")
                               : QStringLiteral("recovers the first key on a normal nonce card"),
        QStringList({QStringLiteral("normal PRNG"), QStringLiteral("no known key required")}));

    add(Method::Sen, nonce == NonceType::StaticEncrypted, nonce == NonceType::Unknown,
        nonce == NonceType::StaticEncrypted
        ? QStringLiteral("card presents a static encrypted nonce (FM11RF08S family)")
        : nonce == NonceType::Unknown
          ? QStringLiteral("nonce type is not known yet; run info/isen first")
          : QStringLiteral("only applies to static encrypted nonce cards"),
        QStringList({QStringLiteral("FM11RF08S-compatible card"),
                     QStringLiteral("static encrypted nonce")}));

    add(Method::Isen, true, false,
        QStringLiteral("read-only diagnostics for static encrypted nonce properties"),
        QStringList({QStringLiteral("ISO14443A card")}));

    return list;
}

QString detectStage(const QString& cleanOutput)
{
    struct StagePattern
    {
        const char* pattern;
        const char* stage;
    };
    static const StagePattern patterns[] =
    {
        {"dictionary|checking|chk", "dictionary"},
        {"darkside", "nonce"},
        {"staticnested|static nested", "nonce"},
        {"hardnested", "nonce"},
        {"nested", "nonce"},
        {"nonce", "nonce"},
        {"calculat|brute|filtering|testing|\\bCPU\\b", "compute"},
        {"verif|read back|writing block", "verify"},
        {"reading|rdbl|rdsc|dump", "read"},
        {"Found valid key|key not found|time in ", "done"},
    };

    int bestPos = -1;
    QString best;
    for(const StagePattern& item : patterns)
    {
        QRegularExpression re(QString::fromLatin1(item.pattern), QRegularExpression::CaseInsensitiveOption);
        QRegularExpressionMatchIterator it = re.globalMatch(cleanOutput);
        while(it.hasNext())
        {
            const int pos = it.next().capturedStart();
            if(pos >= bestPos)
            {
                bestPos = pos;
                best = QString::fromLatin1(item.stage);
            }
        }
    }
    return best;
}

QString cardArg(const Mfc::Spec& spec)
{
    return QStringLiteral("--") + spec.name();
}

QString sectorArg(int sector)
{
    return QString::number(sector);
}

static QString replaceCard(const QString& command, const QVariantMap& config, const Mfc::Spec& spec)
{
    QString cmd = command;
    const QVariantMap map = config.value("card type").toMap();
    if(map.contains(spec.name()))
        cmd.replace(QStringLiteral("<card type>"), map.value(spec.name()).toString());
    else
        cmd.replace(QStringLiteral("<card type>"), spec.name());
    return cmd;
}

Command buildCheck(const QVariantMap& profile, const Mfc::Spec& spec,
                   const QString& dictionary, bool includeDefaults)
{
    const QVariantMap config = profile.value("check").toMap();
    Command command;
    command.text = replaceCard(config.value("cmd").toString(), config, spec);
    if(command.text.isEmpty())
        command.text = QStringLiteral("hf mf chk ") + cardArg(spec);
    if(!dictionary.trimmed().isEmpty())
        command.text += QStringLiteral(" -f \"%1\"").arg(dictionary);
    if(!includeDefaults)
        command.text += QStringLiteral(" --no-default");
    command.usesStdin = true; // hf mf chk polls kbd_enter_pressed() per key chunk
    command.successPattern.clear(); // a one-shot process exit is the boundary; the table is parsed afterwards
    command.failurePattern = QStringLiteral("No tag found|Can't select card|command execution time out|not available in this mode");
    command.hardTimeoutMs = 15 * 60 * 1000;
    return command;
}

Command buildAutoPwn(const QVariantMap& profile, const Mfc::Spec& spec,
                     const QString& dictionary, bool noSave)
{
    const QVariantMap config = profile.value("autopwn").toMap();
    Command command;
    command.text = replaceCard(config.value("cmd").toString(), config, spec);
    if(command.text.isEmpty())
        command.text = QStringLiteral("hf mf autopwn ") + cardArg(spec);
    if(noSave && !command.text.contains(QStringLiteral("--ns")))
        command.text += QStringLiteral(" --ns");
    if(!dictionary.trimmed().isEmpty())
        command.text += QStringLiteral(" -f \"%1\"").arg(dictionary);
    command.usesStdin = true; // hf mf autopwn polls kbd_enter_pressed() per chunk
    command.successPattern.clear(); // a one-shot process exit is the boundary; the table is parsed afterwards
    command.failurePattern = QStringLiteral("No tag found|Can't select card|command execution time out|not available in this mode");
    command.hardTimeoutMs = 60 * 60 * 1000;
    return command;
}

Command buildNested(const QVariantMap& profile, const Mfc::Spec& spec, bool isStatic,
                    int knownSector, char knownType, const QString& knownKey)
{
    const QVariantMap config = profile.value("nested").toMap();
    Command command;
    command.text = config.value(isStatic ? "static cmd" : "cmd").toString();
    if(command.text.isEmpty())
        command.text = isStatic
                       ? QStringLiteral("hf mf staticnested --<card type> --blk <block> -<key type> -k <key>")
                       : QStringLiteral("hf mf nested --<card type> --blk <block> -<key type> -k <key>");
    command.text = replaceCard(command.text, config, spec);
    const QVariantMap keyTypes = config.value("key type").toMap();
    const QString typeKey = (knownType == 'B') ? QStringLiteral("B") : QStringLiteral("A");
    command.text.replace(QStringLiteral("<block>"), QString::number(spec.trailerBlockOfSector(knownSector)));
    command.text.replace(QStringLiteral("<key type>"), keyTypes.value(typeKey).toString().isEmpty()
                         ? typeKey.toLower() : keyTypes.value(typeKey).toString());
    command.text.replace(QStringLiteral("<key>"), knownKey);
    command.usesStdin = true; // hf mf nested polls kbd_enter_pressed()
    command.successPattern.clear(); // a one-shot process exit is the boundary; the table is parsed afterwards
    command.failurePattern = QStringLiteral("Can't found|Can't authenticate|Static nonce detected|"
                                            "No tag found|command execution time out|not available in this mode");
    command.hardTimeoutMs = 30 * 60 * 1000;
    return command;
}

Command buildHardnested(const QVariantMap& profile, const Mfc::Spec& spec,
                        int knownSector, char knownType, const QString& knownKey,
                        int targetSector, char targetType)
{
    const QVariantMap config = profile.value("hardnested").toMap();
    Command command;
    command.text = config.value("cmd").toString();
    if(command.text.isEmpty())
        command.text = QStringLiteral("hf mf hardnested --blk <known key block> -<known key type> -k <known key> "
                                      "--tblk <target key block> --t<target key type>");
    const QVariantMap knownTypes = config.value("known key type").toMap();
    const QVariantMap targetTypes = config.value("target key type").toMap();
    const QString knownKeyName = (knownType == 'B') ? QStringLiteral("B") : QStringLiteral("A");
    const QString targetKeyName = (targetType == 'B') ? QStringLiteral("B") : QStringLiteral("A");
    command.text.replace(QStringLiteral("<known key block>"), QString::number(spec.trailerBlockOfSector(knownSector)));
    command.text.replace(QStringLiteral("<known key type>"), knownTypes.value(knownKeyName).toString().isEmpty()
                         ? knownKeyName.toLower() : knownTypes.value(knownKeyName).toString());
    command.text.replace(QStringLiteral("<known key>"), knownKey);
    command.text.replace(QStringLiteral("<target key block>"), QString::number(spec.trailerBlockOfSector(targetSector)));
    command.text.replace(QStringLiteral("<target key type>"), targetTypes.value(targetKeyName).toString().isEmpty()
                         ? targetKeyName.toLower() : targetTypes.value(targetKeyName).toString());
    command.usesStdin = true; // hardnested does not poll today; every method uses one protocol
    command.successPattern.clear(); // a one-shot process exit is the boundary; the table is parsed afterwards
    command.failurePattern = QStringLiteral("Can't authenticate|Static nonce detected|No tag found|"
                                            "command execution time out|not available in this mode");
    command.hardTimeoutMs = 60 * 60 * 1000;
    return command;
}

static Command simpleCommand(const QVariantMap& profile, const QString& section,
                             const QString& fallback, qint64 hardTimeout)
{
    Command command;
    command.text = profile.value(section).toMap().value("cmd").toString();
    if(command.text.isEmpty())
        command.text = fallback;
    command.usesStdin = true; // darkside (mfc_read_tag) and sen poll kbd_enter_pressed()
    command.successPattern.clear(); // a one-shot process exit is the boundary; the table is parsed afterwards
    command.failurePattern = QStringLiteral("Static nonce detected|Static encrypted nonce detected|"
                                            "No tag found|command execution time out|not available in this mode");
    command.hardTimeoutMs = hardTimeout;
    return command;
}

Command buildDarkside(const QVariantMap& profile)
{
    return simpleCommand(profile, QStringLiteral("darkside"), QStringLiteral("hf mf darkside"),
                         30 * 60 * 1000);
}

Command buildSen(const QVariantMap& profile)
{
    Command command = simpleCommand(profile, QStringLiteral("sen"), QStringLiteral("hf mf sen"),
                                    60 * 60 * 1000);
    command.usesStdin = true; // one protocol for every recovery method
    command.successPattern.clear(); // a one-shot process exit is the boundary; the table is parsed afterwards
    return command;
}

Command buildIsen(const QVariantMap& profile)
{
    Command command = simpleCommand(profile, QStringLiteral("isen"), QStringLiteral("hf mf isen"),
                                    10 * 60 * 1000);
    command.usesStdin = true; // one protocol for every recovery method
    command.successPattern.clear(); // a one-shot process exit is the boundary; the table is parsed afterwards
    return command;
}

void Tracker::start(Method method, int totalKeys)
{
    m_method = method;
    m_state = TaskState::Running;
    m_stage.clear();
    m_message.clear();
    m_found = 0;
    m_total = totalKeys;
    m_timer.start();
}

void Tracker::setStage(const QString& stage)
{
    if(!stage.isEmpty())
        m_stage = stage;
}

void Tracker::setFound(int found)
{
    m_found = qMax(0, found);
}

void Tracker::finish(TaskState state, const QString& message)
{
    m_state = state;
    m_message = message;
}

void Tracker::tick()
{
    // Kept as an explicit hook: the UI calls it on its refresh timer, and the
    // elapsed time is derived from the monotonic timer rather than a counter of
    // output events (a CPU phase may be silent for minutes).
}

qint64 Tracker::elapsedMs() const
{
    return m_timer.isValid() ? m_timer.elapsed() : 0;
}

} // namespace MfcRecovery
