#include "config.h"

#include <KConfigGroup>
#include <QDir>
#include <QFileInfo>

namespace
{
// Account colours stay clear of the state colours (blue, orange, yellow, green, grey, red).
const QList<QColor> &palette()
{
    static const QList<QColor> colors{QColor(157, 123, 234), QColor(38, 181, 166), QColor(224, 108, 159), QColor(201, 162, 107),
                                      QColor(121, 134, 203), QColor(156, 204, 101), QColor(240, 98, 146),  QColor(77, 182, 172)};
    return colors;
}

constexpr int maxRecentFolders = 12;
}

Config::Config(const QString &file)
{
    m_config = file.isEmpty() ? KSharedConfig::openConfig(QStringLiteral("konvoyrc"))
                              : KSharedConfig::openConfig(QFileInfo(file).absoluteFilePath(), KConfig::SimpleConfig);
    loadAccounts();
}

void Config::loadAccounts()
{
    const QStringList keys = m_config->group(QStringLiteral("General")).readEntry("Accounts", QStringList());
    int paletteIndex = 0;
    for (const QString &key : keys) {
        const KConfigGroup g = m_config->group(QStringLiteral("Account ") + key);
        Account a;
        a.key = key;
        a.name = g.readEntry("Name", key);
        a.command = g.readEntry("Command", QStringLiteral("claude"));
        a.configDir = QDir::cleanPath(expandHome(g.readEntry("ConfigDir", QStringLiteral("~/.claude"))));
        a.color = QColor::fromString(g.readEntry("Color", QString()));
        if (!a.color.isValid()) {
            a.color = palette().at(paletteIndex++ % palette().size());
        }
        m_accounts.append(a);
    }
    if (m_accounts.isEmpty()) {
        m_accounts.append(Account{QStringLiteral("A"), QStringLiteral("claude"), QStringLiteral("claude"),
                                  QDir::cleanPath(expandHome(QStringLiteral("~/.claude"))), palette().first()});
    }
}

const Account *Config::account(const QString &key) const
{
    for (const Account &a : m_accounts) {
        if (a.key == key) {
            return &a;
        }
    }
    return nullptr;
}

const Account *Config::accountForConfigDir(const QString &dir) const
{
    // An unset CLAUDE_CONFIG_DIR means the default ~/.claude.
    const QString wanted = QDir::cleanPath(expandHome(dir.isEmpty() ? QStringLiteral("~/.claude") : dir));
    for (const Account &a : m_accounts) {
        if (a.configDir == wanted) {
            return &a;
        }
    }
    return nullptr;
}

QString Config::shell() const
{
    QString s = m_config->group(QStringLiteral("General")).readEntry("Shell", QString());
    if (s.isEmpty()) {
        s = qEnvironmentVariable("SHELL", QStringLiteral("/bin/sh"));
    }
    return s;
}

QStringList Config::readFolders(const char *key) const
{
    QStringList out;
    for (const QString &f : m_config->group(QStringLiteral("Folders")).readEntry(key, QStringList())) {
        const QString path = QDir::cleanPath(expandHome(f));
        if (!path.isEmpty() && !out.contains(path)) {
            out.append(path);
        }
    }
    return out;
}

void Config::writeFolders(const char *key, const QStringList &folders)
{
    QStringList stored;
    for (const QString &f : folders) {
        stored.append(collapseHome(f));
    }
    KConfigGroup g = m_config->group(QStringLiteral("Folders"));
    g.writeEntry(key, stored);
    g.sync();
}

QStringList Config::pinnedFolders() const
{
    return readFolders("Pinned");
}

void Config::setPinnedFolders(const QStringList &folders)
{
    writeFolders("Pinned", folders);
}

QStringList Config::recentFolders() const
{
    return readFolders("Recent");
}

void Config::addRecentFolder(const QString &folder)
{
    QStringList recent = recentFolders();
    recent.removeAll(folder);
    recent.prepend(folder);
    while (recent.size() > maxRecentFolders) {
        recent.removeLast();
    }
    writeFolders("Recent", recent);
    KConfigGroup g = m_config->group(QStringLiteral("Folders"));
    g.writeEntry("Last", collapseHome(folder));
    g.sync();
}

QString Config::lastFolder() const
{
    const QString last = m_config->group(QStringLiteral("Folders")).readEntry("Last", QString());
    return last.isEmpty() ? QString() : QDir::cleanPath(expandHome(last));
}

int Config::columns() const
{
    return m_config->group(QStringLiteral("Layout")).readEntry("Columns", 0);
}

void Config::setColumns(int columns)
{
    KConfigGroup g = m_config->group(QStringLiteral("Layout"));
    g.writeEntry("Columns", columns);
    g.sync();
}

QString Config::expandHome(const QString &path)
{
    if (path == QLatin1String("~")) {
        return QDir::homePath();
    }
    if (path.startsWith(QLatin1String("~/"))) {
        return QDir::homePath() + path.mid(1);
    }
    return path;
}

QString Config::collapseHome(const QString &path)
{
    const QString home = QDir::homePath();
    if (path == home) {
        return QStringLiteral("~");
    }
    if (path.startsWith(home + QLatin1Char('/'))) {
        return QLatin1Char('~') + path.mid(home.size());
    }
    return path;
}
