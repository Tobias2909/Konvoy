#include "programstatus.h"
#include "theme.h"

#include <QFontMetrics>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMouseEvent>
#include <QPainter>
#include <QProcess>
#include <QTimer>
#include <QToolButton>

namespace
{
constexpr int headerHeight = 28;
constexpr int lineTop = 29;
constexpr int lineHeight = 19;
constexpr int bottomGap = 6;
constexpr int buttonWidth = 24;
constexpr int buttonHeight = 22;
constexpr int maxButtons = 4;
constexpr int pollTimeoutMs = 10000;
constexpr int pressTimeoutMs = 60000;

void paintDot(QPainter &p, QPointF c, const QString &state, const QColor &dim)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    if (state == QLatin1String("ok")) {
        p.setPen(Qt::NoPen);
        p.setBrush(Theme::stateColor(PaneState::Done));
        p.drawEllipse(c, 4, 4);
    } else if (state == QLatin1String("warn")) {
        p.setPen(Qt::NoPen);
        p.setBrush(Theme::stateColor(PaneState::Permission));
        p.drawEllipse(c, 4, 4);
    } else if (state == QLatin1String("busy")) {
        Theme::paintStateIcon(p, c, PaneState::Working, 5.5);
    } else if (state == QLatin1String("locked")) {
        const QPointF o = c - QPointF(7, 7);
        p.setPen(QPen(dim, 1.3));
        p.setBrush(Qt::NoBrush);
        p.drawArc(QRectF(o.x() + 4.5, o.y() + 2, 5, 6), 0, 180 * 16);
        p.drawLine(o + QPointF(4.5, 5), o + QPointF(4.5, 6.5));
        p.drawLine(o + QPointF(9.5, 5), o + QPointF(9.5, 6.5));
        p.setPen(Qt::NoPen);
        p.setBrush(dim);
        p.drawRoundedRect(QRectF(o.x() + 3, o.y() + 6.5, 8, 5.5), 1, 1);
    } else {
        p.setPen(QPen(dim, 1.3));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(c, 3.5, 3.5);
    }
    p.restore();
}
}

ProgramStatusBlock::ProgramStatusBlock(const Account &program, QWidget *parent)
    : QWidget(parent)
    , m_program(program)
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    m_text = QStringLiteral("waiting for its status");
    setToolTip(QStringLiteral("%1: waiting for the first answer of\n%2").arg(m_program.name, m_program.status));

    m_timer = new QTimer(this);
    m_timer->setInterval(qMax(2, m_program.statusEvery) * 1000);
    connect(m_timer, &QTimer::timeout, this, &ProgramStatusBlock::poll);
    m_timer->start();

    m_deadline = new QTimer(this);
    m_deadline->setSingleShot(true);
    connect(m_deadline, &QTimer::timeout, this, [this] {
        if (m_process) {
            m_process->kill();
        }
    });
}

QSize ProgramStatusBlock::sizeHint() const
{
    return QSize(200, lineTop + lineHeight + bottomGap);
}

QSize ProgramStatusBlock::minimumSizeHint() const
{
    return sizeHint();
}

void ProgramStatusBlock::poll()
{
    // Hidden in the collapsed sidebar: no polls until it shows again.
    if (!m_process && isVisible()) {
        run(QString());
    }
}

void ProgramStatusBlock::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    poll();
}

void ProgramStatusBlock::run(const QString &button)
{
    if (m_process) {
        // One command at a time: a click waits for the poll that runs.
        m_queued = button;
        return;
    }
    m_pressing = button;
    m_process = new QProcess(this);
    m_process->setStandardInputFile(QProcess::nullDevice());
    connect(m_process, &QProcess::finished, this, &ProgramStatusBlock::finished);
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            finished();
        }
    });
    // The command runs in a shell like a pane's command; a button's id comes as "$1".
    QStringList args{QStringLiteral("-c"), m_program.status + QStringLiteral(" \"$@\""), QStringLiteral("sh")};
    if (!button.isEmpty()) {
        args.append(button);
    }
    m_process->start(QStringLiteral("/bin/sh"), args);
    m_deadline->start(button.isEmpty() ? pollTimeoutMs : pressTimeoutMs);
}

void ProgramStatusBlock::finished()
{
    QProcess *process = m_process;
    if (!process) {
        return;
    }
    m_process = nullptr;
    m_deadline->stop();
    process->deleteLater();
    const QByteArray out = process->readAllStandardOutput();
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(out, &error);
    if (process->error() == QProcess::FailedToStart) {
        fail(QStringLiteral("could not start /bin/sh"));
    } else if (process->exitStatus() != QProcess::NormalExit) {
        fail(QStringLiteral("it did not answer within %1 s").arg((m_pressing.isEmpty() ? pollTimeoutMs : pressTimeoutMs) / 1000));
    } else if (process->exitCode() != 0 || !doc.isObject()) {
        const QString err = QString::fromUtf8(process->readAllStandardError()).trimmed().section(QLatin1Char('\n'), 0, 0);
        fail(process->exitCode() != 0 ? QStringLiteral("exit code %1%2").arg(process->exitCode()).arg(err.isEmpty() ? QString() : QStringLiteral(": ") + err)
                                      : QStringLiteral("its output is not a JSON object (%1)").arg(error.errorString()));
    } else {
        apply(doc.object());
    }
    m_pressing.clear();
    if (!m_queued.isEmpty()) {
        const QString next = m_queued;
        m_queued.clear();
        run(next);
    }
}

