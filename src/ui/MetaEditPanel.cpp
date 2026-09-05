#include "MetaEditPanel.h"
#include "controller/ChartController.h"
#include "audio/AudioConverter.h"
#include "model/MetaData.h"
#include "utils/Logger.h"
#include <QMessageBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QTimer>
#include <QSizePolicy>

MetaEditPanel::MetaEditPanel(QWidget *parent)
    : RightPanel(parent),
      m_chartController(nullptr),
      m_autoSaveTimer(new QTimer(this)),
      m_isRefreshingUi(false),
      m_hasPendingMetaSave(false)
{
    m_autoSaveTimer->setSingleShot(true);
    m_autoSaveTimer->setInterval(250);
    connect(m_autoSaveTimer, &QTimer::timeout, this, &MetaEditPanel::flushPendingMetaSave);
    setupUi();
}

void MetaEditPanel::setupUi()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    m_formLayout = new QFormLayout;

    m_titleEdit = new QLineEdit(this);
    m_titleLabel = new QLabel(tr("Title:"), this);
    m_formLayout->addRow(m_titleLabel, m_titleEdit);
    m_titleOrgEdit = new QLineEdit(this);
    m_titleOrgLabel = new QLabel(tr("Original Title:"), this);
    m_formLayout->addRow(m_titleOrgLabel, m_titleOrgEdit);
    m_artistEdit = new QLineEdit(this);
    m_artistLabel = new QLabel(tr("Artist:"), this);
    m_formLayout->addRow(m_artistLabel, m_artistEdit);
    m_artistOrgEdit = new QLineEdit(this);
    m_artistOrgLabel = new QLabel(tr("Original Artist:"), this);
    m_formLayout->addRow(m_artistOrgLabel, m_artistOrgEdit);
    m_difficultyEdit = new QLineEdit(this);
    m_difficultyLabel = new QLabel(tr("Difficulty:"), this);
    m_formLayout->addRow(m_difficultyLabel, m_difficultyEdit);
    m_chartAuthorEdit = new QLineEdit(this);
    m_chartAuthorLabel = new QLabel(tr("Chart Author:"), this);
    m_formLayout->addRow(m_chartAuthorLabel, m_chartAuthorEdit);
    m_audioFileEdit = new QLineEdit(this);
    m_audioFileLabel = new QLabel(tr("Audio File:"), this);
    m_formLayout->addRow(m_audioFileLabel, m_audioFileEdit);
    m_audioBrowseBtn = new QPushButton(tr("Browse..."), this);
    QHBoxLayout *audioLayout = new QHBoxLayout;
    audioLayout->addWidget(m_audioFileEdit);
    audioLayout->addWidget(m_audioBrowseBtn);
    m_audioOggLabel = new QLabel(tr("Audio (ogg):"), this);
    m_formLayout->addRow(m_audioOggLabel, audioLayout);
    connect(m_audioBrowseBtn, &QPushButton::clicked, [this]()
            {
        QString fileName = QFileDialog::getOpenFileName(this, tr("Select Audio"), QString(),
            tr("Audio Files (*.ogg *.oga *.mp3 *.wav *.flac *.m4a *.aac *.wma "
               "*.opus *.aif *.aiff *.mka);;All Files (*.*)"));
        if (!fileName.isEmpty()) m_audioFileEdit->setText(fileName); });

    m_backgroundFileEdit = new QLineEdit(this);
    m_bgBrowseBtn = new QPushButton(tr("Browse..."), this);
    QHBoxLayout *bgLayout = new QHBoxLayout;
    bgLayout->addWidget(m_backgroundFileEdit);
    bgLayout->addWidget(m_bgBrowseBtn);
    m_backgroundLabel = new QLabel(tr("Background (jpg):"), this);
    m_formLayout->addRow(m_backgroundLabel, bgLayout);
    connect(m_bgBrowseBtn, &QPushButton::clicked, [this]()
            {
        QString fileName = QFileDialog::getOpenFileName(this, tr("Select Background"), QString(), tr("JPEG Files (*.jpg)"));
        if (fileName.isEmpty())
            return;

        const QString imported = importResourceToChartDirectory(fileName);
        const QString finalName = imported.isEmpty() ? fileName : imported;
        m_backgroundFileEdit->setText(finalName);
        emit backgroundResourceChanged(finalName); });

    m_previewTimeSpin = new QSpinBox(this);
    m_previewTimeSpin->setRange(0, 999999);
    m_previewTimeSpin->setSuffix(tr(" ms"));
    m_previewTimeLabel = new QLabel(tr("Preview Time:"), this);
    m_formLayout->addRow(m_previewTimeLabel, m_previewTimeSpin);

    m_firstBpmSpin = new QDoubleSpinBox(this);
    m_firstBpmSpin->setRange(1, 999);
    m_firstBpmSpin->setDecimals(3);
    m_firstBpmLabel = new QLabel(tr("First BPM:"), this);
    m_formLayout->addRow(m_firstBpmLabel, m_firstBpmSpin);

    m_offsetSpin = new QSpinBox(this);
    m_offsetSpin->setRange(-9999, 9999);
    m_offsetSpin->setSuffix(tr(" ms"));
    m_offsetLabel = new QLabel(tr("Offset:"), this);
    m_formLayout->addRow(m_offsetLabel, m_offsetSpin);

    m_speedSpin = new QSpinBox(this);
    m_speedSpin->setRange(1, 100);
    m_speedLabel = new QLabel(tr("Fall Speed:"), this);
    m_formLayout->addRow(m_speedLabel, m_speedSpin);

    mainLayout->addLayout(m_formLayout);

    m_saveBtn = new QPushButton(tr("Save"), this);
    m_saveBtn->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    connect(m_saveBtn, &QPushButton::clicked, this, &MetaEditPanel::onSaveClicked);
    mainLayout->addWidget(m_saveBtn);
    mainLayout->addStretch();

    const auto connectLineAutoSave = [this](QLineEdit *edit)
    {
        connect(edit, &QLineEdit::textChanged, this, &MetaEditPanel::onMetaFieldChanged);
    };
    connectLineAutoSave(m_titleEdit);
    connectLineAutoSave(m_titleOrgEdit);
    connectLineAutoSave(m_artistEdit);
    connectLineAutoSave(m_artistOrgEdit);
    connectLineAutoSave(m_difficultyEdit);
    connectLineAutoSave(m_chartAuthorEdit);
    connectLineAutoSave(m_audioFileEdit);
    connectLineAutoSave(m_backgroundFileEdit);
    connect(m_previewTimeSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &MetaEditPanel::onMetaFieldChanged);
    connect(m_firstBpmSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MetaEditPanel::onMetaFieldChanged);
    connect(m_offsetSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &MetaEditPanel::onMetaFieldChanged);
    connect(m_speedSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &MetaEditPanel::onMetaFieldChanged);
}

