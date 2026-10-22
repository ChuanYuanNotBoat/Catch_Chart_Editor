#include "ChartMergeDialog.h"
#include "merge/MergeExport.h"
#include "file/ProjectIO.h"
#include "utils/Settings.h"
#include "utils/NativeWindowTheme.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QSplitter>
#include <QTableWidget>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>
#include <QtConcurrent>

namespace
{
QString seconds(double s) { return QString::number(s, 'f', 2)+" s"; }
QTableWidget *table(const QStringList &labels, QWidget *parent)
{
    auto *t = new QTableWidget(0, labels.size(), parent);
    t->setHorizontalHeaderLabels(labels);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    t->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    t->verticalHeader()->hide();
    t->setMinimumHeight(100);
    return t;
}
void row(QTableWidget *t, const QStringList &values)
{
    const int r = t->rowCount(); t->insertRow(r);
    for (int c = 0; c < values.size(); ++c)
    { auto *item = new QTableWidgetItem(values[c]); item->setToolTip(values[c]); t->setItem(r,c,item); }
}
}

ChartMergeDialog::ChartMergeDialog(const QString &libraryRoot, QWidget *parent)
    : QDialog(parent), m_libraryRoot(libraryRoot), m_inputs(std::make_shared<QTemporaryDir>())
{
    setWindowTitle(tr("Merge Charts"));
    setObjectName("ChartMergeDialog");
    setWindowModality(Qt::NonModal);
    const auto theme = NativeWindowTheme::themeColorsFor(Settings::instance().backgroundColor());
    auto colors = palette();
    colors.setColor(QPalette::Window,theme.window); colors.setColor(QPalette::WindowText,theme.text);
    colors.setColor(QPalette::Base,theme.base); colors.setColor(QPalette::Text,theme.text);
    setPalette(colors);
    setStyleSheet(NativeWindowTheme::dialogStyleSheet(Settings::instance().backgroundColor()));
    resize(1120, 800);
    auto *outer = new QVBoxLayout(this);
    auto *scroll = new QScrollArea(this); scroll->setWidgetResizable(true);
    m_settings = new QWidget(scroll);
    m_settings->setObjectName("chartMergeSettings");
    m_settings->setStyleSheet(QStringLiteral("QWidget#chartMergeSettings { background-color: %1; }").arg(theme.window.name()));
    auto *layout = new QVBoxLayout(m_settings);
    scroll->setWidget(m_settings); outer->addWidget(scroll);
    auto *actions = new QHBoxLayout;
    const auto button = [&](const QString &text, auto handler) {
        auto *b = new QPushButton(text, this); actions->addWidget(b); connect(b, &QPushButton::clicked, this, handler); return b;
    };
    button(tr("Import Charts / Packages..."), [this] {
        addPaths(QFileDialog::getOpenFileNames(this,tr("Import Charts / Packages"),Settings::instance().lastOpenPath(),tr("Charts (*.mc *.mcz *.zip)")));
    });
    button(tr("Choose from Library..."), [this] {
        addPaths({QFileDialog::getExistingDirectory(this,tr("Choose Chart Folder"),m_libraryRoot)});
    });
    button(tr("Add Selected"), [this] { addSelection(); })->setObjectName("mergeAddSelected"); actions->addStretch();
    layout->addLayout(actions);
    m_library = table({tr("Song / Difficulty"),tr("Notes / Rain"),tr("BPM"),tr("Playable Duration")},this);
    m_library->setObjectName("mergeLibrary");
    m_library->setSelectionMode(QAbstractItemView::ExtendedSelection);
    layout->addWidget(m_library);
    connect(m_library,&QTableWidget::itemDoubleClicked,this,[this] { addSelection(); });
    layout->addWidget(new QLabel(tr("Merge order — select a segment to edit its settings:"),this));
    m_queue = table({tr("Song / Difficulty"),tr("Notes / Rain"),tr("BPM"),tr("Duration"),tr("Speed / Pitch"),tr("First → Last Note")},this);
    m_queue->setObjectName("mergeQueue"); m_queue->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(m_queue);
    connect(m_queue,&QTableWidget::itemSelectionChanged,this,&ChartMergeDialog::selectSegment);
    auto *order = new QHBoxLayout;
    const auto orderButton = [&](const QString &text, int direction) {
        auto *b = new QPushButton(text,this); order->addWidget(b);
        connect(b,&QPushButton::clicked,this,[this,direction] {
            const int r = m_queue->currentRow();
            if (r < 0 || r >= m_segments.size()) return;
            if (direction == 0) m_segments.removeAt(r);
            else if (r+direction >= 0 && r+direction < m_segments.size()) m_segments.swapItemsAt(r,r+direction);
            else return;
            invalidate(); rebuild();
            if (!m_segments.isEmpty()) m_queue->selectRow(qBound(0,r+direction,int(m_segments.size())-1));
        });
    };
    orderButton(tr("Move Up"),-1); orderButton(tr("Move Down"),1); orderButton(tr("Remove"),0); order->addStretch(); layout->addLayout(order);
    auto *fields = new QHBoxLayout;
    auto *segmentForm = new QFormLayout;
    const auto spin = [&](const QString &name, double min, double max, double val, const QString &suffix) {
        auto *s = new QDoubleSpinBox(this); s->setObjectName(name); s->setRange(min,max); s->setDecimals(2);
        s->setSingleStep(0.05); s->setValue(val); s->setSuffix(suffix);
        connect(s,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this] { updateSegment(); }); return s;
    };
    m_rate = spin("mergeRate",0.25,4,1," ×");
    m_fadeIn = spin("mergeFadeIn",0,600,5," s"); m_fadeOut = spin("mergeFadeOut",0,600,5," s");
    m_gap = spin("mergeGap",0,600,10," s");
    m_pitch = new QCheckBox(tr("Change pitch with speed"),this); m_pitch->setObjectName("mergePitch");
    connect(m_pitch,&QCheckBox::toggled,this,[this] { updateSegment(); });
    segmentForm->addRow(tr("Segment speed"),m_rate); segmentForm->addRow(QString(),m_pitch);
    segmentForm->addRow(tr("Fade in"),m_fadeIn); segmentForm->addRow(tr("Fade out"),m_fadeOut);
    segmentForm->addRow(tr("Gap to next segment"),m_gap); fields->addLayout(segmentForm);
    auto *metaForm = new QFormLayout;
    const auto text = [&](const QString &name, const QString &initial) {
        auto *edit = new QLineEdit(initial,this); edit->setObjectName(name);
        connect(edit,&QLineEdit::textChanged,this,[this] { invalidate(); }); return edit;
    };
    m_title = text("mergeTitle",{}); m_title->setPlaceholderText(tr("First song title"));
    m_artist = text("mergeArtist",{}); m_artist->setPlaceholderText(tr("First song artist"));
    m_creator = text("mergeCreator","CCE"); m_difficulty = text("mergeDifficulty","Mix");
    metaForm->addRow(tr("Title"),m_title); metaForm->addRow(tr("Artist"),m_artist);
    metaForm->addRow(tr("Creator"),m_creator); metaForm->addRow(tr("Difficulty"),m_difficulty);
    const auto image = [&](const QString &label, const QString &name) {
        auto *edit = text(name,{}); auto *line = new QHBoxLayout; line->addWidget(edit);
        auto *pick = new QPushButton(tr("Browse..."),this); line->addWidget(pick);
        connect(pick,&QPushButton::clicked,this,[this,edit] {
            const auto path = QFileDialog::getOpenFileName(this,tr("Choose Image"),{},tr("Images (*.png *.jpg *.jpeg *.webp)"));
            if (!path.isEmpty()) edit->setText(path);
        });
        metaForm->addRow(label,line); return edit;
    };
    m_background = image(tr("Background"),"mergeBackground"); m_cover = image(tr("Cover"),"mergeCover");
    fields->addLayout(metaForm); layout->addLayout(fields);
    auto *hint = new QLabel(tr("Gap = previous last note (including Rain end) → next first note. Intros and tails each use up to half the gap. Fades stay outside playable notes. Uncheck pitch change to preserve pitch. Maximum merged audio: 30 minutes."),this);
    hint->setWordWrap(true); layout->addWidget(hint);
    m_summary = new QLabel(this); m_summary->setWordWrap(true); layout->addWidget(m_summary);
    m_status = new QLabel(this); m_status->setObjectName("mergeStatus"); m_status->setWordWrap(true); outer->addWidget(m_status);
    m_progress = new QProgressBar(this); m_progress->hide(); outer->addWidget(m_progress);
    auto *bottom = new QHBoxLayout;
    m_render = new QPushButton(tr("Merge"),this); m_render->setObjectName("mergeRender");
    m_export = new QPushButton(tr("Export MCZ..."),this); m_export->setObjectName("mergeExport");
    m_preview = new QPushButton(tr("Preview in CCE"),this); m_preview->setObjectName("mergePreview");
    m_cancel = new QPushButton(tr("Cancel Task"),this); m_cancel->hide();
    auto *close = new QPushButton(tr("Close"),this);
    for (auto *b : {m_render,m_export,m_preview,m_cancel,close}) { b->setAutoDefault(false); bottom->addWidget(b); }
    outer->addLayout(bottom);
    connect(m_render,&QPushButton::clicked,this,&ChartMergeDialog::render);
    connect(m_export,&QPushButton::clicked,this,&ChartMergeDialog::exportPackage);
    connect(m_preview,&QPushButton::clicked,this,[this] { if (m_result) emit previewRequested(m_result->filePath("merged.cceproj")); });
    connect(close,&QPushButton::clicked,this,&ChartMergeDialog::close);
    connect(m_cancel,&QPushButton::clicked,this,[this] { if (m_job) m_job->cancel = true; });
    auto *timer = new QTimer(this); timer->setInterval(100);
    connect(timer,&QTimer::timeout,this,[this] { if (m_job) m_progress->setValue(m_job->progress); }); timer->start();
    connect(&m_watcher,&QFutureWatcher<Result>::finished,this,[this] {
        const auto result = m_watcher.result(); const bool cancelled = m_job && m_job->cancel;
        m_job.reset(); setBusy(false);
        if (cancelled || !result.error.isEmpty())
            m_status->setText(cancelled ? tr("Task cancelled. Settings retained.") : result.error);
        else
        {
            m_result = result.directory;
            m_status->setText(tr("Merge complete. Export the package or preview it in CCE with one layer per segment."));
            m_export->setEnabled(true); m_preview->setEnabled(true);
        }
    });
    rebuild(); selectSegment(); invalidate();
}

