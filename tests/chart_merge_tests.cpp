#include <QtTest>
#include "merge/ChartMerger.h"
#include "merge/MergeAudio.h"
#include "merge/MergeExport.h"
#include "audio/AudioConverter.h"
#include "file/ProjectIO.h"
#include "utils/MathUtils.h"
#include "ui/dialogs/ChartMergeDialog.h"
#include "app/MainWindow.h"
#include "controller/ChartController.h"
#include "controller/SelectionController.h"
#include "controller/PlaybackController.h"
#include "review/ReviewWorkspace.h"
#include "model/Skin.h"
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QImage>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTranslator>
#include <cmath>

namespace
{
QVector<float> sine(double duration, double frequency = 440)
{
    QVector<float> samples(qRound(duration*MergeAudio::SampleRate)*2);
    for (int i = 0; i < samples.size()/2; ++i)
        samples[2*i] = samples[2*i+1] = float(0.3*std::sin(i*frequency*2*3.141592653589793/MergeAudio::SampleRate));
    return samples;
}
QJsonObject fixture(const QString &audio)
{
    return {{"meta",QJsonObject{{"mode",3},{"song",QJsonObject{{"title","Test"},{"artist","Artist"}}},
                              {"version","Cup"},{"creator","Author"},{"audio",audio},{"offset",100}}},
            {"time",QJsonArray{QJsonObject{{"beat",QJsonArray{0,0,1}},{"bpm",120}},
                               QJsonObject{{"beat",QJsonArray{2,0,1}},{"bpm",180},{"delay",80}}}},
            {"note",QJsonArray{QJsonObject{{"beat",QJsonArray{0,0,1}},{"type",1},{"sound",audio},{"offset",100}},
                               QJsonObject{{"beat",QJsonArray{0,1,2}},{"x",64},{"custom","keep"}},
                               QJsonObject{{"beat",QJsonArray{1,1,3}},{"x",128}},
                               QJsonObject{{"beat",QJsonArray{2,0,1}},{"endbeat",QJsonArray{4,0,1}},{"type",3},{"x",400}}}},
            {"effect",QJsonArray{QJsonObject{{"beat",QJsonArray{1,0,1}},{"type",7},{"value",2}}}}};
}
bool writeJson(const QString &path, const QJsonObject &object)
{
    QFile f(path); if (!f.open(QIODevice::WriteOnly)) return false;
    return f.write(QJsonDocument(object).toJson()) > 0;
}
bool prepare(QTemporaryDir &dir, ChartMerger::Source &source, QJsonObject object = {})
{
    QString error;
    if (!AudioConverter::encodePcmToOgg(sine(3),2,MergeAudio::SampleRate,dir.filePath("song.ogg"),&error)) return false;
    if (object.isEmpty()) object = fixture("song.ogg");
    return writeJson(dir.filePath("test.mc"),object) && ChartMerger::load(dir.filePath("test.mc"),source,&error);
}
double frequency(const QVector<float> &pcm)
{
    const int start = pcm.size()/8, end = pcm.size()/2-pcm.size()/8;
    int crosses = 0;
    for (int i = start+1; i < end; ++i) if (pcm[2*(i-1)] <= 0 && pcm[2*i] > 0) ++crosses;
    return double(crosses)*MergeAudio::SampleRate/(end-start);
}
}

