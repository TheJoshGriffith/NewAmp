#pragma once

#include "core/MetadataEditor.h"
#include "ui/GenericWindow.h"

#include <QKeySequence>

#include <functional>
#include <vector>

namespace newamp {

class SkinTextButton;
class SkinTextField;

// File Info (Alt+3): a track's tags, editable where its source allows, and
// read-only details such as format and bitrate. Local files are edited
// through TagLib; plugins can offer their own MetadataEditor.
class FileInfoWindow : public GenericWindow
{
    Q_OBJECT

public:
    // The editor for a track's source, or null when it has none.
    using EditorLookup = std::function<MetadataEditor *(const Track &track)>;
    static constexpr QSize DefaultSize{275 + 3 * 25, 116 + 6 * 29};

    FileInfoWindow(const Skin *skin, EditorLookup editorFor, QWidget *parent = nullptr);

    // Alt+3 as in Winamp; Cmd+I on macOS, where Option+3 types a character.
    static QKeySequence shortcut();

    // Load a track and show the window. Unsaved edits to the previous one
    // are dropped.
    void showTrack(const Track &track);

    const Track &track() const { return m_track; }
    const TrackInfo &info() const { return m_info; }
    bool isModified() const;

    // Tests and the window's own controls.
    void setTag(const QString &key, const QString &value);
    QString tag(const QString &key) const;
    void save();
    void revert();
    QString message() const { return m_message; }

Q_SIGNALS:
    // Tags were saved; updated carries the new title, artist and album so
    // playlist entries for the same track can follow.
    void trackUpdated(const Track &before, const Track &updated);

protected:
    void paintContent(QPainter &painter, const Skin &skin, const QRect &content) override;
    void layoutContent(const QRect &content) override;
    void keyPressEvent(QKeyEvent *event) override;
    // Tab moves between the fields, not between widgets.
    bool focusNextPrevChild(bool) override { return false; }

private:
    struct Row
    {
        TagField field;
        QString original;
        SkinTextField *edit = nullptr;
    };

    void load();
    void setInfo(const TrackInfo &info);
    void focusRow(int index);
    int focusedRow() const;
    void updateButtons();
    static QString label(const QString &key);

    EditorLookup m_editorFor;
    Track m_track;
    TrackInfo m_info;
    std::vector<Row> m_rows; // one per standard tag key, made once
    QString m_message;       // replaces the note until the next load
    bool m_busy = false;     // a read or save is on its way
    int m_generation = 0;    // drops replies for a track no longer shown

    SkinTextButton *m_saveButton = nullptr;
    SkinTextButton *m_revertButton = nullptr;
    QRect m_details;
    QRect m_bottomBar;
};

} // namespace newamp
