#include "render/HyperfruitDetector.h"
#include "app/MainWindow.h"
#include "audio/AudioPlayer.h"
#include "controller/ChartController.h"
#include "controller/SelectionController.h"
#include "controller/PlaybackController.h"
#include "model/Skin.h"
#include "ui/CustomWidgets/ChartCanvas/ChartCanvas.h"
#include "ui/CustomWidgets/RealtimePreviewWidget.h"
#include "utils/Settings.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QImage>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTranslator>

class HyperfruitTests : public QObject
{
    Q_OBJECT
private slots:
    void malodyJudgeBoundaries()
    {
        // At 600 BPM one beat is 100ms. Each pair straddles the engine's
        // 1/4-frame hyperdash boundary for its Malody judge, on a 512 field.
        const int lastReachableX[] = {155, 148, 143, 139, 136};
        const QVector<BpmEntry> timing{BpmEntry(0, 0, 1, 600)};
        for (int level = 0; level < 5; ++level)
        {
            HyperfruitDetector detector;
            detector.setJudgeLevel(level);
            QVERIFY(detector.detect({Note(0, 0, 1, 0), Note(1, 0, 1, lastReachableX[level])}, timing, 0).isEmpty());
            QCOMPARE(detector.detect({Note(0, 0, 1, 0), Note(1, 0, 1, lastReachableX[level] + 1)}, timing, 0), QSet<int>{0});
        }
        HyperfruitDetector detector;
        const QVector<Note> notes{Note(0, 0, 1, 0), Note(1, 0, 1, 135)};
        QVERIFY(detector.detect(notes, timing, 0).isEmpty()); // default E
        detector.setJudgeLevel(4, true);
        QCOMPARE(detector.detect(notes, timing, 0), QSet<int>{0});
        detector.setJudgeLevel(-1);
        QVERIFY(detector.detect(notes, timing, 0).isEmpty());
    }

    void fractionalTimesAndGracePeriod()
    {
        HyperfruitDetector detector;
        const QVector<BpmEntry> timing{BpmEntry(0, 0, 1, 60000.0 / 100.7)};
        const QVector<Note> notes{Note(1, 0, 1, 0), Note(2, 0, 1, 137)};
        // Truncating each timestamp (as osu! does) incorrectly makes this reachable.
        QCOMPARE(detector.detect(notes, timing, 0), QSet<int>{0});
        QCOMPARE(detector.detect(notes, timing, 101), QSet<int>{0});
        QVERIFY(detector.detect({Note(0, 0, 1, 0), Note(0, 1, 100, 512)},
                                {BpmEntry(0, 0, 1, 600)}, 0).isEmpty());
    }

    void chordsRainAndDirectionChanges()
    {
        HyperfruitDetector detector;
        const QVector<BpmEntry> timing{BpmEntry(0, 0, 1, 600)};
        const QSet<int> both{0, 1};
        QCOMPARE(detector.detect({Note(0, 0, 1, 0), Note(0, 0, 1, 1), Note(1, 0, 1, 200)}, timing, 0), both);
        QCOMPARE(detector.detect({Note(0, 0, 1, 0), Note(1, 0, 1, 140), Note(2, 0, 1, 0)}, timing, 0), both);
        QCOMPARE(detector.detect({Note(0, 0, 1, 0), Note(0, 1, 2, 2, 0, 1, 256), Note(1, 0, 1, 200)}, timing, 0), QSet<int>{0});
        QVERIFY(detector.detect({}, timing, 0).isEmpty());
        QVERIFY(detector.detect({Note(0, 0, 1, 0)}, {}, 0).isEmpty());
    }