void MetaEditPanel::refreshMeta()
{
    Logger::info("MetaEditPanel::refreshMeta called");
    if (!m_chartController || !m_chartController->chart())
        return;
    m_isRefreshingUi = true;
    const MetaData &meta = m_chartController->chart()->meta();
    Logger::info(QString("MetaEditPanel::refreshMeta - title='%1', artist='%2', difficulty='%3', speed=%4")
                     .arg(meta.title)
                     .arg(meta.artist)
                     .arg(meta.difficulty)
                     .arg(meta.speed));
    m_titleEdit->setText(meta.title);
    m_titleOrgEdit->setText(meta.titleOrg);
    m_artistEdit->setText(meta.artist);
    m_artistOrgEdit->setText(meta.artistOrg);
    m_difficultyEdit->setText(meta.difficulty);
    m_chartAuthorEdit->setText(meta.chartAuthor);
    m_audioFileEdit->setText(meta.audioFile);
    m_backgroundFileEdit->setText(meta.backgroundFile);
    m_previewTimeSpin->setValue(meta.previewTime);
    m_firstBpmSpin->setValue(meta.firstBpm);
    m_offsetSpin->setValue(meta.offset);
    m_speedSpin->setValue(meta.speed);
    m_isRefreshingUi = false;
}

void MetaEditPanel::onSaveClicked()
{
    m_hasPendingMetaSave = false;
    m_autoSaveTimer->stop();
    applyMetaAndPersist(false);
    emit saveRequested();
}

void MetaEditPanel::onMetaFieldChanged()
{
    if (m_isRefreshingUi)
        return;
    m_hasPendingMetaSave = true;
    m_autoSaveTimer->start();
}

void MetaEditPanel::flushPendingMetaSave()
{
    if (!m_hasPendingMetaSave)
        return;
    m_hasPendingMetaSave = false;
    applyMetaAndPersist(false);
}

