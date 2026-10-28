#include "ui/FileInfoWindow.h"

#include "skin/Skin.h"
#include "ui/SkinElements.h"

#include <QFontMetrics>
#include <QHash>
#include <QKeyEvent>
#include <QPainter>
#include <QPointer>

namespace newamp {

namespace {

constexpr int LabelWidth = 58;
constexpr int RowHeight = 14;
constexpr int FieldHeight = 12;
constexpr int DetailHeight = 10;
constexpr int BarHeight = 13;
constexpr int ButtonWidth = 34;

const TagField *findTag(const TrackInfo &info, const QString &key)
{
    for (const TagField &field : info.tags)
        if (field.key == key)
            return &field;
    return nullptr;
}

} // namespace

FileInfoWindow::FileInfoWindow(const Skin *skin, EditorLookup editorFor, QWidget *parent)
    : GenericWindow(skin, tr("File Info"), parent, DefaultSize)
    , m_editorFor(std::move(editorFor))
{
    setObjectName(QStringLiteral("fileinfo"));
    setSkinSize(skinSize());

    for (const QString &key : standardTagKeys()) {
        Row row;
        row.field.key = key;
        row.edit = addElement<SkinTextField>();
        row.edit->setReadOnly(true);
        const int index = int(m_rows.size());
        row.edit->onClicked = [this, index] { focusRow(index); };
        row.edit->onEdited = [this] {
            m_message.clear();
            updateButtons();
        };
        m_rows.push_back(row);
    }
    m_saveButton = addElement<SkinTextButton>(tr("Save"), [this] { save(); });
    m_revertButton = addElement<SkinTextButton>(tr("Revert"), [this] { revert(); });

    layoutElements();
    updateButtons();
}

QKeySequence FileInfoWindow::shortcut()
{
#ifdef Q_OS_MACOS
    return QKeySequence(Qt::CTRL | Qt::Key_I);
#else
    return QKeySequence(Qt::ALT | Qt::Key_3);
#endif
}

QString FileInfoWindow::label(const QString &key)
{
    static const QHash<QString, const char *> labels = {
        {QStringLiteral("TITLE"), QT_TR_NOOP("Title")},
        {QStringLiteral("ARTIST"), QT_TR_NOOP("Artist")},
        {QStringLiteral("ALBUM"), QT_TR_NOOP("Album")},
        {QStringLiteral("ALBUMARTIST"), QT_TR_NOOP("Album artist")},
        {QStringLiteral("TRACKNUMBER"), QT_TR_NOOP("Track")},
        {QStringLiteral("DISCNUMBER"), QT_TR_NOOP("Disc")},
        {QStringLiteral("DATE"), QT_TR_NOOP("Year")},
        {QStringLiteral("GENRE"), QT_TR_NOOP("Genre")},
        {QStringLiteral("COMMENT"), QT_TR_NOOP("Comment")},
    };
    const char *text = labels.value(key);
    return text ? tr(text) : key;
}

void FileInfoWindow::showTrack(const Track &track)
{
    m_track = track;
    load();
    show();
    raise();
    activateWindow();
}

void FileInfoWindow::load()
{
    const int generation = ++m_generation;
    m_message.clear();
    MetadataEditor *editor = m_editorFor ? m_editorFor(m_track) : nullptr;
    if (!editor) {
        // Only what the playlist knows.
        TrackInfo info;
        info.tags = {{QStringLiteral("TITLE"), m_track.title},
                     {QStringLiteral("ARTIST"), m_track.artist},
                     {QStringLiteral("ALBUM"), m_track.album}};
        info.details = {{tr("Location"), m_track.location.toDisplayString(QUrl::RemoveQuery)}};
        info.note = tr("This source has no more details, and its tags can't be edited.");
        setInfo(info);
        return;
    }
    m_busy = true;
    setInfo({});
    m_message = tr("Loading...");
    QPointer<FileInfoWindow> self(this);
    editor->readInfo(m_track, [self, generation](const TrackInfo &info) {
        if (!self || generation != self->m_generation)
            return;
        self->m_busy = false;
        self->m_message.clear();
        self->setInfo(info);
    });
}

void FileInfoWindow::setInfo(const TrackInfo &info)
{
    m_info = info;
    const int focused = focusedRow();
    for (Row &row : m_rows) {
        const TagField *field = findTag(info, row.field.key);
        row.field = field ? *field : TagField{row.field.key, {}};
        row.original = row.field.value;
        row.edit->setText(row.field.value);
        row.edit->setReadOnly(!info.canEdit || !field || field->readOnly);
        row.edit->setFocus(false);
    }
    // Keep the focus where it was, else on the first field that can change.
    int next = focused >= 0 && !m_rows[focused].edit->isReadOnly() ? focused : -1;
    for (int i = 0; next < 0 && i < int(m_rows.size()); ++i)
        if (!m_rows[i].edit->isReadOnly())
            next = i;
    focusRow(next);
    updateButtons();
    update();
}

bool FileInfoWindow::isModified() const
{
    for (const Row &row : m_rows)
        if (!row.edit->isReadOnly() && row.edit->text() != row.original)
            return true;
    return false;
}

void FileInfoWindow::setTag(const QString &key, const QString &value)
{
    for (Row &row : m_rows)
        if (row.field.key == key && !row.edit->isReadOnly()) {
            row.edit->setText(value);
            updateButtons();
        }
}

QString FileInfoWindow::tag(const QString &key) const
{
    for (const Row &row : m_rows)
        if (row.field.key == key)
            return row.edit->text();
    return {};
}

void FileInfoWindow::save()
{
    MetadataEditor *editor = m_editorFor ? m_editorFor(m_track) : nullptr;
    if (!editor || m_busy || !m_info.canEdit || !isModified())
        return;
    QList<TagField> changes;
    for (const Row &row : m_rows)
        if (!row.edit->isReadOnly() && row.edit->text() != row.original)
            changes.append({row.field.key, row.edit->text().trimmed(), row.field.wholeAlbum, false});

    m_busy = true;
    m_message = tr("Saving...");
    updateButtons();
    update();
    const int generation = m_generation;
    const Track before = m_track;
    QPointer<FileInfoWindow> self(this);
    editor->writeTags(m_track, changes, [self, generation, before, editor](const QString &error) {
        if (!self || generation != self->m_generation)
            return;
        self->m_busy = false;
        if (!error.isEmpty()) {
            self->m_message = error;
            self->updateButtons();
            self->update();
            return;
        }
        // Read back what was stored: formats and servers may normalise it.
        self->m_busy = true;
        editor->readInfo(before, [self, generation, before](const TrackInfo &info) {
            if (!self || generation != self->m_generation)
                return;
            self->m_busy = false;
            self->setInfo(info);
            self->m_message = tr("Saved");
            Track updated = before;
            for (const auto &[key, member] : {std::pair{"TITLE", &Track::title}, std::pair{"ARTIST", &Track::artist},
                                              std::pair{"ALBUM", &Track::album}}) {
                if (const TagField *field = findTag(info, QString::fromLatin1(key)))
                    updated.*member = field->value;
            }
            if (updated.title.isEmpty())
                updated.title = before.title; // keep the file name rather than nothing
            self->m_track = updated;
            self->update();
            Q_EMIT self->trackUpdated(before, updated);
        });
    });
}

void FileInfoWindow::revert()
{
    for (Row &row : m_rows)
        row.edit->setText(row.original);
    m_message.clear();
    updateButtons();
}

void FileInfoWindow::focusRow(int index)
{
    for (int i = 0; i < int(m_rows.size()); ++i)
        m_rows[i].edit->setFocus(i == index);
}

int FileInfoWindow::focusedRow() const
{
    for (int i = 0; i < int(m_rows.size()); ++i)
        if (m_rows[i].edit->hasFocus())
            return i;
    return -1;
}

void FileInfoWindow::updateButtons()
{
    const bool modified = !m_busy && m_info.canEdit && isModified();
    m_saveButton->setEnabled(modified);
    m_revertButton->setEnabled(modified);
    update();
}

void FileInfoWindow::paintContent(QPainter &painter, const Skin &skin, const QRect &content)
{
    const PlaylistStyle &style = skin.playlistStyle();
    painter.fillRect(content, style.normalBackground);
    painter.setFont(style.font());
    const QFontMetrics metrics(painter.font());
    QColor line = style.normal;
    line.setAlphaF(0.35);

    bool anyWholeAlbum = false;
    for (const Row &row : m_rows) {
        if (!row.edit->isVisible())
            continue;
        const QRect field = row.edit->rect();
        const QRect labelRect(content.left() + 2, field.top(), LabelWidth - 4, field.height());
        QString text = label(row.field.key);
        if (row.field.wholeAlbum) {
            text += QLatin1Char('*');
            anyWholeAlbum = true;
        }
        painter.setPen(style.normal);
        painter.drawText(labelRect, Qt::AlignLeft | Qt::AlignVCenter, metrics.elidedText(text, Qt::ElideRight, labelRect.width()));
    }

    // Read-only details, two columns, as many as fit.
    painter.fillRect(QRect(m_details.left(), m_details.top() - 2, m_details.width(), 1), line);
    int y = m_details.top();
    if (!m_info.error.isEmpty()) {
        painter.setPen(style.current);
        painter.drawText(m_details, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, m_info.error);
    }
    for (const auto &[name, value] : m_info.details) {
        if (y + DetailHeight > m_details.bottom() + 1)
            break;
        const QRect nameRect(m_details.left() + 2, y, LabelWidth - 4, DetailHeight);
        const QRect valueRect(m_details.left() + LabelWidth, y, m_details.width() - LabelWidth - 2, DetailHeight);
        painter.setPen(style.normal);
        painter.drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter, name);
        painter.setPen(style.current);
        painter.drawText(valueRect, Qt::AlignLeft | Qt::AlignVCenter,
                         metrics.elidedText(value, Qt::ElideMiddle, valueRect.width()));
        y += DetailHeight;
    }