ChartMergeDialog::~ChartMergeDialog()
{
    if (m_job) m_job->cancel = true;
    m_watcher.waitForFinished();
}

void ChartMergeDialog::addPaths(const QStringList &paths)
{
    if (m_busy || paths.isEmpty() || !m_inputs->isValid()) return;
    // Extraction and media-independent parsing run away from the UI thread.
    setBusy(true); m_status->setText(tr("Importing charts..."));
    auto *watcher = new QFutureWatcher<QPair<QVector<ChartMerger::Source>,QStringList>>(this);
    const auto inputs = m_inputs;
    connect(watcher,&QFutureWatcherBase::finished,this,[this,watcher] {
        const auto data = watcher->result(); watcher->deleteLater(); setBusy(false);
        const int first = m_sources.size();
        for (const auto &src : data.first)
        {
            bool exists = false; for (const auto &s : m_sources) exists |= s.path == src.path;
            if (exists) continue;
            m_sources.append(src);
            row(m_library,{src.title+" ["+src.difficulty+"]",QString("%1 / %2").arg(src.normalCount).arg(src.rainCount),
                           QString("%1–%2").arg(src.minBpm).arg(src.maxBpm),seconds(src.lastSeconds-src.firstSeconds)});
        }
        m_library->clearSelection();
        for (int r = first; r < m_library->rowCount(); ++r)
            for (int c = 0; c < m_library->columnCount(); ++c) m_library->item(r,c)->setSelected(true);
        m_status->setText(data.second.isEmpty() ? tr("Select difficulties, then click Add Selected.") : data.second.join('\n'));
    });
    watcher->setFuture(QtConcurrent::run([paths,inputs] {
        QPair<QVector<ChartMerger::Source>,QStringList> result;
        for (const auto &path : paths)
        {
            if (path.isEmpty()) continue;
            QStringList charts;
            const QFileInfo info(path);
            if (info.isDir())
                for (const auto &c : ProjectIO::findChartsInDirectory(path)) charts.append(c.first);
            else if (info.suffix().compare("mc",Qt::CaseInsensitive) == 0) charts.append(path);
            else
            {
                QString root;
                if (!ProjectIO::extractMcz(path,inputs->filePath(QUuid::createUuid().toString(QUuid::WithoutBraces)),root))
                { result.second.append(QObject::tr("Cannot import package: %1").arg(path)); continue; }
                for (const auto &c : ProjectIO::findChartsInDirectory(root)) charts.append(c.first);
            }
            if (charts.isEmpty()) result.second.append(QObject::tr("No charts found: %1").arg(path));
            for (const auto &chart : charts)
            {
                ChartMerger::Source src; QString error;
                if (ChartMerger::load(chart,src,&error)) result.first.append(src); else result.second.append(error);
            }
        }
        return result;
    }));
}

