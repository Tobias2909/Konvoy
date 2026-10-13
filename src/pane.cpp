#include "pane.h"
#include "shortcuts.h"
#include "usage.h"

#include <KActionCollection>
#include <KPluginFactory>
#include <KPluginMetaData>
#include <KParts/ReadOnlyPart>
#include <kde_terminal_interface.h>

#include <QAction>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QRegularExpression>
#include <QFileSystemWatcher>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>

#include <algorithm>

namespace
{
constexpr int headerHeight = 28;

// Claude Code puts a status glyph in front of its terminal title ("◐ Fix the banner").
QString stripStatusGlyph(const QString &caption)
{
    static const QRegularExpression glyph(QStringLiteral("^[^\\p{L}\\p{N}~/]\\s+"));
    return caption.trimmed().remove(glyph);
}

// Written at every start, so it always matches this Konvoy.
QString paneHelperPath()
{
    static QString path;
    if (!path.isEmpty()) {
        return path;
    }
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + QStringLiteral("/konvoy");
    QDir().mkpath(dir);
    path = dir + QStringLiteral("/run-pane");
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(R"(#!/bin/sh
# Started by Konvoy in every pane. The settings come in as environment variables.
cd -- "$KONVOY_DIR" 2>/dev/null || printf 'konvoy: cannot enter %s\n' "$KONVOY_DIR"
command=$KONVOY_COMMAND
unset KONVOY_COMMAND
exec "$KONVOY_SHELL" -i -c "$command"'; exec "$KONVOY_SHELL" -i'
)");
        f.close();
        f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    }
    return path;
}

QString readEnvironmentVariable(qint64 pid, const QByteArray &name)
{
    QFile f(QStringLiteral("/proc/%1/environ").arg(pid));
    if (!f.open(QIODevice::ReadOnly)) {
        return QString();
    }
    const QByteArray prefix = name + '=';
    for (const QByteArray &entry : f.readAll().split('\0')) {
        if (entry.startsWith(prefix)) {
            return QString::fromLocal8Bit(entry.mid(prefix.size()));
        }
    }
    return QString();
}

// Tasks Claude runs for itself, not for the user, do not keep a pane busy.
QStringList userTasks(const QString &field)
{
    static const QStringList housekeeping{QStringLiteral("dream"), QStringLiteral("auto-mode scan"), QStringLiteral("memory import")};
    QStringList kinds = field.split(QLatin1Char(','), Qt::SkipEmptyParts);
    kinds.removeIf([](const QString &kind) {
        return housekeeping.contains(kind);
    });
    return kinds;
}

// True while a shell runs directly below the process: Claude's Bash tool and
// command monitors run as "<shell> -c ...".
bool hasShellChild(qint64 pid)
{
    static const QList<QByteArray> shells{"bash", "zsh", "sh", "dash"};
    const QStringList threads = QDir(QStringLiteral("/proc/%1/task").arg(pid)).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &thread : threads) {
        QFile f(QStringLiteral("/proc/%1/task/%2/children").arg(pid).arg(thread));
        if (!f.open(QIODevice::ReadOnly)) {
            continue;
        }
        for (const QByteArray &child : f.readAll().split(' ')) {
            QFile comm(QStringLiteral("/proc/%1/comm").arg(QString::fromLatin1(child.trimmed())));
            if (!child.trimmed().isEmpty() && comm.open(QIODevice::ReadOnly) && shells.contains(comm.readAll().trimmed())) {
                return true;
            }
        }
    }
    return false;
}

// Parent pid and process name of every process, read from /proc/<pid>/stat.
QList<qint64> descendants(qint64 root, QHash<qint64, QString> *names)
{
    QMultiHash<qint64, qint64> children;
    const QStringList entries = QDir(QStringLiteral("/proc")).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        bool ok = false;
        const qint64 pid = entry.toLongLong(&ok);
        if (!ok) {
            continue;
        }
        QFile f(QStringLiteral("/proc/%1/stat").arg(pid));
        if (!f.open(QIODevice::ReadOnly)) {
            continue;
        }
        const QByteArray stat = f.readAll();
        const qsizetype open = stat.indexOf('(');
        const qsizetype close = stat.lastIndexOf(')');
        if (open < 0 || close < open) {
            continue;
        }
        names->insert(pid, QString::fromLocal8Bit(stat.mid(open + 1, close - open - 1)));
        const QList<QByteArray> rest = stat.mid(close + 2).split(' ');
        if (rest.size() > 1) {
            children.insert(rest.at(1).toLongLong(), pid);
        }
    }
    QList<qint64> out;
    QList<qint64> queue{root};
    while (!queue.isEmpty()) {
        const qint64 pid = queue.takeFirst();
        const QList<qint64> kids = children.values(pid);
        out.append(kids);
        queue.append(kids);
    }
    return out;
}
}

