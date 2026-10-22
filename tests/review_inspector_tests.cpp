#include "controller/ChartController.h"
#include "controller/SelectionController.h"
#include "identity/LocalIdentityStore.h"
#include "model/Chart.h"
#include "ui/review/IdentityDialog.h"
#include "ui/review/NoteInspector.h"

#include <QLineEdit>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <QScrollArea>
#include <QScrollBar>

using CceReviewUi::IdentityDialog;
using CceReviewUi::NoteInspector;

class ReviewInspectorTests : public QObject
{
    Q_OBJECT

private slots:
    void appliesValidatedBatchAndPreservesIds();
    void editsRainAndSoundSpecificFields();
    void mixedValuesAndComparisonModeAreReadOnly();
    void invalidValuesDoNotChangeTheChart();
    void identityDialogUsesSuppliedLocalStore();
    void compactSidebarPreservesFieldHeightsWithScrolling();
};

void ReviewInspectorTests::appliesValidatedBatchAndPreservesIds()
{
    ChartController chartController;
    chartController.addNote(Note(4, 0, 4, 120));
    chartController.addNote(Note(8, 0, 4, 240));
    SelectionController selectionController;
    selectionController.setNotes(&chartController.chart()->notes(), chartController.revision());
    selectionController.select(QSet<int>{0, 1});

    NoteInspector inspector(&chartController, &selectionController);
    auto *beat = inspector.findChild<QLineEdit *>(QStringLiteral("reviewNoteBeatEdit"));
    auto *apply = inspector.findChild<QPushButton *>(QStringLiteral("reviewNoteApplyButton"));
    QVERIFY(beat);
    QVERIFY(apply);
    QVERIFY(beat->text().isEmpty());
    QCOMPARE(beat->placeholderText(), QStringLiteral("Mixed"));

    QVector<QString> ids;
    for (const Note &note : chartController.chart()->notes())
        ids.append(note.id);
    QSignalSpy changeSpy(&chartController, &ChartController::chartChangeCommitted);
    beat->setText(QStringLiteral("12:1/8"));
    apply->click();

    QCOMPARE(changeSpy.count(), 1);
    const auto &updated = chartController.chart()->notes();
    QCOMPARE(updated.size(), 2);
    for (const Note &note : updated)
    {
        QVERIFY(ids.contains(note.id));
        QCOMPARE(note.beatNum, 12);
        QCOMPARE(note.numerator, 1);
        QCOMPARE(note.denominator, 8);
    }
}

void ReviewInspectorTests::editsRainAndSoundSpecificFields()
{
    ChartController rainController;
    rainController.addNote(Note(4, 0, 4, 8, 0, 4, 256));
    SelectionController rainSelection;
    rainSelection.setNotes(&rainController.chart()->notes(), rainController.revision());
    rainSelection.select(0);
    NoteInspector rainInspector(&rainController, &rainSelection);
    auto *rainEnd = rainInspector.findChild<QLineEdit *>(QStringLiteral("reviewNoteRainEndEdit"));
    auto *rainApply = rainInspector.findChild<QPushButton *>(QStringLiteral("reviewNoteApplyButton"));
    QVERIFY(rainEnd);
    QVERIFY(rainApply);
    const QString rainId = rainController.chart()->notes().first().id;
    rainEnd->setText(QStringLiteral("10:1/4"));
    rainApply->click();
    const Note updatedRain = rainController.chart()->notes().first();
    QCOMPARE(updatedRain.id, rainId);
    QCOMPARE(updatedRain.endBeatNum, 10);
    QCOMPARE(updatedRain.endNumerator, 1);
    QCOMPARE(updatedRain.endDenominator, 4);

    ChartController soundController;
    soundController.addNote(Note(2, 0, 4, QStringLiteral("hit.ogg"), 70, 12));
    SelectionController soundSelection;
    soundSelection.setNotes(&soundController.chart()->notes(), soundController.revision());
    soundSelection.select(0);
    NoteInspector soundInspector(&soundController, &soundSelection);
    auto *sound = soundInspector.findChild<QLineEdit *>(QStringLiteral("reviewNoteSoundEdit"));
    auto *volume = soundInspector.findChild<QLineEdit *>(QStringLiteral("reviewNoteVolumeEdit"));
    auto *offset = soundInspector.findChild<QLineEdit *>(QStringLiteral("reviewNoteOffsetEdit"));
    auto *soundApply = soundInspector.findChild<QPushButton *>(QStringLiteral("reviewNoteApplyButton"));
    QVERIFY(sound);
    QVERIFY(volume);
    QVERIFY(offset);
    QVERIFY(soundApply);
    const QString soundId = soundController.chart()->notes().first().id;
    sound->setText(QStringLiteral("tap.wav"));
    volume->setText(QStringLiteral("85"));
    offset->setText(QStringLiteral("-20"));
    soundApply->click();
    const Note updatedSound = soundController.chart()->notes().first();
    QCOMPARE(updatedSound.id, soundId);
    QCOMPARE(updatedSound.sound, QStringLiteral("tap.wav"));
    QCOMPARE(updatedSound.vol, 85);
    QCOMPARE(updatedSound.offset, -20);
}

