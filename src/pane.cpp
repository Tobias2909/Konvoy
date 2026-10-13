#include "pane.h"
#include "shortcuts.h"

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
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>

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
    p.setPen(pal.color(QPalette::PlaceholderText));
    const qreal titleX = 32 + folderWidth + 8;
    const qreal titleWidth = r.width() - titleX - 36;
    p.drawText(QRectF(titleX, 0, titleWidth, r.height() - 2), Qt::AlignVCenter | Qt::AlignLeft,
               QFontMetrics(small).elidedText(m_pane->title(), Qt::ElideRight, int(titleWidth)));

    Theme::paintStateIcon(p, QPointF(r.width() - 18, (r.height() - 2) / 2), state);
}

void PaneHeader::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton) {
        Q_EMIT middleClicked();
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
    QTimer::singleShot(0, this, &Pane::releaseReservedShortcuts);

    // /switch replaces the claude process with one for another account. Checking the
    // running claude's CLAUDE_CONFIG_DIR keeps the badge right, hooks or not.
    auto *watch = new QTimer(this);
    watch->setInterval(2000);
    connect(watch, &QTimer::timeout, this, &Pane::watchClaude);
    watch->start();
}

Pane::~Pane()
{
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
    // Only Claude's titles count; they start with its status glyph. Shell or
    // profile titles ("Profile 1", "zsh") are ignored.
    const QString title = stripStatusGlyph(caption);
    if (title == caption.trimmed() || title.isEmpty() || title == m_title) {
        return;
    }
    m_title = title;
    m_header->update();
    Q_EMIT changed(this);
}

void Pane::setState(PaneState state)
{
    if (state == m_state) {
        return;
    }
    m_state = state;
    m_stateSince = QDateTime::currentMSecsSinceEpoch();
    if (m_header) {
        m_header->update();
    }
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
            return;
        }
        m_claudePid = 0;
    }
    QHash<qint64, QString> names;
    const QList<qint64> pids = descendants(m_terminal->terminalProcessId(), &names);
    for (qint64 pid : pids) {
        if (names.value(pid) != QLatin1String("claude")) {
            continue;
        }
        m_claudePid = pid;
        const Account *account = m_config->accountForConfigDir(readEnvironmentVariable(pid, "CLAUDE_CONFIG_DIR"));
        if (account && account->key != m_account.key) {
            m_account = *account;
            m_header->update();
            Q_EMIT changed(this);
        }
        return;
    }
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
