#pragma once

#include <QAbstractButton>
#include <QDialog>
#include <QListWidget>

#include "config.h"
#include "usage.h"

#include <optional>

class QButtonGroup;
class QComboBox;
class QTimer;
class QDialogButtonBox;
class QLineEdit;

// One account in the dialog: badge, name and its usage. An account at its limit is
// greyed out, with the full window's 100% in red, and can still be picked.
class AccountButton : public QAbstractButton
{
    Q_OBJECT
public:
    AccountButton(const Account &account, QWidget *parent = nullptr);
    QSize sizeHint() const override;
    const Account &account() const { return m_account; }
    void setUsage(const AccountUsage *usage); // null: none known

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    Account m_account;
    std::optional<AccountUsage> m_usage;
    bool m_hover = false;
};

// Folder list with a star in front of every entry. Clicking the star pins or unpins.
class FolderList : public QListWidget
{
    Q_OBJECT
public:
    explicit FolderList(QWidget *parent = nullptr);
    // Shown instead of the folders while set (e.g. why the list is greyed out).
    void setMessage(const QString &message);

Q_SIGNALS:
    void starClicked(const QString &folder);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    QString m_message;
};

class NewPaneDialog : public QDialog
{
    Q_OBJECT
public:
    // openSessions: chats already running in a pane, which cannot be continued twice.
    NewPaneDialog(Config *config, const UsageModel *usage, const QStringList &openSessions, QWidget *parent = nullptr);

    QString accountKey() const;
    QString folder() const;
    QString sessionId() const; // empty for a new chat
    bool isProgram() const;

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    void fillFolders(const QString &select);
    void togglePin(const QString &folder);
    void updateOk();
    void fillChats();

    Config *m_config;
    QButtonGroup *m_accounts;
    FolderList *m_folders;
    QLineEdit *m_path;
    QComboBox *m_chats;
    QTimer *m_chatsTimer;
    QStringList m_openSessions;
    QDialogButtonBox *m_buttons;
    QList<QWidget *> m_folderParts; // greyed out while a program is chosen
};
