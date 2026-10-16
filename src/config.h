#pragma once

#include <KSharedConfig>
#include <QColor>
#include <QList>
#include <QString>
#include <QStringList>

struct Account {
    QString key;       // also the letter on the badge
    QString name;
    QString command;   // run by the pane's shell
    QString configDir; // absolute, the account's CLAUDE_CONFIG_DIR
    QColor color;
};

// Konvoy's settings, ~/.config/konvoyrc unless another file is given.
class Config
{
public:
    explicit Config(const QString &file = QString());

    const QList<Account> &accounts() const { return m_accounts; }
    const Account *account(const QString &key) const;
    const Account *accountForConfigDir(const QString &dir) const;

    QString shell() const;

    QStringList pinnedFolders() const;
    void setPinnedFolders(const QStringList &folders);
    QStringList recentFolders() const;
    void addRecentFolder(const QString &folder);
    QString lastFolder() const;

    int columns() const; // 0 = automatic
    void setColumns(int columns);

    // [Usage]
    QString usageExtraCommand() const;  // optional, adds fields such as resets
    int usageExtraInterval() const;
    // The usage helper's copy on disk: konvoy-usage.json in the XDG state folder,
    // or "<file>.usage.json" next to a config given with --config.
    QString usageKeepFile() const { return m_usageKeep; }

    // Window layout and panes of the last run: konvoystaterc in the XDG state folder,
    // or "<file>.state" next to a config given with --config.
    KSharedConfigPtr state() const { return m_state; }

    static QString expandHome(const QString &path);
    static QString collapseHome(const QString &path);

private:
    void loadAccounts();
    QStringList readFolders(const char *key) const;
    void writeFolders(const char *key, const QStringList &folders);

    KSharedConfigPtr m_config;
    KSharedConfigPtr m_state;
    QString m_usageKeep;
    QList<Account> m_accounts;
};