void MetaEditPanel::setChartController(ChartController *controller)
{
    if (m_chartController)
    {
        disconnect(m_chartController, &ChartController::metaDataChanged, this, &MetaEditPanel::refreshMeta);
        disconnect(m_chartController, &ChartController::chartLoaded, this, &MetaEditPanel::refreshMeta);
    }

    m_chartController = controller;
    if (!m_chartController)
        return;

    connect(m_chartController, &ChartController::metaDataChanged, this, &MetaEditPanel::refreshMeta, Qt::UniqueConnection);
    connect(m_chartController, &ChartController::chartLoaded, this, &MetaEditPanel::refreshMeta, Qt::UniqueConnection);
    refreshMeta();
}

void MetaEditPanel::setSelectionController(SelectionController *controller)
{
    Q_UNUSED(controller);
}

void MetaEditPanel::retranslateUi()
{
    if (m_titleLabel)
        m_titleLabel->setText(tr("Title:"));
    if (m_titleOrgLabel)
        m_titleOrgLabel->setText(tr("Original Title:"));
    if (m_artistLabel)
        m_artistLabel->setText(tr("Artist:"));
    if (m_artistOrgLabel)
        m_artistOrgLabel->setText(tr("Original Artist:"));
    if (m_difficultyLabel)
        m_difficultyLabel->setText(tr("Difficulty:"));
    if (m_chartAuthorLabel)
        m_chartAuthorLabel->setText(tr("Chart Author:"));
    if (m_audioFileLabel)
        m_audioFileLabel->setText(tr("Audio File:"));
    if (m_audioOggLabel)
        m_audioOggLabel->setText(tr("Audio (ogg):"));
    if (m_backgroundLabel)
        m_backgroundLabel->setText(tr("Background (jpg):"));
    if (m_previewTimeLabel)
        m_previewTimeLabel->setText(tr("Preview Time:"));
    if (m_firstBpmLabel)
        m_firstBpmLabel->setText(tr("First BPM:"));
    if (m_offsetLabel)
        m_offsetLabel->setText(tr("Offset:"));
    if (m_speedLabel)
        m_speedLabel->setText(tr("Fall Speed:"));
    if (m_audioBrowseBtn)
        m_audioBrowseBtn->setText(tr("Browse..."));
    if (m_bgBrowseBtn)
        m_bgBrowseBtn->setText(tr("Browse..."));
    if (m_saveBtn)
        m_saveBtn->setText(tr("Save"));
    if (m_previewTimeSpin)
        m_previewTimeSpin->setSuffix(tr(" ms"));
    if (m_offsetSpin)
        m_offsetSpin->setSuffix(tr(" ms"));
}

QString MetaEditPanel::importResourceToChartDirectory(const QString &sourcePath) const
{
    if (!m_chartController)
        return QString();

    const QString chartPath = m_chartController->chartFilePath();
    if (chartPath.isEmpty())
        return QString();

    const QFileInfo sourceInfo(sourcePath);
    if (!sourceInfo.exists() || !sourceInfo.isFile())
        return QString();

    const QDir chartDir(QFileInfo(chartPath).absolutePath());
    if (!chartDir.exists())
        return QString();

    const QString sourceAbs = sourceInfo.absoluteFilePath();
    const QString existingRel = chartDir.relativeFilePath(sourceAbs);
    if (!existingRel.startsWith("..") && !QDir::isAbsolutePath(existingRel))
        return existingRel;

    const QString baseName = sourceInfo.completeBaseName().isEmpty()
                                 ? QStringLiteral("background")
                                 : sourceInfo.completeBaseName();
    const QString suffix = sourceInfo.suffix().isEmpty()
                               ? QStringLiteral("jpg")
                               : sourceInfo.suffix();

    QString fileName = baseName + "." + suffix;
    QString targetPath = chartDir.filePath(fileName);
    for (int i = 2; QFileInfo::exists(targetPath); ++i)
    {
        fileName = QString("%1_%2.%3").arg(baseName).arg(i).arg(suffix);
        targetPath = chartDir.filePath(fileName);
    }

    if (!QFile::copy(sourceAbs, targetPath))
    {
        Logger::warn(QString("Failed to copy background image into chart directory: %1 -> %2")
                         .arg(sourceAbs, targetPath));
        return QString();
    }

    return fileName;
}

MetaData MetaEditPanel::collectMetaFromUi() const
{
    MetaData meta;
    meta.title = m_titleEdit->text();
    meta.titleOrg = m_titleOrgEdit->text();
    meta.artist = m_artistEdit->text();
    meta.artistOrg = m_artistOrgEdit->text();
    meta.difficulty = m_difficultyEdit->text();
    meta.chartAuthor = m_chartAuthorEdit->text();
    meta.audioFile = m_audioFileEdit->text();
    meta.backgroundFile = m_backgroundFileEdit->text();
    meta.previewTime = m_previewTimeSpin->value();
    meta.firstBpm = m_firstBpmSpin->value();
    meta.offset = m_offsetSpin->value();
    meta.speed = m_speedSpin->value();
    return meta;
}

