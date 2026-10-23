#include "JellyfinSignInDialog.h"

#include "JellyfinClient.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QUdpSocket>
#include <QVBoxLayout>

namespace newamp::jellyfin {

JellyfinSignInDialog::JellyfinSignInDialog(JellyfinClient &client, QWidget *parent)
    : QDialog(parent)
    , m_client(client)
    , m_server(new QComboBox(this))
    , m_user(new QLineEdit(this))
    , m_password(new QLineEdit(this))
    , m_apiKey(new QLineEdit(this))
    , m_message(new QLabel(this))
    , m_buttons(new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this))
    , m_discovery(new QUdpSocket(this))
{
    setWindowTitle(tr("Sign in to Jellyfin"));
    m_server->setEditable(true);
    m_server->setInsertPolicy(QComboBox::NoInsert);
    m_server->setMinimumWidth(280);
    m_server->lineEdit()->setPlaceholderText(QStringLiteral("http://192.168.1.20:8096"));
    m_server->setEditText(client.serverUrl());
    m_user->setText(client.userName());
    m_password->setEchoMode(QLineEdit::Password);
    m_apiKey->setEchoMode(QLineEdit::Password);
    m_apiKey->setPlaceholderText(tr("Optional, instead of a password"));
    m_message->setWordWrap(true);
    m_buttons->button(QDialogButtonBox::Ok)->setText(tr("Sign In"));

    auto *form = new QFormLayout;
    form->addRow(tr("Server:"), m_server);
    form->addRow(tr("User name:"), m_user);
    form->addRow(tr("Password:"), m_password);
    form->addRow(tr("API key:"), m_apiKey);
    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(m_message);
    layout->addWidget(m_buttons);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &JellyfinSignInDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &JellyfinSignInDialog::reject);

    if (m_server->currentText().isEmpty())
        m_server->setFocus();
    else
        m_password->setFocus();
    discover();
}

void JellyfinSignInDialog::discover()
{
    // Jellyfin answers this broadcast with its address, as its apps use.
    connect(m_discovery, &QUdpSocket::readyRead, this, &JellyfinSignInDialog::readReplies);
    if (m_discovery->bind(QHostAddress::AnyIPv4, 0))
        m_discovery->writeDatagram(discoveryRequest(), QHostAddress::Broadcast, DiscoveryPort);
}

void JellyfinSignInDialog::readReplies()
{
    while (m_discovery->hasPendingDatagrams()) {
        QByteArray datagram(int(m_discovery->pendingDatagramSize()), '\0');
        m_discovery->readDatagram(datagram.data(), datagram.size());
        const DiscoveredServer server = parseDiscoveryReply(datagram);
        if (server.address.isEmpty() || m_server->findText(server.address) >= 0)
            continue;
        m_server->addItem(server.address);
        m_server->setItemData(m_server->count() - 1, server.name, Qt::ToolTipRole);
        if (m_server->currentText().isEmpty()) {
            m_server->setEditText(server.address);
            m_message->setText(tr("Found \"%1\" on this network.").arg(server.name));
        }
    }
}

void JellyfinSignInDialog::setBusy(bool busy)
{
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(!busy);
    for (QWidget *field : {static_cast<QWidget *>(m_server), static_cast<QWidget *>(m_user),
                           static_cast<QWidget *>(m_password), static_cast<QWidget *>(m_apiKey)})
        field->setEnabled(!busy);
}

void JellyfinSignInDialog::accept()
{
    setBusy(true);
    m_message->setText(tr("Signing in..."));
    QPointer<JellyfinSignInDialog> self(this);
    m_client.signIn(m_server->currentText(), m_user->text(), m_password->text(), m_apiKey->text(),
                    [self](const QString &error) {
                        if (!self)
                            return;
                        if (error.isEmpty()) {
                            self->QDialog::accept();
                            return;
                        }
                        self->setBusy(false);
                        self->m_message->setText(error);
                    });
}

void JellyfinSignInDialog::reject()
{
    m_client.cancelSignIn();
    QDialog::reject();
}

} // namespace newamp::jellyfin