void ProgramStatusBlock::apply(const QJsonObject &status)
{
    m_state = status.value(QStringLiteral("state")).toString(QStringLiteral("off"));
    m_text = status.value(QStringLiteral("text")).toString();
    setToolTip(status.value(QStringLiteral("tip")).toString());
    QList<Button> buttons;
    for (const QJsonValue &v : status.value(QStringLiteral("buttons")).toArray()) {
        const QJsonObject o = v.toObject();
        Button b{o.value(QStringLiteral("id")).toString(), o.value(QStringLiteral("icon")).toString(),
                 o.value(QStringLiteral("tip")).toString(), o.value(QStringLiteral("enabled")).toBool(true),
                 o.value(QStringLiteral("pane")).toBool(false)};
        if (!b.id.isEmpty() && buttons.size() < maxButtons) {
            buttons.append(b);
        }
    }
    m_buttons = buttons;
    placeButtons();
    update();
}

void ProgramStatusBlock::fail(const QString &why)
{
    m_state = QStringLiteral("warn");
    m_text = QStringLiteral("no status");
    setToolTip(QStringLiteral("%1's status command failed: %2\n%3").arg(m_program.name, why, m_program.status));
    m_buttons.clear();
    placeButtons();
    update();
}

void ProgramStatusBlock::placeButtons()
{
    while (m_tools.size() < m_buttons.size()) {
        auto *tool = new QToolButton(this);
        tool->setAutoRaise(true);
        tool->setIconSize(QSize(16, 16));
        tool->setFixedSize(buttonWidth, buttonHeight);
        const int index = m_tools.size();
        connect(tool, &QToolButton::clicked, this, [this, index] {
            if (index >= m_buttons.size()) {
                return;
            }
            const Button &b = m_buttons.at(index);
            if (b.pane) {
                Q_EMIT paneRequested(m_program.key);
            } else {
                run(b.id);
            }
        });
        m_tools.append(tool);
    }
    for (int i = 0; i < m_tools.size(); ++i) {
        QToolButton *tool = m_tools.at(i);
        if (i >= m_buttons.size()) {
            tool->hide();
            continue;
        }
        const Button &b = m_buttons.at(i);
        tool->setIcon(QIcon::fromTheme(b.icon, QIcon::fromTheme(QStringLiteral("system-run"))));
        tool->setToolTip(b.tip);
        tool->setEnabled(b.enabled);
        tool->move(width() - 6 - buttonWidth * (m_buttons.size() - i), 3);
        tool->show();
    }
}

void ProgramStatusBlock::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    placeButtons();
}

void ProgramStatusBlock::mousePressEvent(QMouseEvent *)
{
    // Not the sidebar's empty space: the press stops here.
}

void ProgramStatusBlock::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::TextAntialiasing);
    const QPalette pal = palette();
    const QColor dim = pal.color(QPalette::PlaceholderText);
    const QColor dimmer = Theme::withAlpha(dim, 0.75);
    const qreal w = width();

    QFont heading = font();
    heading.setPointSizeF(font().pointSizeF() - 2);
    heading.setWeight(QFont::Bold);
    QFont small = font();
    small.setPointSizeF(font().pointSizeF() - 1);

    p.fillRect(QRectF(8, 0, w - 16, 1), pal.color(QPalette::Mid));
    const QRectF badge(10, 5, 18, 18);
    Theme::paintBadge(p, badge, m_program);
    const qreal buttonsLeft = w - 6 - buttonWidth * m_buttons.size() - 4;
    p.setFont(heading);
    p.setPen(dimmer);
    p.drawText(QRectF(34, 5, buttonsLeft - 34, 18), Qt::AlignVCenter | Qt::AlignLeft,
               QFontMetrics(heading).elidedText(m_program.name.toUpper(), Qt::ElideRight, int(buttonsLeft - 34)));

    paintDot(p, QPointF(19, lineTop + lineHeight / 2.0), m_state, dim);
    p.setFont(small);
    // Quiet states are dim; a running one is in the normal text colour.
    p.setPen(m_state == QLatin1String("busy") ? pal.color(QPalette::WindowText) : dimmer);
    p.drawText(QRectF(32, lineTop, w - 42, lineHeight), Qt::AlignVCenter | Qt::AlignLeft,
               QFontMetrics(small).elidedText(m_text, Qt::ElideRight, int(w - 42)));
}