bool MetaEditPanel::isSameMeta(const MetaData &a, const MetaData &b) const
{
    return a.title == b.title &&
           a.titleOrg == b.titleOrg &&
           a.artist == b.artist &&
           a.artistOrg == b.artistOrg &&
           a.difficulty == b.difficulty &&
           a.chartAuthor == b.chartAuthor &&
           a.audioFile == b.audioFile &&
           a.backgroundFile == b.backgroundFile &&
           a.previewTime == b.previewTime &&
           qFuzzyCompare(a.firstBpm + 1.0, b.firstBpm + 1.0) &&
           a.offset == b.offset &&
           a.speed == b.speed;
}

bool MetaEditPanel::applyMetaAndPersist(bool persistToDisk)
{
    if (!m_chartController || !m_chartController->chart())
        return false;

    MetaData next = collectMetaFromUi();
    const MetaData current = m_chartController->chart()->meta();

    bool bgChanged = false;
    bool audioChanged = false;

    // Import background file to chart directory if absolute path.
    if (!next.backgroundFile.trimmed().isEmpty() && QDir::isAbsolutePath(next.backgroundFile))
    {
        const QString imported = importResourceToChartDirectory(next.backgroundFile);
        if (!imported.isEmpty())
        {
            next.backgroundFile = imported;
            if (m_backgroundFileEdit->text() != imported)
            {
                m_isRefreshingUi = true;
                m_backgroundFileEdit->setText(imported);
                m_isRefreshingUi = false;
            }
        }
    }
    if (next.backgroundFile != current.backgroundFile)
        bgChanged = true;

    // Import audio file to chart directory if absolute path.
    if (!next.audioFile.trimmed().isEmpty() && QDir::isAbsolutePath(next.audioFile))
    {
        QString importSource = next.audioFile;

        // Malody charts can only reference OGG music: convert non-OGG audio
        // into the chart directory first, then reuse the normal import flow.
        if (!AudioConverter::isOggFile(importSource))
        {
            const QString chartPath = m_chartController ? m_chartController->chartFilePath()
                                                        : QString();
            const QDir chartDir(QFileInfo(chartPath).absolutePath());
            if (chartPath.isEmpty() || !chartDir.exists())
            {
                QMessageBox::critical(this, tr("Error"),
                                      tr("Cannot convert audio: chart directory is not available."));
                return false;
            }

            const QFileInfo sourceInfo(importSource);
            const QString baseName = sourceInfo.completeBaseName().isEmpty()
                                         ? QStringLiteral("audio")
                                         : sourceInfo.completeBaseName();
            QString fileName = baseName + QStringLiteral(".ogg");
            QString targetPath = chartDir.filePath(fileName);
            for (int i = 2; QFileInfo::exists(targetPath); ++i)
            {
                fileName = QStringLiteral("%1_%2.ogg").arg(baseName).arg(i);
                targetPath = chartDir.filePath(fileName);
            }

            QString convertError;
            if (AudioConverter::convertToOggWithProgress(this, importSource, targetPath,
                                                         &convertError)
                    .isEmpty())
            {
                QMessageBox::critical(this, tr("Error"),
                                      tr("Failed to convert audio to OGG:\n%1").arg(convertError));
                return false;
            }
            importSource = targetPath;
        }

        const QString imported = importResourceToChartDirectory(importSource);
        if (!imported.isEmpty())
        {
            next.audioFile = imported;
            if (m_audioFileEdit->text() != imported)
            {
                m_isRefreshingUi = true;
                m_audioFileEdit->setText(imported);
                m_isRefreshingUi = false;
            }
        }
    }
    if (next.audioFile != current.audioFile)
        audioChanged = true;

    if (!isSameMeta(current, next))
        m_chartController->setMetaData(next);

    if (!persistToDisk)
        return true;

    const QString path = m_chartController->chartFilePath();
    if (path.isEmpty())
        return false;
    const bool saved = m_chartController->saveChart(path);

    // Emit resource change signals after save so MainWindow can reload.
    if (bgChanged)
        emit backgroundResourceChanged(next.backgroundFile);
    if (audioChanged)
        emit audioFileChanged(next.audioFile);

    return saved;
}