void ChartMergeDialog::addSelection()
{
    auto rows = m_library->selectionModel()->selectedRows();
    if (rows.isEmpty()) return;
    std::sort(rows.begin(),rows.end(),[](const auto &a,const auto &b) { return a.row() < b.row(); });
    for (const auto &r : rows) { ChartMerger::Segment s; s.source = m_sources[r.row()]; m_segments.append(s); }
    if (m_segments.size() == rows.size() && !m_segments.isEmpty())
    {
        const auto &src = m_segments.first().source;
        const auto meta = src.json["meta"].toObject();
        if (!meta["background"].toString().isEmpty()) m_background->setText(ChartMerger::resourcePath(src,meta["background"].toString()));
        if (!meta["cover"].toString().isEmpty()) m_cover->setText(ChartMerger::resourcePath(src,meta["cover"].toString()));
    }
    invalidate(); rebuild(); if (!m_segments.isEmpty()) m_queue->selectRow(m_segments.size()-1);
}

void ChartMergeDialog::rebuild()
{
    m_refreshing = true;
    const int selected = m_queue->currentRow(); m_queue->setRowCount(0);
    double lastSeconds = 0;
    int count = 0;
    for (int i = 0; i < m_segments.size(); ++i)
    {
        const auto &s = m_segments[i]; const auto &src = s.source; count += src.normalCount+src.rainCount;
        const double firstSeconds = i ? lastSeconds+m_segments[i-1].gapAfter : std::max(0.0,src.firstSeconds/s.rate);
        lastSeconds = firstSeconds+(src.lastSeconds-src.firstSeconds)/s.rate;
        row(m_queue,{src.title+" ["+src.difficulty+"]",QString("%1 / %2").arg(src.normalCount).arg(src.rainCount),
                     QString("%1–%2").arg(src.minBpm*s.rate).arg(src.maxBpm*s.rate),seconds((src.lastSeconds-src.firstSeconds)/s.rate),
                     QString::number(s.rate,'f',2)+"× / "+(s.changePitch ? tr("Shift") : tr("Preserve")),
                     seconds(firstSeconds)+" → "+seconds(lastSeconds)});
    }
    m_summary->setText(m_segments.isEmpty() ? tr("Add charts to start.") :
        tr("%1 segments · %2 playable notes · last note at %3 (audio tail may extend further)").arg(m_segments.size()).arg(count).arg(seconds(lastSeconds)));
    if (selected >= 0 && selected < m_queue->rowCount()) m_queue->selectRow(selected);
    m_refreshing = false; selectSegment();
}

