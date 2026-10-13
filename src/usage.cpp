#include "usage.h"
#include "config.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>

namespace
{
QString runtimeFolder()
{
    return QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + QStringLiteral("/konvoy");
}

UsageWindow readWindow(const QJsonObject &o)
{
    UsageWindow w;
    if (o.value(QLatin1String("percent")).isDouble()) {
        w.percent = o.value(QLatin1String("percent")).toDouble();
        w.resetsAt = qint64(o.value(QLatin1String("resets_at")).toDouble());
    }
    return w;
}

QString percentText(const UsageWindow &w, qint64 now)
{
    if (!w.known()) {
        return QStringLiteral("–");
    }
    return QStringLiteral("%1%").arg(w.passed(now) ? 0 : qRound(w.percent));
}
}

UsageModel::UsageModel(QObject *parent)
    : QObject(parent)
{
    QDir().mkpath(runtimeFolder());
    // The helper replaces the file, so the folder is watched, not the file.
    m_watcher = new QFileSystemWatcher({runtimeFolder()}, this);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, &UsageModel::load);
    // Resets pass and numbers age while the file stays the same.
    m_tick = new QTimer(this);
    m_tick->setInterval(30000);
    connect(m_tick, &QTimer::timeout, this, [this] {
        load();
        Q_EMIT changed();
    });
    m_tick->start();
    load();
}

UsageModel::~UsageModel()
{
    if (m_helper && m_helper->state() != QProcess::NotRunning) {
        disconnect(m_helper, nullptr, this, nullptr);
        m_helper->terminate();
        m_helper->waitForFinished(2000);
    }
}

QString UsageModel::filePath()
{
    return runtimeFolder() + QStringLiteral("/usage.json");
}

QString UsageModel::reportFolder()
{
    return runtimeFolder() + QStringLiteral("/statusline");
}

void UsageModel::startHelper(const Config *config)
{
    // Reports left by a Konvoy that did not close cleanly would pass for open panes.
    QDir reports(reportFolder());
    reports.mkpath(QStringLiteral("."));
    for (const QString &name : reports.entryList({QStringLiteral("*.json")}, QDir::Files)) {
        reports.remove(name);
    }
    QStringList args{QStringLiteral("--output"), filePath()};
    for (const Account &account : config->accounts()) {
        args << QStringLiteral("--account") << account.key + QLatin1Char('=') + account.configDir;
    }
    if (!config->usageExtraCommand().isEmpty()) {
        args << QStringLiteral("--extra-command") << config->usageExtraCommand() << QStringLiteral("--extra-interval")
             << QString::number(config->usageExtraInterval());
    }
    m_helper = new QProcess(this);
    m_helper->setProgram(QStringLiteral(KONVOY_LIBEXECDIR "/konvoy-usage"));
    m_helper->setArguments(args);
    m_helper->setStandardOutputFile(QProcess::nullDevice());
    m_helper->setProcessChannelMode(QProcess::ForwardedErrorChannel);
    // A helper that dies comes back a minute later.
    connect(m_helper, &QProcess::finished, this, [this] {
        QTimer::singleShot(60000, m_helper, [this] {
            m_helper->start();
        });
    });
    connect(m_helper, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            qWarning("konvoy: cannot start %s", qPrintable(m_helper->program()));
        }
    });
    m_helper->start();
}

void UsageModel::load()
{
    QFileInfo info(filePath());
    const qint64 mtime = info.exists() ? info.lastModified().toMSecsSinceEpoch() : 0;
    if (mtime == m_mtime) {
        return;
    }
    QFile f(filePath());
    if (!f.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonObject doc = QJsonDocument::fromJson(f.readAll()).object();
    if (doc.isEmpty()) {
        return; // keep what was shown; the next write brings a whole file
    }
    m_mtime = mtime;
    m_written = qint64(doc.value(QLatin1String("written")).toDouble());
    m_accounts.clear();
    const QJsonObject accounts = doc.value(QLatin1String("accounts")).toObject();
    for (auto it = accounts.begin(); it != accounts.end(); ++it) {
        const QJsonObject a = it.value().toObject();
        AccountUsage u;
        u.live = a.value(QLatin1String("live")).toBool();
        u.source = a.value(QLatin1String("source")).toString();
        u.asOf = qint64(a.value(QLatin1String("as_of")).toDouble());
        const QJsonObject limits = a.value(QLatin1String("limits")).toObject();
        u.fiveHour = readWindow(limits.value(QLatin1String("five_hour")).toObject());
        u.sevenDay = readWindow(limits.value(QLatin1String("seven_day")).toObject());
        const QJsonObject resets = a.value(QLatin1String("extra")).toObject().value(QLatin1String("resets")).toObject();
        u.resetsAvailable = resets.value(QLatin1String("available")).toInt();
        u.resetsExpireAt = qint64(resets.value(QLatin1String("expires_at")).toDouble());
        m_accounts.insert(it.key(), u);
    }
    Q_EMIT changed();
}

bool UsageModel::outdated(qint64 now) const
{
    // The helper writes at least every 30 seconds.
    return m_written == 0 || now - m_written > 90;
}

const AccountUsage *UsageModel::usage(const QString &key) const
{
    const auto it = m_accounts.constFind(key);
    return it == m_accounts.constEnd() ? nullptr : &it.value();
}

bool UsageModel::stale(const AccountUsage &u) const
{
    return !u.live || u.asOf == 0;
}

QString UsageModel::shortText(const QString &key) const
{
    const AccountUsage *u = usage(key);
    if (!u) {
        return QString();
    }
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    return QStringLiteral("5h %1   7d %2").arg(percentText(u->fiveHour, now), percentText(u->sevenDay, now));
}