// ---------------------------------------------------------------- PaneHeader

PaneHeader::PaneHeader(Pane *pane)
    : QWidget(pane)
    , m_pane(pane)
{
    setFixedHeight(headerHeight);
    setCursor(Qt::PointingHandCursor);
}

QSize PaneHeader::sizeHint() const
{
    return QSize(200, headerHeight);
}

void PaneHeader::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::TextAntialiasing);
    const QPalette pal = palette();
    const QRectF r = rect();
    const PaneState state = m_pane->state();

    p.fillRect(r, pal.color(QPalette::Window).lighter(118));
    if (Theme::needsYou(state)) {
        p.fillRect(r, Theme::withAlpha(Theme::stateColor(state), 0.18));
    }
    p.fillRect(QRectF(0, r.height() - 2, r.width(), 2), m_pane->isCurrent() ? Theme::accent() : pal.color(QPalette::Mid));

    Theme::paintBadge(p, QRectF(8, 5, 18, 18), m_pane->account().key, m_pane->account().color);

    QFont bold = font();
    bold.setWeight(QFont::DemiBold);
    const QString folder = Config::collapseHome(m_pane->folder());
    const int folderWidth = QFontMetrics(bold).horizontalAdvance(folder);
    p.setFont(bold);
    p.setPen(pal.color(QPalette::WindowText));
    p.drawText(QRectF(32, 0, folderWidth + 2, r.height() - 2), Qt::AlignVCenter | Qt::AlignLeft, folder);

    QFont small = font();
    small.setPointSizeF(font().pointSizeF() - 1);
    p.setFont(small);

    // An exited pane offers to start Claude again.
    qreal right = r.width() - 36;
    m_restartRect = QRectF();
    if (state == PaneState::Exited && m_pane->canRestart()) {
        const QString label = QStringLiteral("Restart");
        const qreal w = QFontMetrics(small).horizontalAdvance(label) + 20;
        m_restartRect = QRectF(right - w, 4, w, r.height() - 10);
        p.save();
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(Theme::stateColor(state), 1));
        p.setBrush(Theme::withAlpha(Theme::stateColor(state), 0.15));
        p.drawRoundedRect(m_restartRect, 4, 4);
        p.setPen(pal.color(QPalette::WindowText));
        p.drawText(m_restartRect, Qt::AlignCenter, label);
        p.restore();
        right = m_restartRect.left() - 6;
    }

    p.setPen(pal.color(QPalette::PlaceholderText));
    const qreal titleX = 32 + folderWidth + 8;
    const qreal titleWidth = right - titleX;
    p.drawText(QRectF(titleX, 0, titleWidth, r.height() - 2), Qt::AlignVCenter | Qt::AlignLeft,
               QFontMetrics(small).elidedText(m_pane->title(), Qt::ElideRight, int(titleWidth)));

    Theme::paintStateIcon(p, QPointF(r.width() - 18, (r.height() - 2) / 2), state);
}

void PaneHeader::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton) {
        Q_EMIT middleClicked();
    } else if (m_restartRect.contains(event->position())) {
        Q_EMIT restartClicked();
    } else {
        Q_EMIT clicked();
    }
}

// ---------------------------------------------------------------- Pane

