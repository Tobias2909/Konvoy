#include "newpanedialog.h"
#include "theme.h"
#include "usage.h"

#include <QButtonGroup>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

namespace
{
constexpr int starWidth = 26;
constexpr int folderRole = Qt::UserRole + 1;

QLabel *sectionLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    QFont f = label->font();
    f.setWeight(QFont::DemiBold);
    label->setFont(f);
    return label;
}
}

// ---------------------------------------------------------------- AccountButton

AccountButton::AccountButton(const Account &account, QWidget *parent)
    : QAbstractButton(parent)
    , m_account(account)
{
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setToolTip(QStringLiteral("%1 (press %2)").arg(account.name, account.key));
}

QSize AccountButton::sizeHint() const
{
    return QSize(150, 54);
}

void AccountButton::setUsageText(const QString &text)
{
    m_usage = text;
    update();
}

void AccountButton::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPalette pal = palette();
    const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);

    QColor bg = pal.color(QPalette::Window).lighter(isChecked() ? 150 : (m_hover ? 135 : 125));
    p.setPen(isChecked() ? QPen(m_account.color, 2) : (hasFocus() ? QPen(Theme::accent(), 1) : QPen(Qt::NoPen)));
    p.setBrush(isChecked() ? Theme::withAlpha(m_account.color, 0.22) : bg);
    p.drawRoundedRect(r, 6, 6);

    Theme::paintBadge(p, QRectF(r.x() + 10, r.y() + 9, 20, 20), m_account.key, m_account.color);

    QFont bold = font();
    bold.setWeight(QFont::DemiBold);
    p.setFont(bold);
    p.setPen(pal.color(QPalette::WindowText));
    p.drawText(QRectF(r.x() + 38, r.y() + 8, r.width() - 44, 22), Qt::AlignVCenter | Qt::AlignLeft, m_account.name);

    QFont small = font();
    small.setPointSizeF(font().pointSizeF() - 1);
    p.setFont(small);
    p.setPen(pal.color(QPalette::PlaceholderText));
    p.drawText(QRectF(r.x() + 10, r.y() + 32, r.width() - 16, 16),
               Qt::AlignVCenter | Qt::AlignLeft,
               m_usage.isEmpty() ? QStringLiteral("5h –   7d –") : m_usage);
}

void AccountButton::enterEvent(QEnterEvent *)
{
    m_hover = true;
    update();
}

void AccountButton::leaveEvent(QEvent *)
{
    m_hover = false;
    update();
}

// ---------------------------------------------------------------- FolderList

FolderList::FolderList(QWidget *parent)
    : QListWidget(parent)
{
    setIconSize(QSize(16, 16));
    setUniformItemSizes(true);
}

void FolderList::mousePressEvent(QMouseEvent *event)
{
    QListWidgetItem *item = itemAt(event->position().toPoint());
    if (item && event->button() == Qt::LeftButton && event->position().x() < starWidth) {
        Q_EMIT starClicked(item->data(folderRole).toString());
        return;
    }
    QListWidget::mousePressEvent(event);
}

// ---------------------------------------------------------------- NewPaneDialog

