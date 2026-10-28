#pragma once

#include <QDialog>

class QLineEdit;
class QListWidget;

namespace newamp {

class PlayerController;

// The classic "Jump to file" box (J): type to filter the playlist, Enter
// plays the pick, Queue adds it to the play queue. A plain dialog, like the
// file pickers, rather than a skinned window.
class JumpToFileDialog : public QDialog
{
    Q_OBJECT

public:
    JumpToFileDialog(PlayerController &controller, QWidget *parent = nullptr);

    // Opens the one shared dialog, or raises it if it's already open.
    static void open(PlayerController &controller, QWidget *parent);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void refill();
    int pickedRow() const;
    void play();
    void queue();

    PlayerController &m_controller;
    QLineEdit *m_filter = nullptr;
    QListWidget *m_list = nullptr;
};

} // namespace newamp