Pane::Pane(const Config *config, const Account &account, const QString &folder, QWidget *parent)
    : QWidget(parent)
    , m_config(config)
    , m_id(QUuid::createUuid().toString(QUuid::WithoutBraces))
    , m_account(account)
    , m_folder(folder)
    , m_stateSince(QDateTime::currentMSecsSinceEpoch())
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_header = new PaneHeader(this);
    layout->addWidget(m_header);
    connect(m_header, &PaneHeader::clicked, this, &Pane::focusTerminal);
    connect(m_header, &PaneHeader::middleClicked, this, [this] {
        Q_EMIT closeRequested(this);
    });
    connect(m_header, &PaneHeader::restartClicked, this, &Pane::restartClaude);

    const auto factory = KPluginFactory::loadFactory(KPluginMetaData(QStringLiteral("kf6/parts/konsolepart")));
    if (factory) {
        m_part = factory.plugin->create<KParts::ReadOnlyPart>(this, this);
    }
    m_terminal = m_part ? qobject_cast<TerminalInterface *>(m_part) : nullptr;
    if (!m_terminal) {
        auto *error = new QLabel(QStringLiteral("Konsole's terminal component could not be loaded.\n%1").arg(factory.errorString), this);
        error->setAlignment(Qt::AlignCenter);
        layout->addWidget(error, 1);
        setState(PaneState::Exited);
        return;
    }

    layout->addWidget(m_part->widget(), 1);
    setFocusProxy(m_part->widget());

    releaseReservedShortcuts();

    connect(m_part, &KParts::Part::setWindowCaption, this, &Pane::setCaption);
    connect(m_part, SIGNAL(overrideShortcut(QKeyEvent *, bool &)), this, SLOT(onOverrideShortcut(QKeyEvent *, bool &)));
    connect(m_part, &QObject::destroyed, this, [this] {
        m_terminal = nullptr;
        Q_EMIT terminalClosed(this);
    });

    start(config->shell());
    // Once more after the first event loop turn, for actions Konsole adds late.
    // Key presses in the terminal tell when the user answers a dialog.
    QTimer::singleShot(0, this, [this] {
        releaseReservedShortcuts();
        if (m_part && m_part->widget()) {
            for (QWidget *w : m_part->widget()->findChildren<QWidget *>()) {
                w->installEventFilter(this);
            }
        }
    });

    // /switch replaces the claude process with one for another account. Checking the
    // running claude's CLAUDE_CONFIG_DIR keeps the badge right, hooks or not.
    auto *watch = new QTimer(this);
    watch->setInterval(2000);
    connect(watch, &QTimer::timeout, this, &Pane::watchClaude);
    watch->start();
}

Pane::~Pane()
{
    if (!m_eventFile.isEmpty()) {
        QFile::remove(m_eventFile);
    }
    // The status line of this pane's Claude reports its plan usage here.
    QFile::remove(UsageModel::reportFolder() + QLatin1Char('/') + m_id + QStringLiteral(".json"));
    if (m_part) {
        disconnect(m_part, nullptr, this, nullptr);
        delete m_part;
    }
}

void Pane::releaseReservedShortcuts()
{
    // The part's own actions must not hold Konvoy's keys, or Qt sees two owners and
    // runs neither. Konsole's per-session actions ("close session" on Ctrl+Shift+W)
    // live in child GUI clients and on the terminal widgets, not in the part's own
    // collection, so all three places are searched.
    QList<QAction *> actions = m_part->actionCollection()->actions();
    QList<KXMLGUIClient *> clients = m_part->childClients();
    while (!clients.isEmpty()) {
        KXMLGUIClient *client = clients.takeFirst();
        actions += client->actionCollection()->actions();
        clients += client->childClients();
    }
    actions += m_part->widget()->actions();
    for (QWidget *w : m_part->widget()->findChildren<QWidget *>()) {
        actions += w->actions();
    }
    const QList<QKeySequence> reserved = Shortcuts::all();
    for (QAction *action : std::as_const(actions)) {
        QList<QKeySequence> keys = action->shortcuts();
        const qsizetype before = keys.size();
        keys.removeIf([&reserved](const QKeySequence &k) {
            return reserved.contains(k);
        });
        if (keys.size() != before) {
            if (qEnvironmentVariableIsSet("KONVOY_DEBUG")) {
                fprintf(stderr, "konvoy: took %s from part action %s\n", qPrintable(action->shortcut().toString()), qPrintable(action->objectName()));
            }
            action->setShortcuts(keys);
        }
    }
}

void Pane::start(const QString &shell)
{
    // Konsole expands $VARIABLES in a program's arguments with Konvoy's own
    // environment, so the pane's settings travel as environment variables to a
    // helper that takes no arguments. The helper starts the account command in an
    // interactive shell, so shell functions work, and leaves an interactive shell
    // in the pane when Claude ends.
    const QList<QPair<QByteArray, QString>> vars{
        {QByteArrayLiteral("KONVOY_PANE"), m_id},
        {QByteArrayLiteral("KONVOY_DIR"), m_folder},
        {QByteArrayLiteral("KONVOY_SHELL"), shell},
        {QByteArrayLiteral("KONVOY_COMMAND"), m_account.command},
    };
    for (const auto &[name, value] : vars) {
        qputenv(name.constData(), value.toLocal8Bit());
    }
    openEventFile();
    const QString helper = paneHelperPath();
    m_terminal->startProgram(helper, {helper});
    for (const auto &[name, value] : vars) {
        qunsetenv(name.constData());
    }
    adoptSession();
}

