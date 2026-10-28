#include "ui/JumpToFileDialog.h"

#include "core/PlayerController.h"
#include "core/Playlist.h"

#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>

namespace newamp {

JumpToFileDialog::JumpToFileDialog(PlayerController &controller, QWidget *parent)
    : QDialog(parent)
    , m_controller(controller)
{
    setWindowTitle(tr("Jump to file"));
    resize(420, 360);

    m_filter = new QLineEdit(this);
    m_filter->setPlaceholderText(tr("Search the playlist"));
    m_filter->setClearButtonEnabled(true);
    m_filter->installEventFilter(this);
    m_list = new QListWidget(this);

    auto *buttons = new QDialogButtonBox(this);
    QPushButton *playButton = buttons->addButton(tr("Play"), QDialogButtonBox::AcceptRole);
    QPushButton *queueButton = buttons->addButton(tr("Queue"), QDialogButtonBox::ActionRole);
    buttons->addButton(QDialogButtonBox::Close);
    playButton->setDefault(true);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_filter);
    layout->addWidget(m_list);
    layout->addWidget(buttons);

    connect(m_filter, &QLineEdit::textChanged, this, &JumpToFileDialog::refill);
    connect(m_list, &QListWidget::itemActivated, this, &JumpToFileDialog::play);
    connect(playButton, &QPushButton::clicked, this, &JumpToFileDialog::play);
    connect(queueButton, &QPushButton::clicked, this, &JumpToFileDialog::queue);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(&m_controller.playlist(), &Playlist::tracksChanged, this, &JumpToFileDialog::refill);
    refill();
}

void JumpToFileDialog::open(PlayerController &controller, QWidget *parent)
{
    static QPointer<JumpToFileDialog> dialog;
    if (!dialog) {
        dialog = new JumpToFileDialog(controller, parent ? parent->window() : nullptr);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
    }
    dialog->m_filter->selectAll();
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
    dialog->m_filter->setFocus();
}

void JumpToFileDialog::refill()
{
    const Playlist &playlist = m_controller.playlist();
    m_list->clear();
    for (int row : playlist.search(m_filter->text())) {
        auto *item = new QListWidgetItem(QStringLiteral("%1. %2").arg(row + 1).arg(playlist.at(row).displayTitle()),
                                         m_list);
        item->setData(Qt::UserRole, row);
    }
    m_list->setCurrentRow(0);
}

int JumpToFileDialog::pickedRow() const
{
    const QListWidgetItem *item = m_list->currentItem();
    return item ? item->data(Qt::UserRole).toInt() : -1;
}

void JumpToFileDialog::play()
{
    const int row = pickedRow();
    if (row < 0)
        return;
    m_controller.playIndex(row);
    accept();
}

void JumpToFileDialog::queue()
{
    const int row = pickedRow();
    if (row >= 0)
        m_controller.playlist().toggleQueued({row});
}

bool JumpToFileDialog::eventFilter(QObject *watched, QEvent *event)
{
    // Up and down in the search box move through the matches.
    if (watched == m_filter && event->type() == QEvent::KeyPress) {
        const int key = static_cast<QKeyEvent *>(event)->key();
        if (key == Qt::Key_Up || key == Qt::Key_Down || key == Qt::Key_PageUp || key == Qt::Key_PageDown) {
            QCoreApplication::sendEvent(m_list, event);
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}

} // namespace newamp