void ChartMergeDialog::selectSegment()
{
    if (m_refreshing) return;
    const int r = m_queue->currentRow(); const bool valid = r >= 0 && r < m_segments.size();
    for (auto *s : {m_rate,m_fadeIn,m_fadeOut,m_gap}) s->setEnabled(valid);
    m_pitch->setEnabled(valid);
    if (!valid) return;
    m_refreshing = true;
    const auto &s = m_segments[r];
    m_rate->setValue(s.rate); m_fadeIn->setValue(s.fadeIn); m_fadeOut->setValue(s.fadeOut); m_gap->setValue(s.gapAfter); m_pitch->setChecked(s.changePitch);
    m_gap->setEnabled(r+1 < m_segments.size());
    m_refreshing = false;
}

void ChartMergeDialog::updateSegment()
{
    if (m_refreshing) return;
    const int r = m_queue->currentRow(); if (r < 0 || r >= m_segments.size()) return;
    auto &s = m_segments[r]; s.rate = m_rate->value(); s.fadeIn = m_fadeIn->value(); s.fadeOut = m_fadeOut->value();
    s.gapAfter = m_gap->value(); s.changePitch = m_pitch->isChecked(); invalidate(); rebuild();
}

void ChartMergeDialog::invalidate()
{
    m_result.reset(); m_export->setEnabled(false); m_preview->setEnabled(false);
    m_render->setEnabled(!m_busy && !m_segments.isEmpty());
}

