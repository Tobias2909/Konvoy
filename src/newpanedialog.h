#pragma once

#include <QAbstractButton>
#include <QDialog>
#include <QListWidget>

#include "config.h"

class UsageModel;
class QButtonGroup;
class QDialogButtonBox;
class QLineEdit;

// One account in the dialog: badge, name and its usage.
class AccountButton : public QAbstractButton
{
    Q_OBJECT
public:
    AccountButton(const Account &account, QWidget *parent = nullptr);
    QSize sizeHint() const override;
    const Account &account() const { return m_account; }
    void setUsageText(const QString &text);

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    Account m_account;
    QString m_usage;
    bool m_hover = false;
};

// Folder list with a star in front of every entry. Clicking the star pins or unpins.
class FolderList : public QListWidget
{
    Q_OBJECT
public:
    explicit FolderList(QWidget *parent = nullptr);

Q_SIGNALS:
    void starClicked(const QString &folder);

protected:
    void mousePressEvent(QMouseEvent *event) override;
};

class NewPaneDialog : public QDialog
{
    Q_OBJECT
public:
    NewPaneDialog(Config *config, const UsageModel *usage, QWidget *parent = nullptr);

    QString accountKey() const;
    QString folder() const;

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    void fillFolders(const QString &select);
    void togglePin(const QString &folder);
    void updateOk();

    Config *m_config;
    QButtonGroup *m_accounts;
    FolderList *m_folders;
    QLineEdit *m_path;
    QDialogButtonBox *m_buttons;
};
