#pragma once

#include "config.h"
#include "theme.h"

#include <QPointer>
#include <QWidget>

namespace KParts
{
class ReadOnlyPart;
}
class TerminalInterface;
class QKeyEvent;
class Pane;

// Slim bar above each terminal: account badge, folder, chat title, state icon.
class PaneHeader : public QWidget
{
    Q_OBJECT
public:
    explicit PaneHeader(Pane *pane);
    QSize sizeHint() const override;

Q_SIGNALS:
    void clicked();
    void middleClicked();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    Pane *m_pane;
};

// One Claude Code session: a real Konsole terminal running the account's command
// inside the user's interactive shell, so shell functions and wrappers keep working.
class Pane : public QWidget
{
    Q_OBJECT
public:
    Pane(const Config *config, const Account &account, const QString &folder, QWidget *parent = nullptr);
    ~Pane() override;

    QString id() const { return m_id; }
    const Account &account() const { return m_account; }
    QString folder() const { return m_folder; }
    QString title() const { return m_title; }
    PaneState state() const { return m_state; }
    qint64 stateSince() const { return m_stateSince; }

    bool isCurrent() const { return m_current; }
    void setCurrent(bool current);
    void focusTerminal();

    // True while a claude process runs somewhere below the pane's shell.
    bool claudeRunning() const;

Q_SIGNALS:
    void changed(Pane *pane);
    void terminalClosed(Pane *pane);
    void closeRequested(Pane *pane);
    void shortcutCheck(QKeyEvent *event, bool &override);

private Q_SLOTS:
    void onOverrideShortcut(QKeyEvent *event, bool &override);

private:
    void start(const QString &shell);
    void releaseReservedShortcuts();
    void adoptSession();
    void setCaption(const QString &caption);
    void setState(PaneState state);
    void watchClaude();

    const Config *m_config;
    QString m_id;
    Account m_account;
    QString m_folder;
    QString m_title;
    PaneState m_state = PaneState::Starting;
    qint64 m_stateSince = 0;
    bool m_current = false;

    PaneHeader *m_header = nullptr;
    QPointer<KParts::ReadOnlyPart> m_part;
    TerminalInterface *m_terminal = nullptr;
    QString m_sessionPath; // D-Bus path of the Konsole session inside this process
    qint64 m_claudePid = 0; // the claude process last seen in this pane
};
