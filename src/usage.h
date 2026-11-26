#pragma once

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QString>

class Config;
class QFileSystemWatcher;
class QProcess;
class QTimer;

struct UsageWindow {
    double percent = -1; // below 0: unknown
    qint64 resetsAt = 0; // epoch seconds, 0: unknown

    bool known() const { return percent >= 0; }
    // The window ended after these numbers were taken: it starts again from 0.
    bool passed(qint64 now) const { return resetsAt > 0 && resetsAt <= now; }
    // Used up and not reset since: the account is at its limit until resetsAt. Rounded
    // like percentText, so a window counts as full exactly when it reads 100%.
    bool full(qint64 now) const { return known() && !passed(now) && qRound(percent) >= 100; }
    // "32%", "0%" once the window has passed, "–" when unknown.
    QString percentText(qint64 now) const;
};

struct AccountUsage {
    bool live = false; // a pane of this account is open and reports every change
    QString source;    // statusline, cache
    qint64 asOf = 0;   // when the numbers were current, epoch seconds
    UsageWindow fiveHour;
    UsageWindow sevenDay;
    int resetsAvailable = 0;
    qint64 resetsExpireAt = 0;
};

// Reads the usage file the helper writes. Neither sends any request: the numbers are
// the ones Claude Code reports to its status line or saved itself. The helper is a
// separate program, started here while Konvoy runs.
class UsageModel : public QObject
{
    Q_OBJECT
public:
    explicit UsageModel(QObject *parent = nullptr);
    ~UsageModel() override;

    static QString filePath();
    static QString reportFolder(); // where status lines in panes report their numbers

    // Starts konvoy-usage for the configured accounts.
    void startHelper(const Config *config);

    bool hasData() const { return m_written > 0; }
    qint64 written() const { return m_written; }
    // The file has not been rewritten for a while: the helper does not run.
    bool outdated(qint64 now) const;
    const AccountUsage *usage(const QString &key) const;
    // No pane of the account is open, so its numbers may be out of date.
    bool stale(const AccountUsage &usage) const;

    // "5h 32%   7d 68%" for the tooltip on the sidebar's usage rows.
    QString shortText(const QString &key) const;

Q_SIGNALS:
    void changed();

private:
    void load();

    QHash<QString, AccountUsage> m_accounts;
    qint64 m_written = 0;
    qint64 m_mtime = 0;
    QFileSystemWatcher *m_watcher = nullptr;
    QProcess *m_helper = nullptr;
    QTimer *m_tick = nullptr;
};
