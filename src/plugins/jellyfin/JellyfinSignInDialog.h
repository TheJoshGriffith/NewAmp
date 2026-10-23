#pragma once

#include <QDialog>

class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QUdpSocket;

namespace newamp::jellyfin {

class JellyfinClient;

// Asks for the server address and a user name with a password or an API
// key, and stays open until the client signs in or the user gives up.
// Servers on the local network fill the address list as they answer.
class JellyfinSignInDialog final : public QDialog
{
    Q_OBJECT

public:
    JellyfinSignInDialog(JellyfinClient &client, QWidget *parent = nullptr);

    void accept() override;
    void reject() override;

private:
    void discover();
    void readReplies();
    void setBusy(bool busy);

    JellyfinClient &m_client;
    QComboBox *m_server;
    QLineEdit *m_user;
    QLineEdit *m_password;
    QLineEdit *m_apiKey;
    QLabel *m_message;
    QDialogButtonBox *m_buttons;
    QUdpSocket *m_discovery;
};

} // namespace newamp::jellyfin
