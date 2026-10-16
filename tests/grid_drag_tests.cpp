#include "controller/ChartController.h"
#include "controller/SelectionController.h"
#include "ui/CustomWidgets/ChartCanvas/ChartCanvas.h"
#include "ui/NoteEditPanel.h"
#include "utils/Settings.h"

#include <QCheckBox>
#include <QDialog>
#include <QFontDatabase>
#include <QMouseEvent>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <memory>

class GridDragTests : public QObject
{
    Q_OBJECT
    QTemporaryDir m_settings;
    ChartController m_charts;
    SelectionController m_selection;
    std::unique_ptr<ChartCanvas> m_canvas;

private slots:
    void initTestCase()
    {
        QVERIFY(m_settings.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
        QVERIFY(!Settings::instance().editorDragGridSnapEnabled());
        m_canvas = std::make_unique<ChartCanvas>();
        m_canvas->setChartController(&m_charts);
        m_canvas->setSelectionController(&m_selection);
        m_canvas->resize(640, 480);
        m_canvas->show();
        QTest::qWait(20);
    }

    void dragPreferenceIsIndependent_data()
    {
        QTest::addColumn<bool>("placementSnap");
        QTest::addColumn<bool>("dragSnap");
        QTest::addColumn<bool>("group");
        for (bool placement : {false, true})
            for (bool drag : {false, true})
                for (bool group : {false, true})
                    QTest::newRow(qPrintable(QString("place%1-drag%2-group%3").arg(placement).arg(drag).arg(group)))
                        << placement << drag << group;
    }

    void dragPreferenceIsIndependent()
    {
        QFETCH(bool, placementSnap);
        QFETCH(bool, dragSnap);
        QFETCH(bool, group);
        Chart chart;
        chart.addNote(Note(4, 0, 1, 129));
        chart.addNote(Note(6, 0, 1, 170));
        const auto originals = chart.notes();
        ChartController &charts = m_charts;
        QVERIFY(charts.loadChartFromData(m_settings.filePath("chart.mc"), chart));
        SelectionController &selection = m_selection;
        selection.setNotes(&charts.chart()->notes(), charts.revision());
        selection.clearSelection();
        ChartCanvas &canvas = *m_canvas;
        canvas.setVerticalFlip(false);
        canvas.setMode(ChartCanvas::Select);
        canvas.setGridDivision(16);
        canvas.setGridSnap(placementSnap);
        canvas.setDragGridSnap(dragSnap);
        canvas.resize(640, 480);
        canvas.show();
        QTest::qWait(20);
        selection.select(0);
        if (group)
            selection.addToSelection(1);

        const QPoint start(qRound(canvas.chartLaneXToCanvasX(129)), qRound(canvas.chartBeatToY(4)));
        const QPoint end = start + QPoint(27, 0);
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(start), QPointF(canvas.mapToGlobal(start)),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&canvas, &press);
        QMouseEvent move(QEvent::MouseMove, QPointF(end), QPointF(canvas.mapToGlobal(end)),
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&canvas, &move);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(end), QPointF(canvas.mapToGlobal(end)),
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(&canvas, &release);
        const auto moved = charts.chart()->notes();
        QVERIFY(charts.canUndo());
        if (dragSnap)
            QCOMPARE(moved[0].x, 160);
        else
            QVERIFY(moved[0].x > 145 && moved[0].x < 158);
        QCOMPARE(moved[1].x, group ? moved[0].x + 41 : 170);
        QCOMPARE(moved[0].startPosition(), originals[0].startPosition());
        charts.undo();
        QCOMPARE(charts.chart()->notes(), originals);
        QVERIFY(!charts.canUndo());
    }

    void bothGridDialogsSaveThePreference_data()
    {
        QTest::addColumn<bool>("panelEntry");
        QTest::newRow("canvas") << false;
        QTest::newRow("note-panel") << true;
    }

    void bothGridDialogsSaveThePreference()
    {
        QFETCH(bool, panelEntry);
        Settings::instance().setEditorDragGridSnapEnabled(false);
        ChartCanvas &canvas = *m_canvas;
        canvas.setDragGridSnap(false);
        NoteEditPanel panel;
        QSignalSpy changes(&panel, &NoteEditPanel::dragGridSnapChanged);
        bool found = false;
        QTimer::singleShot(0, this, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog)
                return;
            if (auto *check = dialog->findChild<QCheckBox *>("dragGridSnapCheck"))
            {
                found = true;
                check->setChecked(true);
            }
            dialog->accept();
        });
        if (panelEntry)
            QVERIFY(QMetaObject::invokeMethod(&panel, "onGridSettingsClicked", Qt::DirectConnection));
        else
            canvas.showGridSettings();
        QVERIFY(found);
        QVERIFY(Settings::instance().editorDragGridSnapEnabled());
        if (panelEntry)
            QCOMPARE(changes.size(), 1);
    }
};

int main(int argc, char **argv)
{
#ifdef Q_OS_WIN
    if (qEnvironmentVariable("QT_QPA_PLATFORM") == QLatin1String("offscreen")
        && qEnvironmentVariableIsEmpty("QT_QPA_FONTDIR"))
        qputenv("QT_QPA_FONTDIR", (qEnvironmentVariable("WINDIR", "C:/Windows") + "/Fonts").toUtf8());
#endif
    QApplication app(argc, argv);
#ifdef Q_OS_WIN
    if (qEnvironmentVariable("QT_QPA_PLATFORM") == QLatin1String("offscreen"))
    {
        const int font = QFontDatabase::addApplicationFont(qEnvironmentVariable("WINDIR", "C:/Windows") + "/Fonts/segoeui.ttf");
        const QStringList families = QFontDatabase::applicationFontFamilies(font);
        if (!families.isEmpty())
            app.setFont(QFont(families.first(), 9));
    }
#endif
    GridDragTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "grid_drag_tests.moc"