void ChartMergeDialog::setBusy(bool busy)
{
    m_busy = busy; m_settings->setEnabled(!busy); m_render->setEnabled(!busy && !m_segments.isEmpty());
    m_preview->setEnabled(!busy && bool(m_result)); m_export->setEnabled(!busy && bool(m_result));
    m_progress->setVisible(busy); m_progress->setValue(0); m_cancel->setVisible(busy && bool(m_job));
}

void ChartMergeDialog::render()
{
    if (m_busy || m_segments.isEmpty()) return;
    ChartMerger::Plan plan; QString error;
    if (!ChartMerger::plan(m_segments,m_title->text(),m_artist->text(),m_creator->text(),m_difficulty->text(),plan,&error))
    { m_status->setText(error); return; }
    const auto segments = m_segments; const auto background = m_background->text(), cover = m_cover->text();
    const auto inputs = m_inputs; m_job = std::make_shared<Job>(); const auto job = m_job;
    invalidate(); setBusy(true); m_status->setText(tr("Merging charts and audio..."));
    m_watcher.setFuture(QtConcurrent::run([segments,plan,background,cover,inputs,job] {
        Result result; result.directory = std::make_shared<QTemporaryDir>();
        try
        {
            if (!result.directory->isValid()) result.error = QObject::tr("Cannot create merge workspace.");
            else if (!MergeExport::write(segments,plan,result.directory->path(),background,cover,&result.error,
                [job](float p) { job->progress = qRound(p*100); return !job->cancel; }) && result.error.isEmpty())
                result.error = QObject::tr("Merge failed or cancelled.");
        }
        catch (const std::exception &e) { result.error = QString::fromUtf8(e.what()); }
        return result;
    }));
}

void ChartMergeDialog::exportPackage()
{
    if (!m_result || m_busy) return;
    auto path = QFileDialog::getSaveFileName(this,tr("Export Merged Package"),"merged.mcz",tr("Malody Package (*.mcz)"));
    if (path.isEmpty()) return;
    if (!path.endsWith(".mcz",Qt::CaseInsensitive)) path += ".mcz";
    setBusy(true); m_status->setText(tr("Exporting package..."));
    const auto result = m_result;
    auto *watcher = new QFutureWatcher<bool>(this);
    connect(watcher,&QFutureWatcherBase::finished,this,[this,watcher,path] {
        const bool ok = watcher->result(); watcher->deleteLater(); setBusy(false);
        m_status->setText(ok ? tr("Exported: %1").arg(path) : tr("Package export failed."));
    });
    watcher->setFuture(QtConcurrent::run([result,path] { return ProjectIO::exportToMczPure(path,result->filePath("merged.mc")); }));
}

void ChartMergeDialog::closeEvent(QCloseEvent *event)
{
    if (m_job) m_job->cancel = true;
    QDialog::closeEvent(event);
}
void ChartMergeDialog::reject()
{
    if (m_job) m_job->cancel = true;
    QDialog::reject();
}
