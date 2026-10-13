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

    static QString expandHome(const QString &path);
    static QString collapseHome(const QString &path);

private:
    void loadAccounts();
    QStringList readFolders(const char *key) const;
    void writeFolders(const char *key, const QStringList &folders);

    KSharedConfigPtr m_config;
    QList<Account> m_accounts;
};