NewPaneDialog::NewPaneDialog(Config *config, const UsageModel *usage, QWidget *parent)
    : QDialog(parent)
    , m_config(config)
{
    setWindowTitle(QStringLiteral("New pane"));
    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(8);

    layout->addWidget(sectionLabel(QStringLiteral("Account"), this));
    auto *accountRow = new QHBoxLayout;
    m_accounts = new QButtonGroup(this);
    m_accounts->setExclusive(true);
    for (const Account &account : config->accounts()) {
        auto *button = new AccountButton(account, this);
        button->setUsageText(usage->shortText(account.key));
        connect(usage, &UsageModel::changed, button, [button, usage] {
            button->setUsageText(usage->shortText(button->account().key));
        });
        m_accounts->addButton(button);
        accountRow->addWidget(button);
    }
    accountRow->addStretch();
    layout->addLayout(accountRow);
    connect(m_accounts, &QButtonGroup::buttonToggled, this, &NewPaneDialog::updateOk);

    layout->addSpacing(6);
    layout->addWidget(sectionLabel(QStringLiteral("Folder"), this));
    m_folders = new FolderList(this);
    m_folders->setMinimumHeight(220);
    layout->addWidget(m_folders);
    connect(m_folders, &FolderList::starClicked, this, &NewPaneDialog::togglePin);
    connect(m_folders, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) {
        if (item) {
            m_path->setText(Config::collapseHome(item->data(folderRole).toString()));
        }
    });
    connect(m_folders, &QListWidget::itemDoubleClicked, this, [this] {
        if (m_buttons->button(QDialogButtonBox::Ok)->isEnabled()) {
            accept();
        }
    });

    auto *pathRow = new QHBoxLayout;
    m_path = new QLineEdit(this);
    m_path->setPlaceholderText(QStringLiteral("~/folder"));
    pathRow->addWidget(m_path, 1);
    auto *browse = new QPushButton(QIcon::fromTheme(QStringLiteral("document-open-folder")), QStringLiteral("Browse…"), this);
    pathRow->addWidget(browse);
    layout->addLayout(pathRow);
    connect(m_path, &QLineEdit::textChanged, this, &NewPaneDialog::updateOk);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString start = folder().isEmpty() ? QDir::homePath() : folder();
        const QString chosen = QFileDialog::getExistingDirectory(this, QStringLiteral("Folder for the new pane"), start);
        if (!chosen.isEmpty()) {
            m_path->setText(Config::collapseHome(chosen));
        }
    });

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Open"));
    layout->addWidget(m_buttons);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    fillFolders(config->lastFolder());
    updateOk();
    resize(560, sizeHint().height());
}

QString NewPaneDialog::accountKey() const
{
    auto *button = qobject_cast<AccountButton *>(m_accounts->checkedButton());
    return button ? button->account().key : QString();
}

QString NewPaneDialog::folder() const
{
    const QString text = m_path->text().trimmed();
    return text.isEmpty() ? QString() : QDir::cleanPath(Config::expandHome(text));
}

void NewPaneDialog::fillFolders(const QString &select)
{
    const QSignalBlocker blocker(m_folders);
    m_folders->clear();
    const QStringList pinned = m_config->pinnedFolders();
    QStringList folders = pinned;
    for (const QString &f : m_config->recentFolders()) {
        if (!folders.contains(f)) {
            folders.append(f);
        }
    }
    QListWidgetItem *selected = nullptr;
    for (const QString &f : std::as_const(folders)) {
        const bool isPinned = pinned.contains(f);
        auto *item = new QListWidgetItem(QIcon::fromTheme(isPinned ? QStringLiteral("starred-symbolic") : QStringLiteral("non-starred-symbolic")),
                                         Config::collapseHome(f), m_folders);
        item->setData(folderRole, f);
        item->setToolTip(isPinned ? QStringLiteral("Pinned. Click the star to unpin.") : QStringLiteral("Recent. Click the star to pin."));
        if (!QDir(f).exists()) {
            item->setForeground(palette().color(QPalette::PlaceholderText));
        }
        if (f == select) {
            selected = item;
        }
    }
    if (selected) {
        m_folders->setCurrentItem(selected);
        m_path->setText(Config::collapseHome(select));
    }
}

void NewPaneDialog::togglePin(const QString &folder)
{
    QStringList pinned = m_config->pinnedFolders();
    if (pinned.contains(folder)) {
        pinned.removeAll(folder);
    } else {
        pinned.append(folder);
    }
    m_config->setPinnedFolders(pinned);
    const QString keep = this->folder();
    fillFolders(keep);
}

void NewPaneDialog::updateOk()
{
    const QString f = folder();
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(!accountKey().isEmpty() && !f.isEmpty() && QDir(f).exists());
}

void NewPaneDialog::keyPressEvent(QKeyEvent *event)
{
    // The account letter selects that account, unless the user is typing a path.
    if (!m_path->hasFocus() && event->modifiers() == Qt::NoModifier) {
        const QString key = event->text().toUpper();
        for (QAbstractButton *b : m_accounts->buttons()) {
            auto *button = static_cast<AccountButton *>(b);
            if (!key.isEmpty() && button->account().key.compare(key, Qt::CaseInsensitive) == 0) {
                button->setChecked(true);
                return;
            }
        }
    }
    QDialog::keyPressEvent(event);
}
