#include "TransientPanel.h"
#include "AnalysisCanvas.h"
#include <QBoxLayout>
#include <QFormLayout>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QLabel>
#include <QTableWidget>
#include <QHeaderView>
#include <QTabWidget>
#include <QPushButton>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QFileDialog>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QtConcurrentRun>
#include <algorithm>
#include <cmath>
#include <functional>
namespace
{
QString dispositionText(analysis::PeakDisposition disposition)
{
    switch (disposition)
    {
        case analysis::PeakDisposition::Detected:
            return QStringLiteral("detected");
        case analysis::PeakDisposition::BelowThreshold:
            return QStringLiteral("below threshold");
        case analysis::PeakDisposition::TooClose:
            return QStringLiteral("minimum interval");
        case analysis::PeakDisposition::PageBoundary:
            return QStringLiteral("page boundary");
    }
    return {};
}
} // namespace
class TransientPlot : public QWidget
{
public:
    TransientPlot(AnalysisCanvas *canvas, const analysis::TransientResult *result, QWidget *parent)
        : QWidget(parent), canvas(canvas), result(result)
    {
        setObjectName("analysis.transientPlot");
        setMinimumSize(170, 220);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }
    std::array<bool, 4> visible{{true, true, true, true}};
    double selectedTime = -1;
    std::function<void(double)> seek;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor(20, 24, 31));
        const QRectF area(55, 24, qMax(1, width() - 67), qMax(1, height() - 42));
        const double first = canvas->timeAtY(28) / 1000, last = canvas->timeAtY(canvas->height()) / 1000;
        if (qAbs(last - first) < 1e-9)
            return;
        auto yAt = [&](double t) {
            return area.top() + (t - first) / (last - first) * area.height();
        };
        p.setPen(QColor(125, 132, 145));
        for (int i = 0; i <= 4; ++i)
        {
            const double t = first + (last - first) * i / 4;
            const double y = area.top() + area.height() * i / 4;
            p.drawText(QRectF(0, y - 9, 50, 18), Qt::AlignRight | Qt::AlignVCenter, QString::number(t, 'f', 2));
            p.setPen(QColor(44, 50, 62));
            p.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
            p.setPen(QColor(125, 132, 145));
        }
        p.drawText(QRect(0, 0, width(), 20), Qt::AlignCenter, tr("Audio seconds / onset strength →"));
        if (!result->valid())
        {
            p.drawText(area, Qt::AlignCenter, tr("Load audio to inspect transients"));
            return;
        }
        const double low = qMin(first, last), high = qMax(first, last);
        float maximum = 1e-7f;
        for (const auto &f : result->frames)
            if (f.timeSeconds >= low && f.timeSeconds <= high)
                for (int k = 0; k < 4; ++k)
                    if (visible[k])
                        maximum = std::max(maximum, k ? f.bands[k - 1] : f.strength);
        const std::array<QColor, 4> colors{
            {QColor(225, 231, 242), QColor(255, 176, 75), QColor(107, 220, 136), QColor(102, 175, 255)}};
        p.save();
        p.setClipRect(area);
        p.setRenderHint(QPainter::Antialiasing);
        for (int k = 0; k < 5; ++k)
        {
            if (k < 4 && !visible[k])
                continue;
            QPainterPath path;
            bool started = false;
            for (const auto &f : result->frames)
            {
                if (f.timeSeconds < low - .1 || f.timeSeconds > high + .1)
                    continue;
                const double value = k == 4 ? f.threshold : k ? f.bands[k - 1] : f.strength;
                const QPointF point(area.left() + std::min(1., value / maximum) * area.width(), yAt(f.timeSeconds));
                if (!started)
                    path.moveTo(point);
                else
                    path.lineTo(point);
                started = true;
            }
            p.setPen(QPen(k == 4 ? QColor(182, 125, 218) : colors[k], 1.2, k == 4 ? Qt::DashLine : Qt::SolidLine));
            p.drawPath(path);
        }
        for (const auto &peak : result->peaks)
            if (peak.timeSeconds >= low && peak.timeSeconds <= high)
            {
                const bool accepted = peak.disposition == analysis::PeakDisposition::Detected;
                p.setPen(accepted ? QColor(80, 230, 190) : QColor(158, 97, 103));
                p.drawEllipse(QPointF(area.left() + std::min(1., double(peak.strength / maximum)) * area.width(),
                                      yAt(peak.timeSeconds)),
                              accepted ? 3 : 2, accepted ? 3 : 2);
            }
        for (const auto &head : {std::pair<double, QColor>{canvas->currentTime() / 1000, QColor(255, 104, 108)},
                                 {selectedTime, QColor(75, 225, 234)}})
        {
            if (head.first < low || head.first > high)
                continue;
            p.setPen(QPen(head.second, 1, Qt::DashLine));
            p.drawLine(QPointF(area.left(), yAt(head.first)), QPointF(area.right(), yAt(head.first)));
        }
        p.restore();
        p.setPen(QColor(160, 168, 181));
        p.drawText(QRect(0, height() - 18, width(), 18), Qt::AlignCenter, tr("Click to seek · All threshold: dashed"));
    }
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton || event->position().y() < 24 || event->position().y() > height() - 18)
            return;
        const double fraction = (event->position().y() - 24) / qMax(1, height() - 42);
        const double time = canvas->timeAtY(28 + fraction * (canvas->height() - 28));
        if (seek)
            seek(time);
    }