    // Bottom bar: buttons, then the note or the last message.
    painter.fillRect(QRect(m_bottomBar.left(), m_bottomBar.top(), m_bottomBar.width(), 1), line);
    QString note = m_message.isEmpty() ? m_info.note : m_message;
    if (m_message.isEmpty() && anyWholeAlbum)
        note = tr("* changes the whole album. ") + note;
    painter.setPen(style.normal);
    const QRect status = m_bottomBar.adjusted(m_revertButton->rect().right() - m_bottomBar.left() + 6, 1, -2, 0);
    painter.drawText(status, Qt::AlignRight | Qt::AlignVCenter, metrics.elidedText(note, Qt::ElideRight, status.width()));
}

void FileInfoWindow::layoutContent(const QRect &content)
{
    if (!m_saveButton)
        return; // called from the base constructor
    m_bottomBar = QRect(content.left(), content.bottom() - BarHeight + 1, content.width(), BarHeight);
    m_saveButton->setRect(QRect(m_bottomBar.left() + 2, m_bottomBar.top() + 2, ButtonWidth, BarHeight - 3));
    m_revertButton->setRect(m_saveButton->rect().translated(ButtonWidth + 3, 0));

    int y = content.top() + 2;
    for (Row &row : m_rows) {
        row.edit->setRect(QRect(content.left() + LabelWidth, y, content.width() - LabelWidth - 2, FieldHeight));
        y += RowHeight;
    }
    m_details = QRect(content.left(), y + 3, content.width(), m_bottomBar.top() - y - 5);
}