class ChartMergeTests : public QObject
{
    Q_OBJECT
    QTranslator m_translation;
private slots:
    void initTestCase()
    {
        if (m_translation.load(qEnvironmentVariable("CCE_MERGE_UI_TRANSLATION")))
            qApp->installTranslator(&m_translation);
    }
    void sourceParsingIsReadOnly()
    {
        QTemporaryDir dir; ChartMerger::Source src; QVERIFY(prepare(dir,src));
        QFile chart(src.path); QVERIFY(chart.open(QIODevice::ReadOnly)); const auto original = chart.readAll(); chart.close();
        QCOMPARE(src.normalCount,2); QCOMPARE(src.rainCount,1); QCOMPARE(src.soundCount,1);
        QCOMPARE(src.minBpm,120.0); QCOMPARE(src.maxBpm,180.0); QVERIFY(std::abs(src.firstSeconds-0.15) < 1e-9);
        QCOMPARE(src.lastBeat,4.0); QVERIFY(std::abs(src.lastSeconds-(1+0.08+2.0/3-0.1)) < 1e-9);
        QString error; QVERIFY2(ChartMerger::load(src.path,src,&error),qPrintable(error));
        QVERIFY(chart.open(QIODevice::ReadOnly)); QCOMPARE(chart.readAll(),original); QVERIFY(QFile::exists(dir.filePath("song.ogg")));
        auto invalid = fixture("song.ogg"); auto meta = invalid["meta"].toObject(); meta["mode"] = 0; invalid["meta"] = meta;
        QVERIFY(writeJson(dir.filePath("other.mc"),invalid)); QVERIFY(!ChartMerger::load(dir.filePath("other.mc"),src,&error));
        invalid = fixture("song.ogg"); invalid["time"] = QJsonArray{QJsonObject{{"beat",QJsonArray{0,0,0}},{"bpm",120}}};
        QVERIFY(writeJson(dir.filePath("bad.mc"),invalid)); QVERIFY(!ChartMerger::load(dir.filePath("bad.mc"),src,&error));
    }
    void placementPreservesTiming_data()
    {
        QTest::addColumn<double>("gap"); QTest::newRow("normal") << 2.75; QTest::newRow("zero") << 0.0;
    }
    void placementPreservesTiming()
    {
        QFETCH(double,gap);
        QTemporaryDir dir; ChartMerger::Source src; QVERIFY(prepare(dir,src));
        ChartMerger::Segment a,b; a.source = src; b.source = src; a.rate = 1.5; b.rate = 0.75; a.gapAfter = gap;
        ChartMerger::Plan plan; QString error;
        QVERIFY2(ChartMerger::plan({a,b},"Mix","Artist","Creator","Mix",plan,&error),qPrintable(error));
        QCOMPARE(plan.placements.size(),2); QCOMPARE(plan.segmentNotes.size(),2);
        QVERIFY(std::abs(plan.placements[1].firstSeconds-plan.placements[0].lastSeconds-gap) < 1e-9);
        const QVector<ChartMerger::Segment> segments{a,b};
        for (int i = 0; i < 2; ++i)
        {
            int n = 0;
            for (const auto &v : src.json["note"].toArray())
            {
                const auto original = v.toObject(); if (original.contains("sound")) continue;
                const auto merged = plan.segmentNotes[i][n++].toObject();
                QCOMPARE(merged["x"],original["x"]); QCOMPARE(merged["custom"],original["custom"]);
                for (const auto &key : {QStringLiteral("beat"),QStringLiteral("endbeat")})
                {
                    if (!original.contains(key)) continue;
                    const double expected = (ChartMerger::secondsAt(src.json,original[key].toArray())-src.audioOrigin)/segments[i].rate+plan.placements[i].audioStart;
                    const double actual = ChartMerger::secondsAt(plan.chart,merged[key].toArray());
                    QVERIFY2(std::abs(expected-actual) < 1e-8,qPrintable(QString("%1 vs %2").arg(expected,0,'f',12).arg(actual,0,'f',12)));
                }
            }
        }
        QCOMPARE(plan.chart["effect"].toArray().size(),2);
        QCOMPARE(plan.chart["note"].toArray().size(),7); // one BGM, six playable notes
        a.rate = 0; QVERIFY(!ChartMerger::plan({a},"","","","",plan,&error));
    }
    void pitchAndSpeed_data()
    {
        QTest::addColumn<double>("rate"); QTest::addColumn<bool>("pitch");
        for (double rate : {0.5, 0.75, 1.5, 2.0, 4.0})
            for (bool pitch : {false,true}) QTest::newRow(qPrintable(QString::number(rate)+(pitch ? "shift" : "preserve"))) << rate << pitch;
    }
    void pitchAndSpeed()
    {
        QFETCH(double,rate); QFETCH(bool,pitch);
        const auto input = sine(1.5);
        const auto result = MergeAudio::changeSpeed(input,rate,pitch);
        QCOMPARE(result.size()/2,qRound64(input.size()/2/rate));
        const double target = pitch ? 440*rate : 440;
        QVERIFY2(std::abs(frequency(result)-target) < 18,qPrintable(QString("Measured %1, expected %2").arg(frequency(result)).arg(target)));
        for (int i = 0; i < result.size()/2; ++i) QCOMPARE(result[2*i],result[2*i+1]);
    }
    void exportAndLayeredPreview()
    {
        QTemporaryDir dir,output,unpacked; ChartMerger::Source src; QVERIFY(prepare(dir,src));
        ChartMerger::Segment a,b; a.source = src; b.source = src; a.gapAfter = 0.4; b.rate = 1.5;
        ChartMerger::Plan plan; QString error;
        QVERIFY(ChartMerger::plan({a,b},"Merged","Test","CCE","Mix",plan,&error));
        QImage image(8,8,QImage::Format_RGB32); image.fill(Qt::darkCyan);
        const auto imagePath = dir.filePath("source.png"); QVERIFY(image.save(imagePath));
        QVERIFY2(MergeExport::write({a,b},plan,output.path(),imagePath,imagePath,&error),qPrintable(error));
        QVERIFY(AudioConverter::isOggFile(output.filePath("merged.ogg")));
        CceDocument::ProjectBundle bundle;
        QVERIFY2(CceDocument::readProject(output.filePath("merged.cceproj"),bundle,&error),qPrintable(error));
        QCOMPARE(bundle.project.layers.size(),2);
        const auto composed = bundle.project.compose(bundle.project.activeDifficultyId);
        QCOMPARE(composed.notes().size(),7);
        QVERIFY(bundle.resourcePaths.contains("merged.ogg"));
        const auto cache = unpacked.filePath("cache");
        QVERIFY(CceDocument::materializeResources(bundle,cache,&error));
        const auto preview = bundle.project.compose(bundle.project.activeDifficultyId);
        const auto cacheTimes = MathUtils::buildBpmTimeCache(preview.bpmList(),preview.meta().offset);
        double last = 0;
        for (const auto &n : preview.notes())
            if (!n.isSound()) last = std::max(last,MathUtils::beatToMs(n.endBeatNum,n.endNumerator,n.endDenominator,cacheTimes)/1000);
        // Rain's end is retained and delay charts get a time-equivalent preview.
        QVERIFY(std::abs(last-plan.placements.last().lastSeconds) < 0.001);
        ProjectIO::initializeBuiltinFileTypes();
        const auto package = dir.filePath("merged.mcz");
        QVERIFY(ProjectIO::exportToMczPure(package,output.filePath("merged.mc")));
        QString extracted; QVERIFY(ProjectIO::extractMcz(package,unpacked.filePath("package"),extracted));
        const auto charts = ProjectIO::findChartsInDirectory(extracted); QCOMPARE(charts.size(),1);
        const auto packageDir = QFileInfo(charts.first().first).dir();
        QVERIFY(packageDir.exists("merged.ogg")); QVERIFY(packageDir.exists("background.png")); QVERIFY(packageDir.exists("cover.png"));
        QVector<float> decoded; QVERIFY2(MergeAudio::decode(output.filePath("merged.ogg"),decoded,&error),qPrintable(error));
        QVERIFY(decoded.size()/2.0/MergeAudio::SampleRate >= plan.placements.last().lastSeconds);
    }
    void silenceAndFades()
    {
        QTemporaryDir dir,output; ChartMerger::Source src; QVERIFY(prepare(dir,src));
        ChartMerger::Segment a,b; a.source = src; b.source = src; a.gapAfter = 4;
        ChartMerger::Plan plan; QString error;
        QVERIFY(ChartMerger::plan({a,b},"","","","Mix",plan,&error));
        QVERIFY2(MergeAudio::render({a,b},plan,output.filePath("mix.ogg"),&error),qPrintable(error));
        QVector<float> pcm; QVERIFY2(MergeAudio::decode(output.filePath("mix.ogg"),pcm,&error),qPrintable(error));
        const auto rms = [&](double start,double end) {
            double sum = 0; const int first = qRound(start*MergeAudio::SampleRate), last = qRound(end*MergeAudio::SampleRate);
            for (int i = first; i < last; ++i) sum += pcm[i*2]*pcm[i*2];
            return std::sqrt(sum/(last-first));
        };
        QVERIFY(rms(3.5,4.5) < 0.0001);
        QVERIFY(rms(2.9,2.95) < rms(0.7,0.75)*0.2);
        QVERIFY(rms(0.02,0.04) < rms(0.1,0.12)*0.5);
    }
    void cancellationDoesNotPublishAudio()
    {
        QTemporaryDir dir; QString error;
        const auto path = dir.filePath("cancel.ogg");
        QVERIFY(!AudioConverter::encodePcmToOgg(sine(1),2,MergeAudio::SampleRate,path,&error,[](float) { return false; }));
        QVERIFY(!QFile::exists(path));
        QFile existing(path); QVERIFY(existing.open(QIODevice::WriteOnly)); existing.write("keep"); existing.close();
        QVERIFY(!AudioConverter::encodePcmToOgg(sine(1),2,MergeAudio::SampleRate,path,&error,[](float) { return false; }));
        QVERIFY(existing.open(QIODevice::ReadOnly)); QCOMPARE(existing.readAll(),QByteArray("keep"));
    }
    void zeroGapWithoutDelayKeepsValidSubdivisions()
    {
        QTemporaryDir dir,output; ChartMerger::Source src;
        auto json = fixture("song.ogg");
        json["time"] = QJsonArray{QJsonObject{{"beat",QJsonArray{0,0,1}},{"bpm",120}}};
        QVERIFY(prepare(dir,src,json));
        ChartMerger::Segment a; a.source = src; a.gapAfter = 0;
        ChartMerger::Plan plan; QString error;
        QVERIFY(ChartMerger::plan({a,a},"","","","",plan,&error));
        for (const auto &part : plan.segmentNotes)
            for (const auto &v : part)
                QVERIFY(v.toObject()["beat"].toArray()[1].toInt() >= 0);
        QVERIFY(AudioConverter::encodePcmToOgg(sine(1),2,MergeAudio::SampleRate,output.filePath("merged.ogg"),&error));
        CceDocument::ProjectBundle bundle;
        QVERIFY2(MergeExport::previewBundle({a,a},plan,output.path(),bundle,&error),qPrintable(error));
        QCOMPARE(bundle.project.layers.size(),2);
        QVERIFY(bundle.project.validate(&error));
    }
    void windowRetainsSettingsAfterPreview()
    {
        QTemporaryDir dir; ChartMerger::Source src; QVERIFY(prepare(dir,src));
        ChartMergeDialog dialog(dir.path()); dialog.show(); dialog.addPaths({src.path});
        if (!qEnvironmentVariableIsEmpty("CCE_MERGE_UI_CAPTURE"))
        {
            const int font = QFontDatabase::addApplicationFont("C:/Windows/Fonts/msyh.ttc");
            const auto families = QFontDatabase::applicationFontFamilies(font);
            if (!families.isEmpty()) dialog.setFont(QFont(families.first(),10));
        }
        auto *library = dialog.findChild<QTableWidget*>("mergeLibrary");
        auto *queue = dialog.findChild<QTableWidget*>("mergeQueue");
        auto *render = dialog.findChild<QPushButton*>("mergeRender");
        auto *preview = dialog.findChild<QPushButton*>("mergePreview");
        QTRY_COMPARE_WITH_TIMEOUT(library->rowCount(),1,15000);
        QTRY_VERIFY(library->isEnabled()); library->selectRow(0);
        QTest::mouseClick(dialog.findChild<QPushButton*>("mergeAddSelected"),Qt::LeftButton);
        QTRY_COMPARE(queue->rowCount(),1);
        auto *rate = dialog.findChild<QDoubleSpinBox*>("mergeRate"); rate->setValue(1.5);
        QSignalSpy spy(&dialog,&ChartMergeDialog::previewRequested);
        QTest::mouseClick(render,Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(preview->isEnabled(),30000);
        QTest::mouseClick(preview,Qt::LeftButton); QCOMPARE(spy.size(),1);
        QVERIFY(dialog.isVisible()); QCOMPARE(queue->rowCount(),1); QCOMPARE(rate->value(),1.5);
        QTest::mouseClick(preview,Qt::LeftButton); QCOMPARE(spy.size(),2);
        const auto screenshot = qEnvironmentVariable("CCE_MERGE_UI_CAPTURE");
        if (!screenshot.isEmpty()) QVERIFY(dialog.grab().save(screenshot));
        dialog.close(); dialog.show(); QCOMPARE(rate->value(),1.5); QCOMPARE(queue->rowCount(),1);
    }
    void previewImportsIntoMainWindowAndSurvivesWorkspaceRemoval()
    {
        QTemporaryDir dir,output; ChartMerger::Source src; QVERIFY(prepare(dir,src));
        ChartMerger::Segment a; a.source = src;
        ChartMerger::Plan plan; QString error;
        QVERIFY(ChartMerger::plan({a,a},"Imported Mix","Test","CCE","Mix",plan,&error));
        QVERIFY(AudioConverter::encodePcmToOgg(sine(1),2,MergeAudio::SampleRate,output.filePath("merged.ogg"),&error));
        CceDocument::ProjectBundle bundle;
        QVERIFY(MergeExport::previewBundle({a,a},plan,output.path(),bundle,&error));
        const auto path = output.filePath("merged.cceproj");
        QVERIFY(CceDocument::writeProject(path,bundle,&error));
        ChartController chart; SelectionController selection; AudioPlayer audio; PlaybackController playback(&audio);
        MainWindow window(&chart,&selection,&playback,new Skin); window.show();
        QVERIFY(QMetaObject::invokeMethod(&window,"mergeCharts",Qt::DirectConnection));
        auto *dialog = window.findChild<ChartMergeDialog*>(); QVERIFY(dialog);
        auto *review = window.findChild<CceReview::ReviewWorkspace*>(); QVERIFY(review);
        QSignalSpy opened(review,&CceReview::ReviewWorkspace::projectOpened);
        dialog->previewRequested(path);
        QCOMPARE(opened.size(),1); QVERIFY(review->projectMode());
        QCOMPARE(review->project().layers.size(),2); QCOMPARE(chart.chart()->meta().title,QString("Imported Mix"));
        const auto imported = opened.first().first().toString();
        QVERIFY(imported != path); QVERIFY(QFile::exists(imported));
        const auto audioPath = chart.chart()->audioSourceFullPath(); QVERIFY(QFile::exists(audioPath));
        QVERIFY(dialog->isVisible()); output.remove();
        QVERIFY(QFile::exists(audioPath)); QVERIFY(QFile::exists(imported));
        QVERIFY2(review->saveProject(imported,&error),qPrintable(error));
        QVERIFY(QMetaObject::invokeMethod(&window,"mergeCharts",Qt::DirectConnection));
        QCOMPARE(window.findChildren<ChartMergeDialog*>().size(),1);
    }
};
int main(int argc, char **argv)
{
    QApplication app(argc,argv);
    app.setApplicationName("ChartMergeRegression"); app.setQuitOnLastWindowClosed(false);
    QStandardPaths::setTestModeEnabled(true);
    QTemporaryDir settings;
    if (!settings.isValid()) return 1;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    ChartMergeTests tests;
    return QTest::qExec(&tests,argc,argv);
}
#include "chart_merge_tests.moc"