private:
    AnalysisCanvas *canvas;
    const analysis::TransientResult *result;
};
TransientPanel::TransientPanel(AnalysisCanvas *canvas, QWidget *parent) : QWidget(parent), m_canvas(canvas)
{
    setObjectName("analysis.transientPanel");
    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    m_threshold = new QDoubleSpinBox;
    m_threshold->setObjectName("analysis.transientThreshold");
    m_threshold->setRange(0, 1);
    m_threshold->setSingleStep(.02);
    m_threshold->setDecimals(2);
    m_threshold->setValue(.12);
    m_interval = new QDoubleSpinBox;
    m_interval->setObjectName("analysis.transientInterval");
    m_interval->setRange(0, 2000);
    m_interval->setValue(60);
    m_interval->setSuffix(tr(" ms"));
    form->addRow(tr("Relative threshold"), m_threshold);
    form->addRow(tr("Minimum interval"), m_interval);
    layout->addLayout(form);
    m_summary = new QLabel(tr("Waiting for stereo spectrum…"));
    m_summary->setObjectName("analysis.transientSummary");
    m_summary->setWordWrap(true);
    layout->addWidget(m_summary);
    auto *tabs = new QTabWidget;
    tabs->setObjectName("analysis.transientTabs");
    auto *curves = new QWidget;
    auto *curvesLayout = new QVBoxLayout(curves);
    curvesLayout->setContentsMargins(0, 0, 0, 0);
    auto *legend = new QHBoxLayout;
    m_plot = new TransientPlot(canvas, &m_result, curves);
    const QStringList labels{tr("All"), tr("Low"), tr("Mid"), tr("High")};
    const QStringList colors{"#e1e7f2", "#ffb04b", "#6bdc88", "#66afff"};
    for (int i = 0; i < labels.size(); ++i)
    {
        auto *check = new QCheckBox(labels[i]);
        check->setChecked(true);
        check->setStyleSheet(QStringLiteral("QCheckBox { color: %1; } QCheckBox::indicator:checked { "
                                            "background-color: %1; border: 1px solid #727987; } "
                                            "QCheckBox::indicator:unchecked { background-color: #14181f; "
                                            "border: 1px solid #727987; }")
                                 .arg(colors[i]));
        legend->addWidget(check);
        connect(check, &QCheckBox::toggled, this, [this, i](bool on) {
            m_plot->visible[i] = on;
            m_plot->update();
        });
    }
    curvesLayout->addLayout(legend);
    curvesLayout->addWidget(m_plot, 1);
    tabs->addTab(curves, tr("Curves"));
    auto *peaksPage = new QWidget;
    auto *peaksLayout = new QVBoxLayout(peaksPage);
    peaksLayout->setContentsMargins(0, 0, 0, 0);
    m_rejected = new QCheckBox(tr("Show rejected peaks"));
    m_rejected->setObjectName("analysis.transientRejected");
    m_rejected->setChecked(true);
    peaksLayout->addWidget(m_rejected);
    m_peaks = new QTableWidget(0, 7);
    m_peaks->setObjectName("analysis.transientPeaks");
    m_peaks->setHorizontalHeaderLabels(
        {tr("Audio s"), tr("Result"), tr("Strength"), tr("Width ms"), tr("Low"), tr("Mid"), tr("High")});
    m_peaks->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_peaks->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_peaks->setSelectionMode(QAbstractItemView::SingleSelection);
    m_peaks->verticalHeader()->hide();
    m_peaks->horizontalHeader()->setDefaultSectionSize(76);
    m_peaks->setColumnWidth(0, 70);
    m_peaks->setColumnWidth(1, 116);
    m_peaks->setColumnWidth(3, 65);
    m_peaks->setAlternatingRowColors(true);
    peaksLayout->addWidget(m_peaks, 1);
    tabs->addTab(peaksPage, tr("Peaks"));
    layout->addWidget(tabs, 1);
    auto *exportButton = new QPushButton(tr("Export transient JSON…"));
    layout->addWidget(exportButton);
    auto *hint = new QLabel(
        tr("Display-band spectral flux; low <250 Hz, mid <2 kHz. Threshold is relative to the loaded audio page. Width "
           "measures the onset peak, not sound duration. Double-click a peak to inspect audio."));
    hint->setWordWrap(true);
    layout->addWidget(hint);
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    m_timer->setInterval(100);
    connect(m_timer, &QTimer::timeout, this, &TransientPanel::startAnalysis);
    connect(m_threshold, &QDoubleSpinBox::valueChanged, this, &TransientPanel::scheduleAnalysis);
    connect(m_interval, &QDoubleSpinBox::valueChanged, this, &TransientPanel::scheduleAnalysis);
    connect(m_rejected, &QCheckBox::toggled, this, &TransientPanel::fillPeaks);
    connect(exportButton, &QPushButton::clicked, this, &TransientPanel::exportResult);
    connect(canvas, &AnalysisCanvas::viewportChanged, m_plot, qOverload<>(&QWidget::update));
    m_plot->seek = [this](double ms) {
        emit seekRequested(ms);
    };
    connect(m_peaks, &QTableWidget::currentCellChanged, this, [this](int row, int, int, int) {
        m_plot->selectedTime = m_peaks->item(row, 0) ? m_peaks->item(row, 0)->data(Qt::UserRole).toDouble() : -1;
        m_plot->update();
    });
    connect(m_peaks, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (const auto *item = m_peaks->item(row, 0))
            emit seekRequested(item->data(Qt::UserRole).toDouble() * 1000);
    });
}
TransientPanel::~TransientPanel()
{
    if (m_cancel)
        m_cancel->store(true);
}
void TransientPanel::clearSource()
{
    ++m_generation;
    if (m_cancel)
        m_cancel->store(true);
    m_timer->stop();
    m_pending = false;
    m_spectrum.reset();
    m_result = {};
    m_plot->selectedTime = -1;
    fillPeaks();
    m_summary->setText(tr("Waiting for stereo spectrum…"));
    m_plot->update();
}
void TransientPanel::setSpectrum(const analysis::StereoSpectrum &spectrum)
{
    m_spectrum = std::make_shared<analysis::StereoSpectrum>(spectrum);
    scheduleAnalysis();
}
void TransientPanel::scheduleAnalysis()
{
    ++m_generation;
    if (m_cancel)
        m_cancel->store(true);
    m_result = {};
    m_plot->selectedTime = -1;
    fillPeaks();
    m_plot->update();
    m_pending = bool(m_spectrum);
    if (m_pending)
    {
        m_summary->setText(tr("Analyzing transients…"));
        m_timer->start();
    }
}
void TransientPanel::startAnalysis()
{
    if (m_busy || !m_pending || !m_spectrum)
        return;
    m_pending = false;
    m_busy = true;
    m_cancel = std::make_shared<std::atomic<bool>>(false);
    const auto cancel = m_cancel;
    const auto spectrum = m_spectrum;
    const auto generation = m_generation;
    const analysis::TransientOptions options{m_threshold->value(), m_interval->value() / 1000};
    auto *watcher = new QFutureWatcher<analysis::TransientResult>(this);
    connect(watcher, &QFutureWatcher<analysis::TransientResult>::finished, this, [this, watcher, generation, cancel] {
        auto result = watcher->result();
        watcher->deleteLater();
        m_busy = false;
        if (generation == m_generation && !cancel->load())
        {
            m_result = std::move(result);
            fillPeaks();
            const auto count = std::count_if(m_result.peaks.begin(), m_result.peaks.end(), [](const auto &p) {
                return p.disposition == analysis::PeakDisposition::Detected;
            });
            m_summary->setText(m_result.valid()
                                   ? tr("%1 detected / %2 rejected · hop %3 ms · audio %4–%5 s")
                                         .arg(count)
                                         .arg(m_result.peaks.size() - count)
                                         .arg(m_spectrum->hopSeconds * 1000, 0, 'f', 1)
                                         .arg(m_spectrum->startSeconds, 0, 'f', 2)
                                         .arg(m_spectrum->startSeconds + m_spectrum->durationSeconds, 0, 'f', 2)
                                   : QString::fromStdString(m_result.error));
            m_plot->update();
        }
        if (m_pending && !m_timer->isActive())
            m_timer->start();
    });
    watcher->setFuture(QtConcurrent::run([spectrum, options, cancel] {
        return analysis::analyzeTransients(*spectrum, options, cancel.get());
    }));
}
void TransientPanel::fillPeaks()
{
    const QSignalBlocker blocker(m_peaks);
    m_peaks->setRowCount(0);
    for (const auto &p : m_result.peaks)
    {
        if (!m_rejected->isChecked() && p.disposition != analysis::PeakDisposition::Detected)
            continue;
        const int row = m_peaks->rowCount();
        m_peaks->insertRow(row);
        const QStringList values{QString::number(p.timeSeconds, 'f', 3), dispositionText(p.disposition),
                                 QString::number(p.strength, 'g', 5),    QString::number(p.widthSeconds * 1000, 'f', 1),
                                 QString::number(p.bands[0], 'g', 5),    QString::number(p.bands[1], 'g', 5),
                                 QString::number(p.bands[2], 'g', 5)};
        for (int col = 0; col < values.size(); ++col)
        {
            auto *item = new QTableWidgetItem(values[col]);
            item->setToolTip(values[col]);
            if (col == 0)
                item->setData(Qt::UserRole, p.timeSeconds);
            if (p.disposition != analysis::PeakDisposition::Detected)
                item->setForeground(QColor(173, 107, 113));
            m_peaks->setItem(row, col, item);
        }
    }
}
QJsonObject TransientPanel::diagnostics() const
{
    if (!m_result.valid() || !m_spectrum)
        return {};
    QJsonArray frames, peaks;
    for (const auto &f : m_result.frames)
        frames.append(QJsonObject{{"timeSeconds", f.timeSeconds},
                                  {"strength", f.strength},
                                  {"threshold", f.threshold},
                                  {"low", f.bands[0]},
                                  {"mid", f.bands[1]},
                                  {"high", f.bands[2]}});
    for (const auto &p : m_result.peaks)
        peaks.append(QJsonObject{{"timeSeconds", p.timeSeconds},
                                 {"widthSeconds", p.widthSeconds},
                                 {"strength", p.strength},
                                 {"low", p.bands[0]},
                                 {"mid", p.bands[1]},
                                 {"high", p.bands[2]},
                                 {"disposition", dispositionText(p.disposition)}});
    return QJsonObject{{"method", "positive stereo display-band spectral flux"},
                       {"timeline", "whole-file audio seconds"},
                       {"audioStartSeconds", m_spectrum->startSeconds},
                       {"audioDurationSeconds", m_spectrum->durationSeconds},
                       {"sampleRate", m_spectrum->sampleRate},
                       {"fftSize", m_spectrum->fftSize},
                       {"hopSeconds", m_spectrum->hopSeconds},
                       {"relativeThreshold", m_threshold->value()},
                       {"minimumIntervalSeconds", m_interval->value() / 1000},
                       {"frames", frames},
                       {"peaks", peaks}};
}
void TransientPanel::exportResult()
{
    const auto data = diagnostics();
    if (data.isEmpty())
        return;
    const QString path = QFileDialog::getSaveFileName(this, tr("Export transient JSON"), "transient-diagnostics.json",
                                                      tr("JSON (*.json)"));
    if (path.isEmpty())
        return;
    QSaveFile file(path);
    const auto bytes = QJsonDocument(data).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        m_summary->setText(tr("Export failed: %1").arg(file.errorString()));
}