void FileInfoWindow::keyPressEvent(QKeyEvent *event)
{
    const int focused = focusedRow();
    const bool editable = m_info.canEdit;
    if (event->matches(QKeySequence::Save)) {
        save();
        event->accept();
        return;
    }
    switch (event->key()) {
    case Qt::Key_Tab:
    case Qt::Key_Backtab:
    case Qt::Key_Down:
    case Qt::Key_Up:
        if (editable) {
            const bool back = event->key() == Qt::Key_Backtab || event->key() == Qt::Key_Up
                || (event->modifiers() & Qt::ShiftModifier);
            const int count = int(m_rows.size());
            int next = focused < 0 ? 0 : focused;
            // Skip the fields this source doesn't offer.
            for (int i = 0; i < count; ++i) {
                next = (next + (back ? count - 1 : 1)) % count;
                if (!m_rows[next].edit->isReadOnly())
                    break;
            }
            focusRow(next);
            event->accept();
            return;
        }
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        save();
        event->accept();
        return;
    case Qt::Key_Escape:
        if (isModified())
            revert();
        else
            hide();
        event->accept();
        return;
    default: break;
    }
    // Typing goes to the field before the player keys (Z, X, C, ...) get it.
    if (focused >= 0 && m_rows[focused].edit->keyPress(event)) {
        event->accept();
        return;
    }
    GenericWindow::keyPressEvent(event);
}

} // namespace newamp