void Pane::adoptSession()
{
    // Each embedded Konsole session is a D-Bus object of this process. Its path is in
    // the shell's environment. Through it the tab title follows the title the program
    // sets (Claude's chat title) instead of Konsole's "folder : process" format.
    const int pid = m_terminal->terminalProcessId();
    m_sessionPath = readEnvironmentVariable(pid, "KONSOLE_DBUS_SESSION");
    if (m_sessionPath.isEmpty()) {
        qWarning("konvoy: no Konsole session found for pid %d", pid);
        return;
    }
    QDBusInterface session(QDBusConnection::sessionBus().baseService(), m_sessionPath, QStringLiteral("org.kde.konsole.Session"));
    session.call(QStringLiteral("setTabTitleFormat"), 0, QStringLiteral("%w"));
    session.call(QStringLiteral("setTabTitleFormat"), 1, QStringLiteral("%w"));
}

void Pane::setCaption(const QString &caption)
{
    // Only Claude's titles count; they start with its status symbol. Shell or
    // profile titles ("Profile 1", "zsh") are ignored. Claude alternates ◐ and ◑
    // while it works and shows ✳ otherwise, including after Esc.
    const QString trimmed = caption.trimmed();
    if (qEnvironmentVariableIsSet("KONVOY_DEBUG")) {
        fprintf(stderr, "konvoy: %lld caption [%s]\n", QDateTime::currentMSecsSinceEpoch() % 100000, qPrintable(trimmed));
    }
    const QString title = stripStatusGlyph(trimmed);
    if (title == trimmed || title.isEmpty()) {
        return;
    }
    const QChar symbol = trimmed.at(0);
    const bool animating = symbol == QChar(0x25D0) || symbol == QChar(0x25D1);
    m_titleSeen = true;
    if (animating && !m_animating) {
        // Konsole reports titles up to about 2 s late. A dialog hook from just before
        // this report belongs to a dialog that opened after the reported spin, so it
        // stays; the next still symbol brings it back.
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        m_workingSince = now;
        // In the same way a spin reported just after Stop is the finished turn's own.
        const bool lateDialog = Theme::needsYou(m_hookState) && now - m_dialogAt < 1500;
        const bool lateTurn = m_hookState == PaneState::Background && now - m_stopAt < 3000;
        if (!lateDialog && !lateTurn) {
            m_hookState = PaneState::Working;
        }
        m_seen = false;
    }
    m_animating = animating;
    if (title != m_title) {
        m_title = title;
        m_header->update();
        Q_EMIT changed(this);
    }
    recompute();
}

void Pane::openEventFile()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + QStringLiteral("/konvoy/events");
    QDir().mkpath(dir);
    m_eventFile = dir + QLatin1Char('/') + m_id;
    QFile f(m_eventFile);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning("konvoy: cannot create %s", qPrintable(m_eventFile));
        return;
    }
    f.close();
    auto *watcher = new QFileSystemWatcher({m_eventFile}, this);
    connect(watcher, &QFileSystemWatcher::fileChanged, this, &Pane::readEvents);
}

void Pane::readEvents()
{
    QFile f(m_eventFile);
    if (!f.open(QIODevice::ReadOnly) || !f.seek(m_eventOffset)) {
        return;
    }
    const QByteArray data = f.readAll();
    m_eventOffset += data.size();
    m_eventBuffer += data;
    qsizetype newline;
    while ((newline = m_eventBuffer.indexOf('\n')) >= 0) {
        const QString line = QString::fromUtf8(m_eventBuffer.left(newline));
        m_eventBuffer.remove(0, newline + 1);
        applyEvent(line.split(QLatin1Char('\t')));
    }
}