void ReviewInspectorTests::mixedValuesAndComparisonModeAreReadOnly()
{
    ChartController chartController;
    const Note normal(4, 0, 4, 128);
    chartController.addNote(normal);
    SelectionController selectionController;
    selectionController.setNotes(&chartController.chart()->notes(), chartController.revision());
    selectionController.select(0);

    NoteInspector inspector(&chartController, &selectionController);
    const Note before = chartController.chart()->notes().first();
    Note after = before;
    after.x = 200;
    inspector.setComparison({qMakePair(before, after)});
    QVERIFY(inspector.isReadOnly());
    auto *apply = inspector.findChild<QPushButton *>(QStringLiteral("reviewNoteApplyButton"));
    auto *x = inspector.findChild<QLineEdit *>(QStringLiteral("reviewNoteXEdit"));
    QVERIFY(apply);
    QVERIFY(x);
    QVERIFY(apply->isHidden());
    QVERIFY(x->isReadOnly());
    QVERIFY(x->text().contains(QStringLiteral("Before: 128")));
    QVERIFY(x->text().contains(QStringLiteral("After: 200")));

    chartController.moveNote(before, after);
    QVERIFY(!inspector.isReadOnly());
    QVERIFY(!apply->isHidden());
    QCOMPARE(x->text(), QStringLiteral("200"));
}

void ReviewInspectorTests::invalidValuesDoNotChangeTheChart()
{
    ChartController chartController;
    chartController.addNote(Note(4, 0, 4, 128));
    SelectionController selectionController;
    selectionController.setNotes(&chartController.chart()->notes(), chartController.revision());
    selectionController.select(0);
    NoteInspector inspector(&chartController, &selectionController);
    auto *x = inspector.findChild<QLineEdit *>(QStringLiteral("reviewNoteXEdit"));
    auto *apply = inspector.findChild<QPushButton *>(QStringLiteral("reviewNoteApplyButton"));
    QVERIFY(x);
    QVERIFY(apply);

    const quint64 revision = chartController.revision();
    x->setText(QStringLiteral("513"));
    apply->click();
    QCOMPARE(chartController.revision(), revision);
    QCOMPARE(chartController.chart()->notes().first().x, 128);
    QVERIFY(!inspector.findChild<QLabel *>(QStringLiteral("reviewNoteInspectorStatus"))->text().isEmpty());
}

void ReviewInspectorTests::identityDialogUsesSuppliedLocalStore()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    CceIdentity::LocalIdentityStore store(directory.filePath(QStringLiteral("identity.cceidentity")));
    QVERIFY(store.createIdentity(QStringLiteral("Local signer")));

    IdentityDialog dialog(&store);
    QCOMPARE(dialog.objectName(), QStringLiteral("reviewIdentityDialog"));
    auto *list = dialog.findChild<QListWidget *>(QStringLiteral("reviewIdentityList"));
    auto *current = dialog.findChild<QLabel *>(QStringLiteral("reviewCurrentIdentity"));
    QVERIFY(list);
    QVERIFY(current);
    QCOMPARE(list->count(), 1);
    QVERIFY(current->text().contains(QStringLiteral("Local signer")));
}

void ReviewInspectorTests::compactSidebarPreservesFieldHeightsWithScrolling()
{
    ChartController controller;
    controller.addNote(Note(4, 0, 1, 128));
    SelectionController selection;
    selection.setNotes(&controller.chart()->notes(), controller.revision());
    selection.select(0);
    QScrollArea scroll;
    scroll.setWidgetResizable(true);
    scroll.setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *inspector = new NoteInspector(&controller, &selection);
    scroll.setWidget(inspector);
    scroll.resize(220, 130);
    scroll.show();
    QApplication::processEvents();
    QVERIFY(scroll.verticalScrollBar()->maximum() > 0);
    const auto editors = inspector->findChildren<QLineEdit *>();
    for (auto *editor : editors)
        if (!editor->isHidden())
        {
            QVERIFY(editor->height() >= editor->minimumSizeHint().height());
            QVERIFY(editor->width() > 60);
            QVERIFY(editor->geometry().right() <= inspector->width());
        }
}

QTEST_MAIN(ReviewInspectorTests)
#include "review_inspector_tests.moc"
