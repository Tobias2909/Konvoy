#include "chats.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace
{
// Claude Code writes the titles and the last prompt again after every turn, so the
// end of a chat file holds the newest ones.
constexpr qint64 tailBytes = 256 * 1024;

QString oneLine(QString text)
{
    text = text.simplified();
    return text.size() > 90 ? text.left(89) + QChar(0x2026) : text;
}

QString readTitle(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return QString();
    }
    const qint64 start = qMax<qint64>(0, f.size() - tailBytes);
    f.seek(start);
    const QList<QByteArray> lines = f.readAll().split('\n');
    QString aiTitle;
    QString lastPrompt;
    for (auto it = lines.crbegin(); it != lines.crend(); ++it) {
        const QByteArray &line = *it;
        const bool custom = line.contains("\"type\":\"custom-title\"");
        const bool ai = aiTitle.isEmpty() && line.contains("\"type\":\"ai-title\"");
        const bool prompt = lastPrompt.isEmpty() && line.contains("\"type\":\"last-prompt\"");
        if (!custom && !ai && !prompt) {
            continue;
        }
        const QJsonObject o = QJsonDocument::fromJson(line).object();
        if (custom) {
            const QString title = o.value(QLatin1String("customTitle")).toString();
            if (!title.isEmpty()) {
                return oneLine(title);
            }
        } else if (ai) {
            aiTitle = o.value(QLatin1String("aiTitle")).toString();
        } else {
            lastPrompt = o.value(QLatin1String("lastPrompt")).toString();
        }
    }
    return oneLine(aiTitle.isEmpty() ? lastPrompt : aiTitle);
}
}

namespace Chats
{
QString projectFolderName(const QString &folder)
{
    static const QRegularExpression other(QStringLiteral("[^a-zA-Z0-9]"));
    return QString(folder).replace(other, QStringLiteral("-"));
}

QList<Chat> recent(const QStringList &configDirs, const QString &folder, int max)
{
    QStringList roots;
    for (const QString &dir : configDirs) {
        const QString root = QFileInfo(dir + QStringLiteral("/projects")).canonicalFilePath();
        if (!root.isEmpty() && !roots.contains(root)) {
            roots.append(root);
        }
    }
    QList<Chat> chats;
    QSet<QString> seen;
    static const QRegularExpression uuid(QStringLiteral("^[0-9a-fA-F-]{8,64}$"));
    for (const QString &root : std::as_const(roots)) {
        const QDir dir(root + QLatin1Char('/') + projectFolderName(folder));
        const QFileInfoList files = dir.entryInfoList({QStringLiteral("*.jsonl")}, QDir::Files, QDir::Time);
        int looked = 0;
        for (const QFileInfo &file : files) {
            // Empty chats are skipped, so a few more than wanted are read.
            if (++looked > max * 3) {
                break;
            }
            const QString id = file.completeBaseName();
            if (!uuid.match(id).hasMatch() || seen.contains(id)) {
                continue;
            }
            const QString title = readTitle(file.filePath());
            if (title.isEmpty()) {
                continue;
            }
            seen.insert(id);
            chats.append(Chat{id, title, file.lastModified()});
        }
    }
    std::sort(chats.begin(), chats.end(), [](const Chat &a, const Chat &b) {
        return a.modified > b.modified;
    });
    if (chats.size() > max) {
        chats.resize(max);
    }
    return chats;
}

QString age(const QDateTime &time)
{
    const QDateTime now = QDateTime::currentDateTime();
    const qint64 minutes = time.secsTo(now) / 60;
    if (minutes < 1) {
        return QStringLiteral("just now");
    }
    if (minutes < 60) {
        return QStringLiteral("%1 min ago").arg(minutes);
    }
    if (time.date() == now.date()) {
        return QStringLiteral("%1 h ago").arg(minutes / 60);
    }
    if (time.date() == now.date().addDays(-1)) {
        return QStringLiteral("yesterday");
    }
    return QLocale::c().toString(time, QStringLiteral("d MMM"));
}
}