void Pane::applyEvent(const QStringList &fields)
{
    // time  event  session_id  tool_name  notification_type  config_dir
    if (fields.size() < 2) {
        return;
    }
    bool ok = false;
    const qint64 at = qint64(QString(fields.at(0)).replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&ok) * 1000);
    const QString event = fields.at(1);
    const QString session = fields.value(2);
    const QString tool = fields.value(3);
    const QString type = fields.value(4);
    if (!session.isEmpty()) {
        m_sessionId = session;
    }
    if (fields.size() > 5) {
        const Account *account = m_config->accountForConfigDir(fields.at(5));
        if (account && account->key != m_account.key) {
            m_account = *account;
            m_header->update();
            Q_EMIT changed(this);
        }
    }

    // A dialog event from before Claude last started working again was answered already.
    const bool fresh = !ok || at >= m_workingSince;
    const bool asks = tool == QLatin1String("AskUserQuestion") || tool == QLatin1String("ExitPlanMode");
    if (event == QLatin1String("SessionStart")) {
        m_exited = false;
        m_hookState = PaneState::Idle;
        m_backgroundTasks.clear();
        m_seen = true;
    } else if (event == QLatin1String("UserPromptSubmit")) {
        m_hookState = PaneState::Working;
        m_promptAt = QDateTime::currentMSecsSinceEpoch();
        m_seen = false;
    } else if (event == QLatin1String("PreToolUse")) {
        if (asks && fresh) {
            m_hookState = PaneState::Question;
            m_dialogAt = at;
        }
    } else if (event == QLatin1String("PermissionRequest")) {
        if (fresh) {
            m_hookState = asks ? PaneState::Question : PaneState::Permission;
            m_dialogAt = at;
        }
    } else if (event == QLatin1String("Notification")) {
        if (fresh && type == QLatin1String("permission_prompt") && m_hookState != PaneState::Question) {
            m_hookState = PaneState::Permission;
        } else if (fresh && (type == QLatin1String("elicitation_dialog") || type == QLatin1String("elicitation_url_dialog"))) {
            m_hookState = PaneState::Question;
        }
    } else if (event == QLatin1String("Stop") || event == QLatin1String("StopFailure")) {
        // Shells, monitors or subagents that still run bring Claude back when they
        // end, so the pane is not done yet. That next turn's Stop updates the list.
        m_backgroundTasks = userTasks(fields.value(6));
        m_hookState = m_backgroundTasks.isEmpty() ? PaneState::Done : PaneState::Background;
        m_stopAt = ok ? at : QDateTime::currentMSecsSinceEpoch();
        m_seen = false;
    } else if (event == QLatin1String("SessionEnd")) {
        m_exited = true;
    }
    if (ok) {
        m_lastEventAt = qMax(m_lastEventAt, at);
    }
    recompute();
}

void Pane::recompute()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    PaneState s = PaneState::Starting;
    if (m_exited) {
        s = PaneState::Exited;
    } else if (m_animating) {
        s = PaneState::Working;
    } else {
        switch (m_hookState) {
        case PaneState::Permission:
        case PaneState::Question:
            s = m_hookState;
            break;
        case PaneState::Working:
            // Without title symbols only Stop ends a turn. With them, a still symbol
            // means the turn is over (finished or interrupted with Esc), after a grace
            // for the title, which Konsole reports about 2 s late.
            if (!m_titleSeen || now - m_promptAt < 3500) {
                s = PaneState::Working;
                if (m_titleSeen) {
                    QTimer::singleShot(3600, this, &Pane::recompute);
                }
            } else {
                s = m_seen ? PaneState::Idle : PaneState::Done;
            }
            break;
        case PaneState::Done:
            s = m_seen ? PaneState::Idle : PaneState::Done;
            break;
        case PaneState::Background:
            s = PaneState::Background;
            break;
        case PaneState::Idle:
            s = PaneState::Idle;
            break;
        case PaneState::Starting:
        case PaneState::Exited:
            s = m_titleSeen ? PaneState::Idle : PaneState::Starting;
            break;
        }
    }
    setState(s);
}

bool Pane::isBusy() const
{
    return m_state == PaneState::Working || m_state == PaneState::Background || Theme::needsYou(m_state);
}

void Pane::markSeen()
{
    if (!m_seen) {
        m_seen = true;
        recompute();
    }
}

void Pane::restartClaude()
{
    if (!m_terminal || m_state != PaneState::Exited) {
        return;
    }
    QString command = m_account.command;
    if (!m_sessionId.isEmpty()) {
        command += QStringLiteral(" --resume ") + m_sessionId;
    }
    m_terminal->sendInput(command + QLatin1Char('\n'));
    focusTerminal();
}

bool Pane::canRestart() const
{
    return m_terminal != nullptr;
}

