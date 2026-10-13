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
    void restartClicked();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    Pane *m_pane;
    QRectF m_restartRect;
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
    QString sessionId() const { return m_sessionId; }
    // Kinds of the tasks still running after the turn ("shell", "subagent", ...).
    QStringList backgroundTasks() const { return m_backgroundTasks; }

    // Working, waiting for an answer or running background tasks: closing such a
    // pane asks first.
    bool isBusy() const;
    // The user has looked at the pane: done becomes idle.
    void markSeen();
    // Starts Claude again in the pane's shell, continuing the last chat.
    void restartClaude();
    bool canRestart() const;

    bool isCurrent() const { return m_current; }
    void setCurrent(bool current);
    void focusTerminal();
    void refreshHeader();

    // True while a claude process runs somewhere below the pane's shell.
    bool claudeRunning() const;

Q_SIGNALS:
    void changed(Pane *pane);
    void stateChanged(Pane *pane, PaneState previous);
    void terminalClosed(Pane *pane);
    void closeRequested(Pane *pane);
    void shortcutCheck(QKeyEvent *event, bool &override);

private Q_SLOTS:
    void onOverrideShortcut(QKeyEvent *event, bool &override);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void start(const QString &shell);
    void releaseReservedShortcuts();
    void adoptSession();
    void setCaption(const QString &caption);
    void setState(PaneState state);
    void watchClaude();
    void openEventFile();
    void readEvents();
    void applyEvent(const QStringList &fields);
    void recompute();
    void checkAnswered();
    void checkBackground();

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
    QString m_sessionId;

    // State tracking. The hooks say what Claude is waiting for, the animated
    // symbol in Claude's terminal title says whether it is working.
    QString m_eventFile;
    qint64 m_eventOffset = 0;
    QByteArray m_eventBuffer;
    PaneState m_hookState = PaneState::Starting;
    bool m_titleSeen = false;    // Claude titles with a status symbol arrived
    bool m_animating = false;    // the symbol spins: Claude is working
    qint64 m_workingSince = 0;   // when it last started spinning
    qint64 m_promptAt = 0;       // last UserPromptSubmit
    bool m_seen = true;          // false from a new turn until the user looks
    bool m_exited = false;
    qint64 m_lastEventAt = 0;
    qint64 m_dialogAt = 0;       // when the last permission or question hook ran
    qint64 m_answerKeyAt = 0;
    QStringList m_backgroundTasks;
    qint64 m_stopAt = 0;         // last Stop
    int m_quietChecks = 0;       // checks in a row without a background shell
};
