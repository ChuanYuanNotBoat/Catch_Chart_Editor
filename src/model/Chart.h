#pragma once

#include <QVector>
#include "Note.h"
#include "BpmEntry.h"
#include "MetaData.h"

class Chart
{
public:
    Chart();

    void addNote(const Note &note);
    void removeNote(int index);
    void removeNote(const Note &note);
    void clearNotes();
    const QVector<Note> &notes() const;
    QVector<Note> &notes();

    void addBpm(const BpmEntry &bpm);
    void removeBpm(int index);
    void updateBpm(int index, const BpmEntry &bpm);
    const QVector<BpmEntry> &bpmList() const;
    QVector<BpmEntry> &bpmList();

    MetaData &meta();
    const MetaData &meta() const;

    void sortNotes();
    bool isValid() const;
    void clear();

    // 音频源完整路径（仅编辑器内部使用，不序列化到 .mc）
    QString audioSourceFullPath() const { return m_audioSourceFullPath; }
    void setAudioSourceFullPath(const QString &path) { m_audioSourceFullPath = path; }

private:
    QVector<Note> m_notes;
    QVector<BpmEntry> m_bpmList;
    MetaData m_meta;
    QString m_audioSourceFullPath;   // 新增：音频源完整路径
};