bool Pane::eventFilter(QObject *watched, QEvent *event)
{
    // Enter, Esc or a digit while a dialog waits: if Claude neither starts working
    // nor reports anything soon after, the dialog was declined or dismissed.
    if (event->type() == QEvent::KeyPress && Theme::needsYou(m_state)) {
        const int key = static_cast<QKeyEvent *>(event)->key();
        if (key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Escape || (key >= Qt::Key_1 && key <= Qt::Key_9)) {
            m_answerKeyAt = QDateTime::currentMSecsSinceEpoch();
            QTimer::singleShot(2000, this, &Pane::checkAnswered);
        }
    }
    return QWidget::eventFilter(watched, event);
}

void Pane::checkAnswered()
{
    if (Theme::needsYou(m_state) && !m_animating && m_lastEventAt < m_answerKeyAt) {
        m_hookState = PaneState::Idle;
        m_seen = true;
        recompute();
    }
}

void Pane::setState(PaneState state)
{
    if (state == m_state) {
        return;
    }
    const PaneState previous = m_state;
    m_state = state;
    if (qEnvironmentVariableIsSet("KONVOY_DEBUG")) {
        fprintf(stderr, "konvoy: %lld pane %s %s -> %s\n", QDateTime::currentMSecsSinceEpoch() % 100000, qPrintable(m_folder),
                qPrintable(Theme::stateText(previous)), qPrintable(Theme::stateText(state)));
    }
    m_stateSince = QDateTime::currentMSecsSinceEpoch();
    if (m_header) {
        m_header->update();
    }
    Q_EMIT stateChanged(this, previous);
    Q_EMIT changed(this);
}

void Pane::setCurrent(bool current)
{
    if (current == m_current) {
        return;
    }
    m_current = current;
    m_header->update();
    Q_EMIT changed(this);
}

void Pane::refreshHeader()
{
    m_header->update();
}

void Pane::focusTerminal()
{
    if (m_part && m_part->widget()) {
        m_part->widget()->setFocus(Qt::OtherFocusReason);
    }
}

void Pane::watchClaude()
{
    if (!m_terminal) {
        return;
    }
    // Same claude as last time: nothing to do, and no /proc scan.
    if (m_claudePid > 0) {
        QFile comm(QStringLiteral("/proc/%1/comm").arg(m_claudePid));
        if (comm.open(QIODevice::ReadOnly) && comm.readAll().trimmed() == "claude") {
            checkBackground();
            return;
        }
        m_claudePid = 0;
    }
    const bool hadClaude = m_titleSeen || m_hookState != PaneState::Starting;
    QHash<qint64, QString> names;
    const QList<qint64> pids = descendants(m_terminal->terminalProcessId(), &names);
    for (qint64 pid : pids) {
        if (names.value(pid) != QLatin1String("claude")) {
            continue;
        }
        m_claudePid = pid;
        if (m_exited) {
            m_exited = false;
            recompute();
        }
        const Account *account = m_config->accountForConfigDir(readEnvironmentVariable(pid, "CLAUDE_CONFIG_DIR"));
        if (account && account->key != m_account.key) {
            m_account = *account;
            m_header->update();
            Q_EMIT changed(this);
        }
        return;
    }
    // Claude is gone without a SessionEnd (a crash, a kill): the pane shows it.
    if (hadClaude && !m_exited) {
        m_exited = true;
        recompute();
    }
}

void Pane::checkBackground()
{
    // Claude starts a turn by itself when a background task ends, and that turn's
    // Stop brings the new list. A shell stopped from Claude's task list may end
    // without one, so when only shells were listed and none has run for a few
    // checks while Claude stays still, the pane is done.
    const bool onlyShells = std::all_of(m_backgroundTasks.cbegin(), m_backgroundTasks.cend(), [](const QString &kind) {
        return kind == QLatin1String("shell");
    });
    if (m_state != PaneState::Background || m_animating || !onlyShells || hasShellChild(m_claudePid)) {
        m_quietChecks = 0;
        return;
    }
    if (++m_quietChecks < 4) {
        return;
    }
    m_quietChecks = 0;
    m_backgroundTasks.clear();
    m_hookState = PaneState::Done;
    recompute();
}

bool Pane::claudeRunning() const
{
    if (!m_terminal) {
        return false;
    }
    const int shellPid = m_terminal->terminalProcessId();
    if (shellPid <= 0) {
        return false;
    }
    QHash<qint64, QString> names;
    const QList<qint64> pids = descendants(shellPid, &names);
    for (qint64 pid : pids) {
        if (names.value(pid) == QLatin1String("claude")) {
            return true;
        }
    }
    return false;
}

void Pane::onOverrideShortcut(QKeyEvent *event, bool &override)
{
    Q_EMIT shortcutCheck(event, override);
}
