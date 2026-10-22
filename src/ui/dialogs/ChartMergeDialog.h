#pragma once
#include "merge/ChartMerger.h"
#include <QDialog>
#include <QFutureWatcher>
#include <QTemporaryDir>
#include <atomic>
#include <memory>

class QTableWidget;
class QLineEdit;
class QLabel;
class QDoubleSpinBox;
class QCheckBox;
class QPushButton;
class QProgressBar;
class QCloseEvent;

class ChartMergeDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ChartMergeDialog(const QString &libraryRoot, QWidget *parent = nullptr);
    ~ChartMergeDialog() override;
    void addPaths(const QStringList &paths);
signals:
    void previewRequested(const QString &projectPath);
protected:
    void closeEvent(QCloseEvent *event) override;
    void reject() override;
private:
    struct Result
    {
        std::shared_ptr<QTemporaryDir> directory;
        QString error;
    };
    struct Job
    {
        std::atomic<bool> cancel{false};
        std::atomic<int> progress{0};
    };
    void addSelection();
    void rebuild();
    void selectSegment();
    void updateSegment();
    void invalidate();
    void render();
    void exportPackage();
    void setBusy(bool busy);
    QString m_libraryRoot;
    std::shared_ptr<QTemporaryDir> m_inputs;
    std::shared_ptr<QTemporaryDir> m_result;
    QVector<ChartMerger::Source> m_sources;
    QVector<ChartMerger::Segment> m_segments;
    QTableWidget *m_library, *m_queue;
    QLineEdit *m_title, *m_artist, *m_creator, *m_difficulty, *m_background, *m_cover;
    QDoubleSpinBox *m_rate, *m_fadeIn, *m_fadeOut, *m_gap;
    QCheckBox *m_pitch;
    QLabel *m_summary, *m_status;
    QPushButton *m_render, *m_preview, *m_export, *m_cancel;
    QProgressBar *m_progress;
    QWidget *m_settings;
    bool m_refreshing = false, m_busy = false;
    std::shared_ptr<Job> m_job;
    QFutureWatcher<Result> m_watcher;
};