    void settingsAndDialog()
    {
        auto &settings = Settings::instance();
        QCOMPARE(settings.hyperfruitJudgeLevel(), 4);
        QVERIFY(!settings.hyperfruitProJudge());
        settings.setNoteSoundVolume(0);
        ChartController chart;
        SelectionController selection;
        AudioPlayer audio;
        PlaybackController playback(&audio);
        MainWindow window(&chart, &selection, &playback, new Skin);
        auto *action = window.findChild<QAction *>("hyperfruitJudgeAction");
        QVERIFY(action);
        QTimer::singleShot(0, &window, [&]() {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            auto *judge = dialog->findChild<QComboBox *>("hyperfruitJudgeLevel");
            auto *pro = dialog->findChild<QCheckBox *>("hyperfruitProJudge");
            QVERIFY(judge && pro);
            QCOMPARE(judge->currentIndex(), 4);
            if (qEnvironmentVariableIsSet("CCE_HYPERFRUIT_SCREENSHOT"))
                dialog->grab().save(qEnvironmentVariable("CCE_HYPERFRUIT_SCREENSHOT"));
            judge->setCurrentIndex(1);
            pro->setChecked(true);
            dialog->accept();
        });
        action->trigger();
        QCOMPARE(settings.hyperfruitJudgeLevel(), 1);
        QVERIFY(settings.hyperfruitProJudge());
        QTimer::singleShot(0, &window, []() {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            QCOMPARE(dialog->findChild<QComboBox *>("hyperfruitJudgeLevel")->currentIndex(), 1);
            dialog->findChild<QComboBox *>("hyperfruitJudgeLevel")->setCurrentIndex(0);
            dialog->reject();
        });
        action->trigger();
        QCOMPARE(settings.hyperfruitJudgeLevel(), 1);
        settings.setHyperfruitJudgeLevel(99);
        QCOMPARE(settings.hyperfruitJudgeLevel(), 4);
        settings.setHyperfruitProJudge(false);
        window.hide();
        QApplication::processEvents();
    }

    void bothViewsRefreshCachedOutlines()
    {
        auto &settings = Settings::instance();
        settings.setHyperfruitJudgeLevel(4);
        settings.setHyperfruitProJudge(false);
        Chart data;
        data.bpmList().clear();
        data.addBpm(BpmEntry(0, 0, 1, 600));
        data.addNotes({Note(1, 0, 1, 100), Note(2, 0, 1, 240)});
        ChartController chart;
        chart.loadChartFromData({}, data);
        ChartCanvas canvas;
        canvas.setChartController(&chart);
        canvas.setColorMode(false);
        canvas.resize(700, 600);
        RealtimePreviewWidget preview;
        preview.setChartController(&chart);
        preview.resize(220, 600);
        canvas.show();
        preview.show();
        QApplication::processEvents();
        const auto redPixels = [](const QImage &image) {
            int count = 0;
            for (int y = 0; y < image.height(); ++y)
                for (int x = 0; x < image.width(); ++x)
                {
                    const QColor c = image.pixelColor(x, y);
                    if (c.red() > 200 && c.green() < 80 && c.blue() < 80)
                        ++count;
                }
            return count;
        };
        const int canvasE = redPixels(canvas.grab().toImage());
        const int previewE = redPixels(preview.grab().toImage());
        settings.setHyperfruitJudgeLevel(0);
        canvas.refreshRenderSettings();
        preview.refreshRenderSettings();
        const int canvasA = redPixels(canvas.grab().toImage());
        const int previewA = redPixels(preview.grab().toImage());
        QVERIFY2(canvasE > canvasA, "Canvas must recompute its red-fruit cache when judge changes");
        QVERIFY2(previewE > previewA, "Preview must recompute its red-fruit cache when judge changes");
        settings.setHyperfruitJudgeLevel(4);
        canvas.hide();
        preview.hide();
        QApplication::processEvents();
    }
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName("HyperfruitRegression");
    app.setQuitOnLastWindowClosed(false);
    QStandardPaths::setTestModeEnabled(true);
    QTemporaryDir settingsDir;
    if (!settingsDir.isValid())
        return 1;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    QTranslator translator;
    if (qEnvironmentVariableIsSet("CCE_HYPERFRUIT_TRANSLATION"))
    {
        if (!translator.load(qEnvironmentVariable("CCE_HYPERFRUIT_TRANSLATION")))
            return 2;
        app.installTranslator(&translator);
    }
    HyperfruitTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "hyperfruit_tests.moc"
