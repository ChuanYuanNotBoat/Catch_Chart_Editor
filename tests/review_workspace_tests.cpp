#include <QtTest>

#include "controller/ChartController.h"
#include "controller/SelectionController.h"
#include "controller/CommandRouter.h"
#include "document/ProjectStorage.h"
#include "file/ChartIO.h"
#include "review/CommentModel.h"
#include "review/ReviewWorkspace.h"
#include "utils/Settings.h"
#include "ui/CustomWidgets/ChartCanvas/ChartCanvas.h"
#include "ui/BPMTimePanel.h"
#include "ui/MetaEditPanel.h"
#include "ui/LongRangeSelector.h"
#include "app/MainWindow.h"
#include "controller/PlaybackController.h"
#include "audio/AudioPlayer.h"
#include "model/Skin.h"
#include <QJsonDocument>
#include <QFileDialog>
#include <QGroupBox>
#include <QListWidget>
#include <QContextMenuEvent>

#include <QJsonArray>
#include <QJsonObject>
#include <QMainWindow>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <memory>
#include <algorithm>
#include <QFile>
#include <QFileInfo>
#include <QDialog>
#include <QImage>
#include <QTabWidget>
#include <QDir>
#include <QMenu>
#include <QInputDialog>
#include <QTimer>
#include <QElapsedTimer>
#include <QPainter>
#include <QLineEdit>
#include <QTreeWidget>
#include <QComboBox>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QCryptographicHash>
#include <QSemaphore>
#include <QSettings>
#include <QMouseEvent>
#include <atomic>

namespace
{
Chart sampleChart()
{
    Chart chart;
    MetaData meta;
    meta.title = QStringLiteral("Review test song");
    meta.artist = QStringLiteral("Review test artist");
    meta.difficulty = QStringLiteral("Hard");
    meta.chartAuthor = QStringLiteral("Chart author");
    meta.firstBpm = 120.0;
    chart.meta() = meta;
    chart.addBpm(BpmEntry(0, 0, 1, 120.0));
    Note first(1, 0, 1, 64);
    first.id = QStringLiteral("runtime-first");
    Note second(3, 0, 1, 192);
    second.id = QStringLiteral("runtime-second");
    chart.addNotes({first, second});
    return chart;
}

QString firstLayerId(const CceDocument::Project &project)
{
    const auto difficulty = project.difficulties.constFind(project.activeDifficultyId);
    if (difficulty == project.difficulties.cend() || difficulty->bindings.isEmpty())
        return {};
    return difficulty->bindings.first().layerId;
}

QJsonArray packageChanges(const QString &path, QJsonObject *revisionOut = nullptr)
{
    QJsonObject revision;
    if (!CceReview::readPackage(path, revision))
        return {};
    if (revisionOut)
        *revisionOut = revision;
    return revision.value(QStringLiteral("changes")).toArray();
}

QJsonObject commentByIndex(const CceReview::ReviewWorkspace &workspace, int index)
{
    return workspace.comments().at(index).toObject();
}

QJsonObject firstNoteTarget(const QJsonObject &comment)
{
    return comment.value(QStringLiteral("anchor"))
        .toObject()
        .value(QStringLiteral("targets"))
        .toArray()
        .at(0)
        .toObject();
}

bool hasNoteAt(const Chart &chart, int beat, int x)
{
    return std::any_of(chart.notes().cbegin(), chart.notes().cend(), [=](const Note &note) {
        return note.beatNum == beat && note.x == x;
    });
}

class WorkspaceRig
{
public:
    explicit WorkspaceRig(const QString &tag, CceReview::ReviewWorkspace::DiffRunner runner = {})
        : identityPath(temp.filePath(tag + QStringLiteral(".identity.json")))
    {
        Q_ASSERT(temp.isValid());
        canvas.setChartController(&controller);
        canvas.setSelectionController(&selection);
        controller.loadChartFromData(temp.filePath(tag + QStringLiteral(".mc")), sampleChart());
        selection.setNotes(&controller.chart()->notes(), controller.revision());
        workspace = std::make_unique<CceReview::ReviewWorkspace>(&controller, &selection, &canvas, &window,
                                                                 identityPath, std::move(runner));
        workspace->installMenus(&router);
    }

    bool initialize(QString *error = nullptr)
    {
        if (!workspace->identities()->createIdentity(QStringLiteral("Reviewer"), error))
            return false;
        CceDocument::Project project = workspace->project();
        project.activeLayerId = firstLayerId(project);
        return workspace->setProject(project, error);
    }

    ~WorkspaceRig()
    {
        // Native expose notifications can still be queued when a fixture ends.
        // Close its windows while their models and Qt window data remain alive.
        for (auto *dialog : window.findChildren<QDialog *>())
            dialog->hide();
        canvas.hide();
        window.hide();
        if (QApplication::platformName() == "windows")
            QTest::qWait(20);
    }

    QTemporaryDir temp;
    QString identityPath;
    QMainWindow window;
    ChartController controller;
    SelectionController selection;
    ChartCanvas canvas;
    CommandRouter router{&window};
    std::unique_ptr<CceReview::ReviewWorkspace> workspace;
};

void selectNote(WorkspaceRig &rig, int index)
{
    rig.selection.setNotes(&rig.controller.chart()->notes(), rig.controller.revision());
    rig.selection.select(index);
}

bool start(WorkspaceRig &rig)
{
    QString error;
    return rig.initialize(&error) && rig.workspace->startProposal(QStringLiteral("Test proposal"), &error);
}

QPushButton *buttonNamed(QWidget *widget, const QString &text)
{
    for (auto *button : widget->findChildren<QPushButton *>())
        if (button->text() == text)
            return button;
    return nullptr;
}

void acceptCommentDialog()
{
    QTimer::singleShot(0, [] {
        if (auto *dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget()))
        {
            dialog->setTextValue("Review selection");
            dialog->accept();
        }
    });
}

bool runContextAction(const std::function<void()> &open, const QString &name)
{
    bool found = false;
    QTimer::singleShot(0, [&] {
        auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        if (!menu)
            return;
        auto *action = menu->findChild<QAction *>(name);
        found = action && action->isEnabled();
        menu->close();
        if (found)
        {
            acceptCommentDialog();
            action->trigger();
        }
    });
    open();
    return found;
}

QByteArray fileHash(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256);
}
} // namespace

class ReviewWorkspaceTests final : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName("CceReviewTests");
        QCoreApplication::setApplicationName("Rv"); // Keep nested Windows working-copy paths below MAX_PATH.
        QApplication::setQuitOnLastWindowClosed(false);
    }

    void sharedLayerWarning_data()
    {
        QTest::addColumn<int>("choice");
        QTest::newRow("continue") << 0;
        QTest::newRow("copy") << 1;
        QTest::newRow("cancel") << 2;
    }
    void layerGuiWorksWithoutIdentityAndPreservesExportChoice()
    {
        WorkspaceRig rig("layer-gui");
        QString error;
        rig.workspace->showPanel();
        auto *panel = rig.window.findChild<QDialog *>("reviewWorkspaceWindow");
        QVERIFY(panel);
        auto *tabs = panel->findChild<QTabWidget *>();
        QVERIFY(tabs);
        tabs->setCurrentIndex(0);
        auto *create = buttonNamed(panel, "New Layer");
        QVERIFY(create);
        QTimer::singleShot(0, [] {
            if (auto *dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget()))
            {
                dialog->setTextValue("GUI layer");
                dialog->accept();
            }
        });
        QTest::mouseClick(create, Qt::LeftButton);
        QCOMPARE(rig.workspace->project().layers.size(), 2);
        QVERIFY(rig.workspace->identities()->activeIdentity().authorId.isEmpty());
        rig.controller.addNote(Note(8, 0, 1, 300));
        QCOMPARE(rig.controller.chart()->notes().size(), 3);
        auto *tree = panel->findChild<QTreeWidget *>("reviewLayers");
        QVERIFY(tree);
        const auto selected = [&]() {
            for (int i = 0; i < tree->topLevelItemCount(); ++i)
                if (tree->topLevelItem(i)->text(0) == "GUI layer")
                    return tree->topLevelItem(i);
            return static_cast<QTreeWidgetItem *>(nullptr);
        };
        QVERIFY(selected());
        selected()->setCheckState(1, Qt::Unchecked);
        selected()->setText(4, "45"); // Two pending fields must merge without losing either edit.
        QApplication::processEvents();
        const auto bindings =
            rig.workspace->project().difficulties.value(rig.workspace->project().activeDifficultyId).bindings;
        QVERIFY(std::any_of(bindings.cbegin(), bindings.cend(), [](const auto &binding) {
            return !binding.visible && binding.opacity == .45;
        }));
        QCOMPARE(rig.controller.chart()->notes().size(), 3); // Hidden still counts and exports.
        selected()->setCheckState(2, Qt::Checked);
        QApplication::processEvents();
        rig.controller.addNote(Note(9, 0, 1, 350));
        QCOMPARE(rig.controller.chart()->notes().size(), 3);
        selected()->setCheckState(2, Qt::Unchecked);
        QApplication::processEvents();
        const auto visibleExport = rig.temp.filePath("hidden-layer.mc");
        QVERIFY2(rig.workspace->exportChart(visibleExport, {}, &error), qPrintable(error));
        Chart chart;
        QVERIFY(ChartIO::load(visibleExport, chart, false));
        QCOMPARE(chart.notes().size(), 3);
        selected()->setCheckState(3, Qt::Unchecked);
        QApplication::processEvents();
        const auto excludedExport = rig.temp.filePath("excluded-layer.mc");
        QVERIFY(rig.workspace->exportChart(excludedExport, {}, &error));
        QVERIFY(ChartIO::load(excludedExport, chart, false));
        QCOMPARE(chart.notes().size(), 2);
        tree->setCurrentItem(selected());
        auto *unbind = buttonNamed(panel, "Unbind");
        QVERIFY(unbind);
        QTest::mouseClick(unbind, Qt::LeftButton);
        QCOMPARE(rig.controller.chart()->notes().size(), 2);
        rig.controller.undo();
        QCOMPARE(rig.controller.chart()->notes().size(), 3);
        auto *duplicate = buttonNamed(panel, "Duplicate");
        QVERIFY(duplicate);
        QTimer::singleShot(0, [] {
            if (auto *dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget()))
            {
                dialog->setTextValue("Copied difficulty");
                dialog->accept();
            }
        });
        QTest::mouseClick(duplicate, Qt::LeftButton);
        QCOMPARE(rig.workspace->project().difficulties.size(), 2);
        const auto copied = rig.workspace->project().difficulties.value(rig.workspace->project().activeDifficultyId);
        QCOMPARE(copied.metadata.chartAuthor, QString("Chart author"));
        QVERIFY(std::any_of(copied.bindings.cbegin(), copied.bindings.cend(), [](const auto &binding) {
            return !binding.visible && !binding.exportEnabled && binding.opacity == .45;
        }));
        QCOMPARE(rig.controller.chart()->notes().size(), 3);
    }
    void sharedLayerWarning()
    {
        QFETCH(int, choice);
        WorkspaceRig rig("shared");
        QString error;
        QVERIFY(rig.initialize(&error));
        auto project = rig.workspace->project();
        const auto layer = firstLayerId(project);
        const auto firstDifficulty = project.activeDifficultyId;
        const auto other = project.addDifficulty("Shared", project.difficulties[firstDifficulty].metadata,
                                                 project.difficulties[firstDifficulty].timing);
        QVERIFY(project.bindLayer(other, layer));
        QVERIFY(rig.workspace->setProject(project, &error));
        bool dialogSeen = false;
        QTimer::singleShot(0, [&] {
            auto *dialog = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (!dialog)
                return;
            dialogSeen = true;
            if (choice == 2)
                dialog->button(QMessageBox::Cancel)->click();
            else
                for (auto *button : dialog->buttons())
                    if (dialog->buttonRole(button) == (choice == 0 ? QMessageBox::AcceptRole : QMessageBox::ActionRole))
                    {
                        button->click();
                        break;
                    }
        });
        rig.controller.addNote(Note(8, 0, 1, 300));
        QVERIFY(dialogSeen);
        if (choice == 0)
        {
            QCOMPARE(rig.workspace->project().compose(other).notes().size(), 3);
            // A second edit in the same session must not open another modal.
            QTimer::singleShot(0, [] {
                if (auto *dialog = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                    dialog->reject();
            });
            rig.controller.addNote(Note(9, 0, 1, 350));
            QApplication::processEvents();
            QCOMPARE(rig.workspace->project().compose(other).notes().size(), 4);
        }
        else if (choice == 1)
        {
            QTRY_COMPARE(rig.workspace->project().bindingCount(layer), 1);
            QCOMPARE(rig.controller.chart()->notes().size(), 2);
            rig.controller.addNote(Note(8, 0, 1, 300));
            QCOMPARE(rig.controller.chart()->notes().size(), 3);
            QCOMPARE(rig.workspace->project().compose(other).notes().size(), 2);
        }
        else
        {
            QCOMPARE(rig.workspace->project().toJson(), project.toJson());
            QCOMPARE(rig.controller.chart()->notes().size(), 2);
        }
    }

    void staleDiffNeverPublishes()
    {
        struct Gate
        {
            QSemaphore entered, release;
            std::atomic<int> calls{0};
        };
        const auto gate = std::make_shared<Gate>();
        WorkspaceRig rig("stale", [gate](const auto &base, const auto &proposal, const auto &intents) {
            ++gate->calls;
            gate->entered.release();
            gate->release.tryAcquire(1, 10000);
            return CceReview::diff(base, proposal, intents);
        });
        QVERIFY(start(rig));
        rig.workspace->showPanel();
        auto *tree = rig.window.findChild<QTreeWidget *>("reviewChanges");
        QVERIFY(tree);
        QTRY_VERIFY(gate->entered.available() > 0);
        gate->entered.acquire();
        rig.controller.addNote(Note(12, 0, 1, 400));
        gate->release.release();
        QTRY_COMPARE(gate->calls.load(), 2);
        QCOMPARE(tree->topLevelItemCount(), 0); // First worker saw the old, empty proposal.
        gate->release.release();
        QTRY_COMPARE(tree->topLevelItemCount(), 1);
        QCOMPARE(tree->topLevelItem(0)->text(1), QString("Add"));
    }
    void coordinatorDestructionDetachesPresentationAndUndoCallbacks()
    {
        WorkspaceRig rig("lifetime");
        QVERIFY(start(rig));
        QString error;
        QVERIFY(rig.workspace->addCommentAtSelection("Undo callback", "TimePosition", &error));
        rig.workspace->showPanel();
        rig.workspace->setViewMode(2);
        QPointer<QWidget> inspector = rig.workspace->inspector();
        QPointer<QDialog> panel = rig.window.findChild<QDialog *>("reviewWorkspaceWindow");
        QVERIFY(inspector && panel);
        rig.workspace.reset();
        QVERIFY(!inspector && !panel);
        QVERIFY(!rig.controller.isReadOnly());
        rig.selection.select(0);
        rig.controller.undo();
        rig.controller.redo();
        rig.selection.select(1);
        QCOMPARE(rig.controller.chart()->notes().size(), 2);
    }
    void difficultyImportFailureKeepsSourceAndProject()
    {
        WorkspaceRig rig("import-failure");
        auto chart = sampleChart();
        chart.meta().backgroundFile = "missing.jpg";
        const auto path = rig.temp.filePath("source.mc");
        QVERIFY(ChartIO::save(path, chart));
        const auto sourceHash = fileHash(path);
        const auto project = rig.workspace->project().toJson();
        const auto undo = rig.controller.nextUndoActionText();
        QString error;
        QVERIFY(!rig.workspace->importDifficultyFromPath(path, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(rig.workspace->project().toJson(), project);
        QCOMPARE(rig.controller.nextUndoActionText(), undo);
        QCOMPARE(fileHash(path), sourceHash);
        QVERIFY(!rig.workspace->projectMode());
    }

    void saveProposalKeepsConfigurationAndClearsReview()
    {
        WorkspaceRig rig("convert");
        QString error;
        QVERIFY(start(rig));
        auto project = rig.workspace->project();
        auto &difficulty = project.difficulties[project.activeDifficultyId];
        difficulty.exportedMetadataFields = {"title", "artist"};
        difficulty.bindings[0].visible = false;
        difficulty.bindings[0].opacity = .4;
        difficulty.bindings[0].order = 7;
        difficulty.bindings[0].exportEnabled = false;
        QVERIFY(rig.workspace->setProject(project, &error));
        QVERIFY(rig.workspace->addCommentAtSelection("Review only", "TimePosition", &error));
        const auto priorDifficulty = project.activeDifficultyId;
        QVERIFY2(rig.workspace->saveProposalAsDifficulty(&error), qPrintable(error));
        const auto result = rig.workspace->project();
        QVERIFY(result.activeDifficultyId != priorDifficulty);
        const auto converted = result.difficulties.value(result.activeDifficultyId);
        QCOMPARE(converted.exportedMetadataFields, difficulty.exportedMetadataFields);
        QCOMPARE(converted.bindings[0].visible, false);
        QCOMPARE(converted.bindings[0].opacity, .4);
        QCOMPARE(converted.bindings[0].order, 7);
        QCOMPARE(converted.bindings[0].exportEnabled, false);
        QVERIFY(converted.bindings[0].layerId != difficulty.bindings[0].layerId);
        QVERIFY(rig.workspace->comments().isEmpty());
        QVERIFY(rig.workspace->revision().isEmpty());
        rig.controller.undo();
        QCOMPARE(rig.workspace->project().activeDifficultyId, priorDifficulty);
        QCOMPARE(rig.workspace->comments().size(), 1);
        QVERIFY(!rig.workspace->revision().isEmpty());
        rig.controller.redo();
        QVERIFY(rig.workspace->comments().isEmpty());
        const auto saved = rig.temp.filePath("converted.cceproj");
        QVERIFY(rig.workspace->saveProject(saved, &error));
        CceDocument::ProjectBundle bundle;
        QVERIFY(CceDocument::readProject(saved, bundle, &error));
        const auto events = bundle.review.value("events").toArray();
        QCOMPARE(events.last().toObject().value("type").toString(), QString("Redo"));
        QVERIFY(!events.last().toObject().value("timestamp").toString().isEmpty());
        QVERIFY(!events.last().toObject().value("author").toObject().isEmpty());
    }

    void convertedProposalDoesNotReplaceExistingDifficultyResources()
    {
        WorkspaceRig rig("aliases");
        QString error;
        QVERIFY(rig.initialize(&error));
        CceDocument::ProjectBundle base;
        base.project = rig.workspace->project();
        const auto difficulty = base.project.activeDifficultyId;
        const auto layer = firstLayerId(base.project);
        const QByteArray oldBytes("original audio"), newBytes("proposal audio");
        const auto hash = [](const QByteArray &bytes) {
            return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
        };
        base.project.layers[layer].notes.append(Note(0, 0, 1, "audio.ogg", 100, 0));
        base.project.difficulties[difficulty].metadata.audioFile = "audio.ogg";
        base.project.difficulties[difficulty].oggHash = hash(oldBytes);
        base.resourcePaths["audio.ogg"] = hash(oldBytes);
        base.resources[hash(oldBytes)] = oldBytes;
        const auto initial = rig.temp.filePath("base.cceproj");
        QVERIFY2(CceDocument::writeProject(initial, base, &error), qPrintable(error));
        QVERIFY2(rig.workspace->openProject(initial, &error), qPrintable(error));
        auto proposal = base;
        proposal.project.difficulties[difficulty].oggHash = hash(newBytes);
        proposal.resourcePaths["audio.ogg"] = hash(newBytes);
        const auto snapshot = [](const CceDocument::ProjectBundle &bundle) {
            auto json = bundle.project.toJson();
            json.remove("activeDifficultyId");
            QJsonObject paths;
            for (auto it = bundle.resourcePaths.cbegin(); it != bundle.resourcePaths.cend(); ++it)
                paths[it.key()] = it.value();
            json["resources"] = paths;
            return json;
        };
        auto revision = CceReview::createRevision(snapshot(base), snapshot(proposal), {}, "Replace audio");
        revision["resources"] =
            QJsonObject{{hash(oldBytes), QJsonObject{{"dataBase64", QString::fromLatin1(oldBytes.toBase64())},
                                                     {"paths", QJsonArray{"audio.ogg"}}}},
                        {hash(newBytes), QJsonObject{{"dataBase64", QString::fromLatin1(newBytes.toBase64())},
                                                     {"paths", QJsonArray{"audio.ogg"}}}}};
        const auto package = rig.temp.filePath("replace.ccepr");
        QVERIFY2(CceReview::writePackage(package, revision, &error), qPrintable(error));
        QVERIFY2(rig.workspace->importProposal(package, &error), qPrintable(error));
        QVERIFY2(rig.workspace->saveProposalAsDifficulty(&error), qPrintable(error));
        const auto saved = rig.temp.filePath("converted.cceproj");
        QVERIFY2(rig.workspace->saveProject(saved, &error), qPrintable(error));
        CceDocument::ProjectBundle result;
        QVERIFY(CceDocument::readProject(saved, result, &error));
        QCOMPARE(result.resourcePaths.value("audio.ogg"), hash(oldBytes));
        QCOMPARE(result.project.difficulties[difficulty].metadata.audioFile, QString("audio.ogg"));
        const auto converted = result.project.difficulties.value(result.project.activeDifficultyId);
        QVERIFY(converted.metadata.audioFile != "audio.ogg");
        QCOMPARE(result.resourcePaths.value(converted.metadata.audioFile), hash(newBytes));
        const auto notes = result.project.compose(converted.difficultyId).notes();
        const auto sound = std::find_if(notes.cbegin(), notes.cend(), [](const auto &note) {
            return note.isSound();
        });
        QVERIFY(sound != notes.cend());
        QCOMPARE(sound->sound, converted.metadata.audioFile);
    }

    void rebasePreservesUnmatchedCommentAsMissing()
    {
        WorkspaceRig author("rebase-author");
        QVERIFY(start(author));
        QString error;
        const auto base = author.temp.filePath("base.cceproj");
        // Save a normal base with the same project IDs, before the proposal edit.
        CceDocument::ProjectBundle baseBundle;
        baseBundle.project = author.workspace->project();
        QVERIFY(CceDocument::writeProject(base, baseBundle, &error));
        selectNote(author, 1);
        QVERIFY(author.workspace->addCommentAtSelection("Current chart will move this", "Note", &error));
        const auto before = author.controller.chart()->notes().first();
        auto moved = before;
        moved.x = 128;
        author.controller.moveNote(before, moved);
        const auto package = author.temp.filePath("rebase.ccepr");
        QVERIFY(author.workspace->exportProposal(package, &error));
        WorkspaceRig reviewer("rebase-reviewer");
        QVERIFY(reviewer.initialize(&error));
        QVERIFY(reviewer.workspace->openProject(base, &error));
        const auto second = reviewer.controller.chart()->notes()[1];
        auto current = second;
        current.x = 300;
        reviewer.controller.moveNote(second, current);
        QVERIFY(reviewer.workspace->importProposal(package, &error));
        const auto oldRevision = reviewer.workspace->revision().value("revisionId");
        QVERIFY2(reviewer.workspace->rebase(&error), qPrintable(error));
        QVERIFY(reviewer.workspace->revision().value("revisionId") != oldRevision);
        const auto target = firstNoteTarget(reviewer.workspace->comments()[0].toObject());
        // The separate Current edit supplied no portable Move provenance. Keep
        // the old location visible instead of guessing an identity by beat.
        QCOMPARE(target.value("state").toString(), QString("Missing"));
        QCOMPARE(target.value("note").toObject().value("x").toInt(), 192);
        QCOMPARE(target.value("originalNote").toObject().value("x").toInt(), 192);
    }

    void realChartCopyRoundTripAndGui()
    {
        const auto source = qEnvironmentVariable("CCE_REVIEW_SOURCE_CHART");
        if (source.isEmpty())
            QSKIP("Set CCE_REVIEW_SOURCE_CHART to test a real chart through isolated copies.");
        WorkspaceRig author("real-author");
        QString error;
        const QFileInfo sourceInfo(source);
        const QDir sourceFolder = sourceInfo.dir();
        QHash<QString, QByteArray> originals;
        for (const auto &file : sourceFolder.entryInfoList(QDir::Files))
        {
            originals[file.fileName()] = fileHash(file.absoluteFilePath());
            QVERIFY2(QFile::copy(file.absoluteFilePath(), author.temp.filePath(file.fileName())),
                     qPrintable(file.fileName()));
        }
        const auto copy = author.temp.filePath(sourceInfo.fileName());
        Chart loaded;
        QVERIFY(ChartIO::load(copy, loaded, false)); // Loader only ever sees the copy.
        QVERIFY(loaded.notes().size() > 500);
        QVERIFY(author.controller.loadChartFromData(copy, loaded));
        QVERIFY(!author.workspace->projectMode());
        QVERIFY(author.workspace->identities()->activeIdentity().authorId.isEmpty());
        author.controller.addNote(Note(2000, 0, 1, 256));
        QCOMPARE(author.controller.chart()->notes().size(), loaded.notes().size() + 1);
        author.controller.undo();
        QCOMPARE(author.controller.chart()->notes().size(), loaded.notes().size());
        QVERIFY(!author.workspace->projectMode());
        const auto legacy = author.temp.filePath("legacy-save.mc");
        QVERIFY(ChartIO::save(legacy, *author.controller.chart()));
        Chart legacyReload;
        QVERIFY(ChartIO::load(legacy, legacyReload, false));
        QCOMPARE(CceDocument::encodeChart(legacyReload, true), CceDocument::encodeChart(loaded, true));
        const auto originalDifficulty = author.workspace->project().activeDifficultyId;
        const auto copyHash = fileHash(copy);
        QVERIFY2(author.workspace->importDifficultyFromPath(copy, &error), qPrintable(error));
        QCOMPARE(fileHash(copy), copyHash);
        QCOMPARE(author.controller.chart()->notes().size(), loaded.notes().size());
        QCOMPARE(author.workspace->project().difficulties.size(), 2);
        auto project = author.workspace->project();
        QVERIFY2(project.addReference(project.activeDifficultyId, originalDifficulty, {}, &error), qPrintable(error));
        QVERIFY(author.workspace->setProject(project, &error));
        QCOMPARE(author.controller.chart()->notes().size(), loaded.notes().size()); // Reference excluded.
        QVERIFY(author.workspace->identities()->createIdentity("Local author", &error));
        const auto base = author.temp.filePath("base.cceproj");
        QVERIFY2(author.workspace->saveProject(base, &error), qPrintable(error));
        QVERIFY2(author.workspace->startProposal("Real chart review", &error), qPrintable(error));
        selectNote(author, 1);
        QVERIFY(author.workspace->addCommentAtSelection("Inspect this pattern", "Note", &error));
        QList<QPair<Note, Note>> moves;
        for (const auto &note : author.controller.chart()->notes())
            if (note.isNormal() && moves.size() < 2)
            {
                auto moved = note;
                moved.x = note.x < 450 ? note.x + 20 : note.x - 20;
                moves.append({note, moved});
            }
        QCOMPARE(moves.size(), 2);
        author.controller.moveNotes(moves);
        auto metadata = author.controller.chart()->meta();
        metadata.title += " [proposal]";
        metadata.offset += 5;
        author.controller.setMetaData(metadata);
        auto timing = author.controller.chart()->bpmList();
        timing.append(BpmEntry(2000, 0, 1, 135));
        QVERIFY(author.controller.replaceBpmList("Review timing", timing));
        const auto package = author.temp.filePath("round1.ccepr");
        QVERIFY2(author.workspace->exportProposal(package, &error), qPrintable(error));
        WorkspaceRig reviewer("real-reviewer");
        QVERIFY(reviewer.workspace->identities()->createIdentity("Local reviewer", &error));
        QVERIFY2(reviewer.workspace->openProject(base, &error), qPrintable(error));
        const auto before = CceDocument::encodeChart(*reviewer.controller.chart(), true);
        QVERIFY2(reviewer.workspace->importProposal(package, &error), qPrintable(error));
        QCOMPARE(CceDocument::encodeChart(*reviewer.controller.chart(), true), before);
        reviewer.workspace->showPanel();
        reviewer.workspace->setViewMode(1);
        auto *panel = reviewer.window.findChild<QDialog *>("reviewWorkspaceWindow");
        QVERIFY(panel);
        auto *notice = panel->findChild<QLabel *>("reviewExperimentalNotice");
        QVERIFY(notice && notice->isVisible());
        auto *tree = panel->findChild<QTreeWidget *>("reviewChanges");
        QVERIFY(tree && tree->topLevelItemCount() >= 4);
        tree->selectAll();
        auto *accept = buttonNamed(panel, "Accept");
        auto *apply = buttonNamed(panel, "Apply Accepted");
        QVERIFY(accept && apply);
        QTest::mouseClick(accept, Qt::LeftButton);
        QTest::mouseClick(apply, Qt::LeftButton);
        QVERIFY(reviewer.controller.chart()->meta().title.endsWith(" [proposal]"));
        QCOMPARE(reviewer.controller.chart()->bpmList().size(), timing.size());
        reviewer.controller.undo();
        QCOMPARE(CceDocument::encodeChart(*reviewer.controller.chart(), true), before);
        reviewer.controller.redo();
        auto *tabs = panel->findChild<QTabWidget *>();
        QVERIFY(tabs);
        tabs->setCurrentIndex(2);
        auto *comments = panel->findChild<QTreeWidget *>("reviewComments");
        QVERIFY(comments && comments->topLevelItemCount() == 1);
        comments->setCurrentItem(comments->topLevelItem(0));
        auto *resolve = buttonNamed(panel, "Resolve");
        QVERIFY(resolve);
        QTest::mouseClick(resolve, Qt::LeftButton);
        auto *filter = panel->findChild<QComboBox *>("reviewCommentFilter");
        QVERIFY(filter);
        filter->setCurrentIndex(filter->findData("Unresolved"));
        QCOMPARE(comments->topLevelItemCount(), 0);
        filter->setCurrentIndex(filter->findData("Resolved"));
        QCOMPARE(comments->topLevelItemCount(), 1);
        const auto round2 = reviewer.temp.filePath("round2.ccepr");
        QVERIFY2(reviewer.workspace->exportProposal(round2, &error), qPrintable(error));
        QVERIFY2(author.workspace->importProposal(round2, &error), qPrintable(error));
        QCOMPARE(author.workspace->comments()[0].toObject().value("state").toString(), QString("Resolved"));
        const auto result = reviewer.temp.filePath("result.mc");
        QVERIFY2(reviewer.workspace->exportChart(result, {}, &error), qPrintable(error));
        Chart exported;
        QVERIFY(ChartIO::load(result, exported, false));
        QCOMPARE(exported.notes().size(), loaded.notes().size());
        QCOMPARE(exported.meta().title, metadata.title);
        const auto screenshotDir = qEnvironmentVariable("CCE_REVIEW_SCREENSHOT_DIR");
        if (!screenshotDir.isEmpty())
        {
            QVERIFY(QDir().mkpath(screenshotDir));
            tabs->setCurrentIndex(1);
            panel->resize(700, 600);
            QApplication::processEvents();
            QVERIFY2(panel->width() <= 720, "Review actions must fit a narrow tool window");
            QVERIFY(panel->grab().save(screenshotDir + "/real-review-diff.png"));
            tabs->setCurrentIndex(0);
            QApplication::processEvents();
            QVERIFY(panel->grab().save(screenshotDir + "/real-review-layers.png"));
        }
        for (auto it = originals.cbegin(); it != originals.cend(); ++it)
            QCOMPARE(fileHash(sourceFolder.filePath(it.key())), it.value());
        qInfo() << "Real chart copy:" << loaded.meta().title << loaded.notes().size()
                << "notes; legacy save, multi-round review, GUI apply/resolve, undo/redo and export passed; source "
                   "hashes unchanged";
    }

    void proposalTracksExplicitMoveBatchesAndRemoveAdd()
    {
        WorkspaceRig rig(QStringLiteral("author"));
        QVERIFY(start(rig));

        const QVector<Note> before = rig.controller.chart()->notes();
        Note movedFirst = before.at(0);
        movedFirst.beatNum = 2;
        movedFirst.x = 128;
        Note movedSecond = before.at(1);
        movedSecond.beatNum = 4;
        movedSecond.x = 256;
        rig.controller.moveNotes({qMakePair(before.at(0), movedFirst), qMakePair(before.at(1), movedSecond)});
        QTRY_COMPARE(rig.controller.chart()->notes().at(0).beatNum, 2);

        const QString firstPackage = rig.temp.filePath(QStringLiteral("moves.ccepr"));
        QString error;
        QVERIFY2(rig.workspace->exportProposal(firstPackage, &error), qPrintable(error));
        const QJsonArray firstChanges = packageChanges(firstPackage);
        QStringList moveGroups;
        for (const QJsonValue &value : firstChanges)
        {
            const QJsonObject change = value.toObject();
            if (change.value(QStringLiteral("kind")).toString() == QStringLiteral("Move"))
                moveGroups.append(change.value(QStringLiteral("operationGroupId")).toString());
        }
        QCOMPARE(moveGroups.size(), 2);
        QVERIFY(!moveGroups.first().isEmpty());
        QCOMPARE(moveGroups.at(0), moveGroups.at(1));

        rig.controller.removeNote(movedFirst);
        Note added(7, 0, 1, 320);
        added.id = QStringLiteral("new-runtime-note");
        rig.controller.addNote(added);
        const QString secondPackage = rig.temp.filePath(QStringLiteral("remove-add.ccepr"));
        QVERIFY2(rig.workspace->exportProposal(secondPackage, &error), qPrintable(error));
        const QJsonArray changes = packageChanges(secondPackage);
        bool sawRemove = false;
        bool sawAdd = false;
        for (const QJsonValue &value : changes)
        {
            const QJsonObject change = value.toObject();
            const QString kind = change.value(QStringLiteral("kind")).toString();
            if (kind == QStringLiteral("Remove"))
                sawRemove = true;
            if (kind == QStringLiteral("Add"))
                sawAdd = true;
        }
        QVERIFY(sawRemove);
        QVERIFY(sawAdd);
    }

    void metadataAndTimingProduceProposalChanges()
    {
        WorkspaceRig rig(QStringLiteral("metadata"));
        QVERIFY(start(rig));
        MetaData metadata = rig.controller.chart()->meta();
        metadata.title = QStringLiteral("Metadata only change");
        rig.controller.setMetaData(metadata);
        const QString metadataPackage = rig.temp.filePath(QStringLiteral("metadata.ccepr"));
        QString error;
        QVERIFY2(rig.workspace->exportProposal(metadataPackage, &error), qPrintable(error));
        bool sawMetadata = false;
        for (const QJsonValue &value : packageChanges(metadataPackage))
            sawMetadata |= value.toObject().value(QStringLiteral("domain")).toString() == QStringLiteral("Metadata");
        QVERIFY(sawMetadata);

        const QVector<BpmEntry> timing = {BpmEntry(0, 0, 1, 120.0), BpmEntry(8, 0, 1, 150.0)};
        QVERIFY(rig.controller.replaceBpmList(QStringLiteral("Add timing entry"), timing));
        const QString timingPackage = rig.temp.filePath(QStringLiteral("timing.ccepr"));
        QVERIFY2(rig.workspace->exportProposal(timingPackage, &error), qPrintable(error));
        bool sawTiming = false;
        for (const QJsonValue &value : packageChanges(timingPackage))
            sawTiming |= value.toObject().value(QStringLiteral("domain")).toString() == QStringLiteral("Timing");
        QVERIFY(sawTiming);
    }

    void commentFollowsNoteDeletionUndoAndProjectRoundTrip()
    {
        WorkspaceRig rig(QStringLiteral("comments"));
        QVERIFY(start(rig));
        selectNote(rig, 0);
        QString error;
        QVERIFY2(
            rig.workspace->addCommentAtSelection(QStringLiteral("Track this note"), QStringLiteral("Note"), &error),
            qPrintable(error));
        QCOMPARE(rig.workspace->comments().size(), 1);

        Note original = rig.controller.chart()->notes().at(0);
        Note moved = original;
        moved.beatNum = 5;
        moved.x = 384;
        rig.controller.moveNote(original, moved);
        QTRY_VERIFY(hasNoteAt(*rig.controller.chart(), 5, 384));
        QTRY_COMPARE(firstNoteTarget(commentByIndex(*rig.workspace, 0))
                         .value(QStringLiteral("note"))
                         .toObject()
                         .value(QStringLiteral("beat"))
                         .toArray()
                         .at(0)
                         .toInt(),
                     5);

        rig.controller.removeNote(moved);
        QTRY_COMPARE(firstNoteTarget(commentByIndex(*rig.workspace, 0)).value(QStringLiteral("state")).toString(),
                     QStringLiteral("Missing"));
        rig.controller.undo();
        QTRY_VERIFY(firstNoteTarget(commentByIndex(*rig.workspace, 0)).value(QStringLiteral("state")).toString()
                    != QStringLiteral("Missing"));

        const QString projectPath = rig.temp.filePath(QStringLiteral("comments.cceproj"));
        QVERIFY2(rig.workspace->saveProject(projectPath, &error), qPrintable(error));
        CceDocument::ProjectBundle saved;
        QVERIFY2(CceDocument::readProject(projectPath, saved, &error), qPrintable(error));
        QCOMPARE(saved.review.value(QStringLiteral("comments")).toArray().size(), 1);
        const QJsonObject layer = saved.project.toJson()
                                      .value(QStringLiteral("layers"))
                                      .toObject()
                                      .value(firstLayerId(saved.project))
                                      .toObject();
        const QJsonArray serializedNotes = layer.value(QStringLiteral("notes")).toArray();
        QVERIFY(!serializedNotes.isEmpty());
        for (const QJsonValue &value : serializedNotes)
            QVERIFY(!value.toObject().contains(QStringLiteral("id")));

        WorkspaceRig reopened(QStringLiteral("reopened"));
        QVERIFY(reopened.initialize(&error));
        QVERIFY2(reopened.workspace->openProject(projectPath, &error), qPrintable(error));
        QCOMPARE(reopened.workspace->comments().size(), 1);
        QCOMPARE(reopened.workspace->project().toJson(), saved.project.toJson());
    }

    void independentImportMapsContentAndApplySharesUndoWithComments()
    {
        WorkspaceRig author(QStringLiteral("author-import"));
        QVERIFY(start(author));
        selectNote(author, 0);
        QString error;
        QVERIFY2(
            author.workspace->addCommentAtSelection(QStringLiteral("Proposal note"), QStringLiteral("Note"), &error),
            qPrintable(error));
        const Note oldNote = author.controller.chart()->notes().at(0);
        Note newNote = oldNote;
        newNote.beatNum = 6;
        newNote.x = 448;
        author.controller.moveNote(oldNote, newNote);
        QTRY_VERIFY(hasNoteAt(*author.controller.chart(), 6, 448));

        const QString proposal = author.temp.filePath(QStringLiteral("proposal.ccepr"));
        QVERIFY2(author.workspace->exportProposal(proposal, &error), qPrintable(error));
        QJsonObject originalRevision;
        packageChanges(proposal, &originalRevision);
        const QString revisionId = originalRevision.value(QStringLiteral("revisionId")).toString();
        const QString unchangedExport = author.temp.filePath(QStringLiteral("unchanged.ccepr"));
        QVERIFY2(author.workspace->exportProposal(unchangedExport, &error), qPrintable(error));
        QJsonObject unchangedRevision;
        packageChanges(unchangedExport, &unchangedRevision);
        QCOMPARE(unchangedRevision.value(QStringLiteral("revisionId")).toString(), revisionId);

        const QString standardChart = author.temp.filePath(QStringLiteral("reviewer.mc"));
        QVERIFY(ChartIO::save(standardChart, sampleChart()));
        Chart reviewerChart;
        QVERIFY(ChartIO::load(standardChart, reviewerChart, false));
        WorkspaceRig reviewer(QStringLiteral("reviewer"));
        QVERIFY(reviewer.controller.loadChartFromData(standardChart, reviewerChart));
        reviewer.selection.setNotes(&reviewer.controller.chart()->notes(), reviewer.controller.revision());
        QString reviewerIdentityError;
        QVERIFY(reviewer.workspace->identities()->createIdentity(QStringLiteral("Independent reviewer"),
                                                                 &reviewerIdentityError));
        const QJsonObject initialChart = CceDocument::encodeChart(*reviewer.controller.chart(), true);
        const CceDocument::Project initialProject = reviewer.workspace->project();
        QVERIFY(author.workspace->project().projectId != initialProject.projectId);
        QVERIFY(firstLayerId(author.workspace->project()) != firstLayerId(initialProject));
        QVERIFY2(reviewer.workspace->importProposal(proposal, &error), qPrintable(error));
        QCOMPARE(CceDocument::encodeChart(*reviewer.controller.chart(), true), initialChart);
        QCOMPARE(reviewer.workspace->comments().size(), 1);

        const QJsonArray commentsBeforeApply = reviewer.workspace->comments();
        QSet<QString> selected;
        for (const QJsonValue &value : reviewer.workspace->revision().value(QStringLiteral("changes")).toArray())
            selected.insert(value.toObject().value(QStringLiteral("changeId")).toString());
        QVERIFY(!selected.isEmpty());
        QVERIFY2(reviewer.workspace->applyChanges(selected, &error), qPrintable(error));
        QVERIFY(hasNoteAt(*reviewer.controller.chart(), 6, 448));
        QVERIFY(reviewer.controller.canUndo());
        reviewer.controller.undo();
        QCOMPARE(reviewer.controller.chart()->notes().at(0).beatNum, 1);
        QCOMPARE(reviewer.workspace->comments(), commentsBeforeApply);

        selectNote(reviewer, 0);
        QVERIFY2(reviewer.workspace->addCommentAtSelection(QStringLiteral("Reviewer response"), QStringLiteral("Note"),
                                                           &error),
                 qPrintable(error));
        const QString responsePath = reviewer.temp.filePath(QStringLiteral("response.ccepr"));
        QVERIFY2(reviewer.workspace->exportProposal(responsePath, &error), qPrintable(error));
        // A reply must not erase edits made locally since the last export.
        author.controller.addNote(Note(9, 0, 1, 400));
        QVERIFY2(author.workspace->importProposal(responsePath, &error), qPrintable(error));
        QCOMPARE(author.workspace->comments().size(), 2);
        bool retainedLocalAddition = false;
        for (const auto &v : author.workspace->revision().value("changes").toArray())
        {
            const auto c = v.toObject();
            retainedLocalAddition |= c.value("kind") == "Add" && c.value("after").toObject().value("x").toInt() == 400;
        }
        QVERIFY(retainedLocalAddition);
    }
    void baseIsReadOnlyAndOpeningProjectStartsNewUndoSession()
    {
        WorkspaceRig rig("readonly");
        QVERIFY(start(rig));
        QString error;
        rig.workspace->setViewMode(2);
        const auto before = rig.workspace->project().toJson();
        auto changed = rig.workspace->project();
        changed.layers.begin()->name = "Forbidden";
        QVERIFY(!rig.workspace->setProject(changed, &error));
        QCOMPARE(rig.workspace->project().toJson(), before);
        const auto count = rig.controller.chart()->notes().size();
        rig.controller.addNote(Note(20, 0, 1, 30));
        QCOMPARE(rig.controller.chart()->notes().size(), count);
        rig.workspace->setViewMode(0);
        const auto path = rig.temp.filePath("reopen.cceproj");
        QVERIFY2(rig.workspace->saveProject(path, &error), qPrintable(error));
        rig.controller.addNote(Note(21, 0, 1, 31));
        QVERIFY(rig.controller.canUndo());
        QVERIFY2(rig.workspace->openProject(path, &error), qPrintable(error));
        QVERIFY(!rig.controller.canUndo());
        QCOMPARE(rig.controller.chart()->notes().size(), count);
    }
    void reopeningEditingProposalKeepsMoveAndRebaseBaseline()
    {
        WorkspaceRig author("persist-proposal");
        QVERIFY(start(author));
        QString error;
        const auto before = author.controller.chart()->notes().first();
        auto after = before;
        after.x = 120;
        author.controller.moveNote(before, after);
        // Saving an unexported draft must persist explicit Move provenance too.
        const auto originalBase = author.workspace->revision().value("baseSnapshot");
        const auto path = author.temp.filePath("editing.cceproj");
        QVERIFY2(author.workspace->saveProject(path, &error), qPrintable(error));
        WorkspaceRig reopened("persist-reopened");
        QVERIFY(reopened.initialize(&error));
        QVERIFY2(reopened.workspace->openProject(path, &error), qPrintable(error));
        QVERIFY2(reopened.workspace->rebase(&error), qPrintable(error));
        QCOMPARE(reopened.workspace->revision().value("baseSnapshot"), originalBase);
        const auto output = reopened.temp.filePath("after.ccepr");
        QVERIFY2(reopened.workspace->exportProposal(output, &error), qPrintable(error));
        const auto changes = packageChanges(output);
        QCOMPARE(changes.size(), 1);
        QCOMPARE(changes.first().toObject().value("kind").toString(), QString("Move"));
    }
    void selectiveApplyCarriesRequiredSoundResourceAndUndoesAtomically()
    {
        WorkspaceRig author("resource-author");
        QVERIFY(start(author));
        QString error;
        QFile resource(author.temp.filePath("hit.wav"));
        QVERIFY(resource.open(QIODevice::WriteOnly));
        const QByteArray bytes("isolated test resource");
        QCOMPARE(resource.write(bytes), bytes.size());
        resource.close();
        author.controller.addNote(Note(8, 0, 1, "hit.wav", 70, 15));
        const auto package = author.temp.filePath("sound.ccepr");
        QVERIFY2(author.workspace->exportProposal(package, &error), qPrintable(error));
        WorkspaceRig reviewer("resource-reviewer");
        QVERIFY(reviewer.initialize(&error));
        QVERIFY2(reviewer.workspace->importProposal(package, &error), qPrintable(error));
        QSet<QString> selected;
        for (const auto &v : reviewer.workspace->revision().value("changes").toArray())
        {
            const auto c = v.toObject();
            if (c.value("domain") == "Note" && c.value("kind") == "Add")
                selected.insert(c.value("changeId").toString());
        }
        QVERIFY(!selected.isEmpty());
        QVERIFY2(reviewer.workspace->applyChanges(selected, &error), qPrintable(error));
        QCOMPARE(reviewer.controller.chart()->notes().size(), 3);
        const auto sound = reviewer.controller.chart()->notes().last();
        QVERIFY(sound.isSound());
        QVERIFY(QFileInfo::exists(sound.sound));
        QFile materialized(sound.sound);
        QVERIFY(materialized.open(QIODevice::ReadOnly));
        QCOMPARE(materialized.readAll(), bytes);
        materialized.close();
        reviewer.controller.undo();
        QCOMPARE(reviewer.controller.chart()->notes().size(), 2);
        reviewer.controller.redo();
        QCOMPARE(reviewer.controller.chart()->notes().size(), 3);
        const auto saved = reviewer.temp.filePath("sound.cceproj");
        QVERIFY2(reviewer.workspace->saveProject(saved, &error), qPrintable(error));
        CceDocument::ProjectBundle bundle;
        QVERIFY2(CceDocument::readProject(saved, bundle, &error), qPrintable(error));
        QVERIFY(bundle.resourcePaths.contains("hit.wav"));
    }
    void failedResourceApplyLeavesChartReviewAndUndoIntact()
    {
        WorkspaceRig author("rollback-author");
        QVERIFY(start(author));
        QString error;
        const QByteArray bytes("rollback sound bytes");
        QFile file(author.temp.filePath("hit.wav"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(bytes);
        file.close();
        author.controller.addNote(Note(8, 0, 1, "hit.wav", 70, 0));
        const auto package = author.temp.filePath("rollback.ccepr");
        QVERIFY(author.workspace->exportProposal(package, &error));
        WorkspaceRig reviewer("rollback-reviewer");
        QVERIFY(reviewer.initialize(&error));
        QVERIFY(reviewer.workspace->importProposal(package, &error));
        QSet<QString> selected;
        for (const auto &v : reviewer.workspace->revision().value("changes").toArray())
            if (v.toObject().value("domain") == "Note")
                selected.insert(v.toObject().value("changeId").toString());
        const auto project = reviewer.workspace->project().toJson();
        const auto revision = reviewer.workspace->revision();
        const auto comments = reviewer.workspace->comments();
        const auto undo = reviewer.controller.nextUndoActionText();
        const auto chart = CceDocument::encodeChart(*reviewer.controller.chart(), true);
        const QString hash = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
        const auto cacheVersion = QString::fromLatin1(CceReview::canonicalHash({{"hit.wav", hash}}));
        const auto cacheRoot = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                               + "/local-review/resources/" + cacheVersion;
        QVERIFY(QDir().mkpath(cacheRoot));
        const auto blockerPath = cacheRoot + "/" + reviewer.workspace->project().projectId;
        QVERIFY(!QFileInfo::exists(blockerPath));
        QFile blocker(blockerPath);
        QVERIFY(blocker.open(QIODevice::WriteOnly));
        blocker.write("blocks materialization");
        blocker.close();
        QVERIFY(!reviewer.workspace->applyChanges(selected, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(reviewer.workspace->project().toJson(), project);
        QCOMPARE(reviewer.workspace->revision(), revision);
        QCOMPARE(reviewer.workspace->comments(), comments);
        QCOMPARE(reviewer.controller.nextUndoActionText(), undo);
        QCOMPARE(CceDocument::encodeChart(*reviewer.controller.chart(), true), chart);
        QVERIFY(QFile::remove(blockerPath));
        QVERIFY2(reviewer.workspace->applyChanges(selected, &error), qPrintable(error));
    }
    void reviewPanelRendersAndKeepsTheViewport()
    {
        WorkspaceRig rig("render");
        QVERIFY(start(rig));
        QString error;
        rig.window.setCentralWidget(&rig.canvas);
        rig.window.resize(1200, 760);
        rig.window.show();
        selectNote(rig, 0);
        QVERIFY(rig.workspace->addCommentAtSelection("Review this position", "Note", &error));
        auto before = rig.controller.chart()->notes().first(), after = before;
        after.x = 320;
        rig.controller.moveNote(before, after);
        QVERIFY2(rig.workspace->exportProposal(rig.temp.filePath("render.ccepr"), &error), qPrintable(error));
        rig.workspace->showPanel();
        auto *commentTree = rig.window.findChild<QTreeWidget *>("reviewComments");
        QVERIFY(commentTree);
        QCOMPARE(commentTree->topLevelItemCount(), 1);
        QVERIFY2(!commentTree->topLevelItem(0)->text(4).isEmpty(),
                 "Changed targets must have a visible reference status");
        const auto json = rig.workspace->project().toJson();
        const auto scroll = rig.canvas.scrollBeat();
        rig.workspace->setViewMode(2);
        QCOMPARE(rig.canvas.scrollBeat(), scroll);
        rig.workspace->setViewMode(1);
        QCOMPARE(rig.canvas.scrollBeat(), scroll);
        QCOMPARE(rig.workspace->project().toJson(), json);
        QApplication::processEvents();
        const auto output = qEnvironmentVariable("CCE_REVIEW_SCREENSHOT_DIR");
        if (!output.isEmpty())
        {
            QVERIFY(QDir().mkpath(output));
            QVERIFY(rig.window.grab().save(QDir(output).filePath("review-canvas.png")));
            auto *panel = rig.window.findChild<QDialog *>("reviewWorkspaceWindow");
            QVERIFY(panel);
            QVERIFY(panel->grab().save(QDir(output).filePath("review-panel.png")));
            auto *tabs = panel->findChild<QTabWidget *>();
            QVERIFY(tabs);
            tabs->setCurrentIndex(2);
            QApplication::processEvents();
            QVERIFY(panel->grab().save(QDir(output).filePath("review-comments.png")));
        }
    }
    void reopenedSoundEditsAndCommentsKeepLogicalResourcePaths()
    {
        WorkspaceRig rig("portable-sound");
        QString error;
        QVERIFY(rig.initialize(&error));
        QFile file(rig.temp.filePath("hit.wav"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("isolated sound");
        file.close();
        rig.controller.addNote(Note(8, 0, 1, "hit.wav", 70, 0));
        const auto project = rig.temp.filePath("sound.cceproj");
        QVERIFY2(rig.workspace->saveProject(project, &error), qPrintable(error));
        QVERIFY2(rig.workspace->openProject(project, &error), qPrintable(error));
        QVERIFY2(rig.workspace->startProposal("Edit sound", &error), qPrintable(error));
        int index = -1;
        for (int i = 0; i < rig.controller.chart()->notes().size(); ++i)
            if (rig.controller.chart()->notes()[i].isSound())
                index = i;
        QVERIFY(index >= 0);
        auto before = rig.controller.chart()->notes()[index], after = before;
        QVERIFY(QDir::isAbsolutePath(before.sound));
        after.vol = 55;
        rig.controller.moveNote(before, after);
        selectNote(rig, index);
        QVERIFY2(rig.workspace->addCommentAtSelection("Sound volume", "Note", &error), qPrintable(error));
        QCOMPARE(
            firstNoteTarget(rig.workspace->comments()[0].toObject()).value("note").toObject().value("sound").toString(),
            QString("hit.wav"));
        const auto package = rig.temp.filePath("sound.ccepr");
        QVERIFY2(rig.workspace->exportProposal(package, &error), qPrintable(error));
        const auto changes = packageChanges(package);
        int found = 0;
        for (const auto &value : changes)
        {
            const auto c = value.toObject();
            if (c.value("domain") != "Note")
                continue;
            ++found;
            QCOMPARE(c.value("kind").toString(), QString("Modify"));
            QCOMPARE(c.value("before").toObject().value("sound").toString(), QString("hit.wav"));
            QCOMPARE(c.value("after").toObject().value("sound").toString(), QString("hit.wav"));
        }
        QCOMPARE(found, 1);
    }
    void contextCommentUsesSelection_data()
    {
        QTest::addColumn<QString>("kind");
        for (const auto &kind : {"Note", "NoteGroup", "Region"})
            QTest::newRow(kind) << QString(kind);
    }
    void contextCommentUsesSelection()
    {
        QFETCH(QString, kind);
        WorkspaceRig rig("selection-comment");
        QVERIFY(rig.initialize());
        rig.canvas.resize(800, 600);
        selectNote(rig, 0);
        if (kind == "NoteGroup")
            rig.selection.addToSelection(1);
        if (kind == "Region")
        {
            rig.selection.clearSelection();
            rig.canvas.setRangeOverlay(6.75, 5.25); // Empty, reversed range.
        }
        QMenu menu;
        rig.canvas.extendContextMenu(&menu, QPointF(500, 400));
        auto *action = menu.findChild<QAction *>("review.context.addComment");
        QVERIFY(action);
        acceptCommentDialog();
        action->trigger();
        QCOMPARE(rig.workspace->comments().size(), 1);
        const auto anchor = rig.workspace->comments().first().toObject().value("anchor").toObject();
        QCOMPARE(anchor.value("type").toString(), kind);
        if (kind == "Region")
        {
            QCOMPARE(anchor.value("beat").toArray(), (QJsonArray{5, 1, 4}));
            QCOMPARE(anchor.value("endBeat").toArray(), (QJsonArray{6, 3, 4}));
            QCOMPARE(anchor.value("x").toInt(), 0);
            QCOMPARE(anchor.value("endX").toInt(), 512);
        }
        else
            QCOMPARE(anchor.value("targets").toArray().size(), kind == "Note" ? 1 : 2);
        rig.controller.undo();
        QVERIFY(rig.workspace->comments().isEmpty());
        rig.controller.redo();
        QCOMPARE(rig.workspace->comments().first().toObject().value("anchor").toObject(), anchor);
    }
    void bpmAndMetadataContextComments()
    {
        WorkspaceRig rig("panel-comments");
        QVERIFY(rig.initialize());
        rig.controller.addBpm(BpmEntry(4, 0, 1, 180));
        BPMTimePanel bpm;
        MetaEditPanel meta;
        bpm.setChartController(&rig.controller);
        meta.setChartController(&rig.controller);
        rig.workspace->installCommentSources(&bpm, &meta);
        auto *list = bpm.findChild<QListWidget *>("bpmEntries");
        QVERIFY(list);
        QCOMPARE(list->selectionMode(), QAbstractItemView::ExtendedSelection);
        list->setCurrentRow(0);
        list->item(list->count() - 1)->setSelected(true);
        QVERIFY(runContextAction([&] { list->customContextMenuRequested(list->visualItemRect(list->item(0)).center()); },
                                 "review.bpm.addComment"));
        auto anchor = rig.workspace->comments().last().toObject().value("anchor").toObject();
        QCOMPARE(anchor.value("type").toString(), QString("BpmGroup"));
        QCOMPARE(anchor.value("bpmList").toArray().size(), 2);
        list->clearSelection();
        QVERIFY(runContextAction([&] { list->customContextMenuRequested(list->visualItemRect(list->item(list->count() - 1)).center()); },
                                 "review.bpm.addComment"));
        anchor = rig.workspace->comments().last().toObject().value("anchor").toObject();
        QCOMPARE(anchor.value("type").toString(), QString("BpmEntry"));
        QCOMPARE(anchor.value("bpm").toObject().value("bpm").toDouble(), 180.0);
        auto *title = meta.findChild<QLabel *>();
        QVERIFY(title);
        QVERIFY(runContextAction([&] { title->customContextMenuRequested(QPoint(1, 1)); }, "review.meta.addComment"));
        QTest::keyPress(&meta, Qt::Key_Control, Qt::ControlModifier);
        auto labels = meta.findChildren<QLabel *>();
        QVERIFY(labels.size() >= 2);
        QVERIFY(runContextAction([&] { labels.at(1)->customContextMenuRequested(QPoint(1, 1)); }, "review.meta.addComment"));
        QTest::keyRelease(&meta, Qt::Key_Control);
        anchor = rig.workspace->comments().last().toObject().value("anchor").toObject();
        QCOMPARE(anchor.value("type").toString(), QString("Metadata"));
        QCOMPARE(anchor.value("fields").toArray().size(), 2);
        const auto path = rig.temp.filePath("panel-comments.cceproj");
        QString error;
        QVERIFY2(rig.workspace->saveProject(path, &error), qPrintable(error));
        WorkspaceRig reopened("panel-comments-reopened");
        QVERIFY(reopened.initialize());
        QVERIFY2(reopened.workspace->openProject(path, &error), qPrintable(error));
        QCOMPARE(reopened.workspace->comments(), rig.workspace->comments());
    }
    void commentAnnotationsAreVisibleAndAuthorColored()
    {
        WorkspaceRig rig("annotation-visuals");
        QVERIFY(rig.initialize());
        rig.canvas.resize(1000, 700);
        rig.canvas.setTimeScale(1);
        rig.canvas.setVerticalFlip(false);
        // Audio zero now sits on the reference line; show the tested 1-9 beats.
        rig.canvas.setScrollPos(4000);
        rig.controller.addNote(Note(6, 1, 2, 8, 1, 2, 256));
        const auto chartBeforeComments = CceDocument::encodeChart(*rig.controller.chart());
        const QPointF target(rig.canvas.chartLaneXToCanvasX(128), rig.canvas.chartBeatToY(5));
        QVERIFY(rig.canvas.rect().contains(target.toPoint()));
        QVERIFY(rig.workspace->addCommentAtSelection("Visible connector", "TimePosition", nullptr, target));
        QImage image(rig.canvas.size(), QImage::Format_ARGB32_Premultiplied);
        rig.canvas.render(&image);
        const QPoint midpoint = (target + QPointF(20, -24)).toPoint();
        int visibleBlue = 0;
        for (int y = midpoint.y() - 2; y <= midpoint.y() + 2; ++y)
            for (int x = midpoint.x() - 2; x <= midpoint.x() + 2; ++x)
            {
                const auto color = image.pixelColor(x, y);
                if (color.blue() > 220 && color.green() > 175 && color.red() > 110)
                    ++visibleBlue;
            }
        QVERIFY2(visibleBlue >= 6, "Connector must remain bright and visibly thicker than one pixel");
        const auto firstBackground = image.pixelColor((target + QPointF(230, -35)).toPoint());
        QString error;
        QVERIFY(rig.workspace->identities()->createIdentity("Second reviewer", &error));
        QVERIFY(rig.workspace->identities()->setActiveIdentity(rig.workspace->identities()->identities().last().authorId, &error));
        const QPointF secondTarget(rig.canvas.chartLaneXToCanvasX(128), rig.canvas.chartBeatToY(9));
        QVERIFY(rig.workspace->addCommentAtSelection("Second author", "TimePosition", nullptr, secondTarget));
        selectNote(rig, 0);
        rig.selection.addToSelection(1);
        QVERIFY(rig.workspace->addCommentAtSelection("Commented note group", "NoteGroup", &error));
        rig.selection.clearSelection();
        rig.canvas.render(&image);
        const auto secondBackground = image.pixelColor((secondTarget + QPointF(230, -35)).toPoint());
        QVERIFY2(firstBackground != secondBackground, "Authors need distinct card backgrounds");
        const QPoint border(qRound(rig.canvas.chartLaneXToCanvasX(64) - Settings::instance().noteSize() / 2.0 - 6),
                           qRound(rig.canvas.chartBeatToY(2)));
        int framePixels = 0;
        for (int y = border.y() - 8; y <= border.y() + 8; ++y)
        {
            const auto color = image.pixelColor(border.x(), y);
            if (color.blue() > 175 && color.green() > 140)
                ++framePixels;
        }
        QVERIFY2(framePixels >= 5, "Note group needs a visible pale blue frame");
        QVERIFY(rig.workspace->addCommentForAnchor("Time point highlight",
            {{"type", "TimePoint"}, {"difficultyId", rig.workspace->project().activeDifficultyId},
             {"beat", QJsonArray{5, 3, 4}}, {"x", 400}}, &error));
        rig.canvas.render(&image);
        const int timelineY = qRound(rig.canvas.chartBeatToY(5.75));
        int timelinePixels = 0;
        for (int x = 60; x < 200; ++x)
        {
            const auto color = image.pixelColor(x, timelineY);
            if (color.blue() > 220 && color.green() > 175 && color.red() > 110)
                ++timelinePixels;
        }
        QVERIFY2(timelinePixels >= 60, "Time point comments need a bright highlighted timeline");
        QCOMPARE(CceDocument::encodeChart(*rig.controller.chart()), chartBeforeComments);
        const auto output = qEnvironmentVariable("CCE_REVIEW_SCREENSHOT_DIR");
        if (!output.isEmpty())
        {
            QDir().mkpath(output);
            QVERIFY(image.save(QDir(output).filePath("comment-annotations.png")));
        }
    }
    void commentCardHasDedicatedContextMenu()
    {
        WorkspaceRig rig("card-menu");
        QVERIFY(rig.initialize());
        rig.canvas.resize(800, 600);
        const QPointF target(rig.canvas.chartLaneXToCanvasX(128), rig.canvas.chartBeatToY(2));
        QVERIFY(rig.workspace->addCommentAtSelection("Menu test", "TimePosition", nullptr, target));
        QImage image(rig.canvas.size(), QImage::Format_ARGB32_Premultiplied);
        rig.canvas.render(&image);
        QRectF card(target + QPointF(40, -78), QSizeF(208, 54));
        card.moveLeft(qBound(4., card.left(), qMax(4., rig.canvas.width() - card.width() - 4.)));
        card.moveTop(qBound(4., card.top(), qMax(4., rig.canvas.height() - card.height() - 4.)));
        const auto open = [&] {
            QMouseEvent press(QEvent::MouseButtonPress, card.center(),
                              QPointF(rig.canvas.mapToGlobal(card.center().toPoint())),
                              Qt::RightButton, Qt::RightButton, Qt::NoModifier);
            QApplication::sendEvent(&rig.canvas, &press);
        };
        QVERIFY(runContextAction(open, "review.comment.resolve"));
        QCOMPARE(rig.workspace->comments().first().toObject().value("state").toString(), QString("Resolved"));
        rig.canvas.render(&image);
        QVERIFY(runContextAction(open, "review.comment.reply"));
        QCOMPARE(rig.workspace->comments().size(), 2);
        QCOMPARE(rig.workspace->comments().last().toObject().value("threadId"),
                 rig.workspace->comments().first().toObject().value("threadId"));
    }
    void foreignCommentMenuPreservesAuthorPermissions()
    {
        WorkspaceRig rig("foreign-card-menu");
        QVERIFY(rig.initialize());
        rig.canvas.resize(800, 600);
        const QPointF target(rig.canvas.chartLaneXToCanvasX(128), rig.canvas.chartBeatToY(2));
        QVERIFY(rig.workspace->addCommentAtSelection("Original author", "TimePosition", nullptr, target));
        QString error;
        QVERIFY(rig.workspace->identities()->createIdentity("Other author", &error));
        QVERIFY(rig.workspace->identities()->setActiveIdentity(rig.workspace->identities()->identities().last().authorId));
        QImage image(rig.canvas.size(), QImage::Format_ARGB32_Premultiplied);
        rig.canvas.render(&image);
        QRectF card(target + QPointF(40, -78), QSizeF(208, 54));
        card.moveLeft(qBound(4., card.left(), qMax(4., rig.canvas.width() - card.width() - 4.)));
        card.moveTop(qBound(4., card.top(), qMax(4., rig.canvas.height() - card.height() - 4.)));
        bool checked = false;
        QTimer::singleShot(0, [&] {
            auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            if (!menu)
                return;
            auto *edit = menu->findChild<QAction *>("review.comment.edit");
            auto *remove = menu->findChild<QAction *>("review.comment.delete");
            auto *reply = menu->findChild<QAction *>("review.comment.reply");
            checked = edit && !edit->isEnabled() && remove && !remove->isEnabled() && reply && reply->isEnabled();
            menu->close();
        });
        QMouseEvent press(QEvent::MouseButtonPress, card.center(), QPointF(rig.canvas.mapToGlobal(card.center().toPoint())),
                          Qt::RightButton, Qt::RightButton, Qt::NoModifier);
        QApplication::sendEvent(&rig.canvas, &press);
        QVERIFY(checked);
        QCOMPARE(rig.workspace->comments().first().toObject().value("body").toString(), QString("Original author"));
    }
    void rightClickUnselectedNoteBindsAndFollows()
    {
        WorkspaceRig rig("right-click-follow");
        QVERIFY(rig.workspace->identities()->createIdentity("Reviewer"));
        QVERIFY(!rig.workspace->projectMode());
        rig.canvas.resize(800, 600);
        rig.canvas.setTimeScale(1);
        rig.canvas.setVerticalFlip(false);
        rig.canvas.setScrollPos(0);
        rig.selection.clearSelection();
        const auto original = rig.controller.chart()->notes().first();
        const QPointF point(rig.canvas.chartLaneXToCanvasX(original.x), rig.canvas.chartBeatToY(original.getStartBeat()));
        QMenu menu;
        rig.canvas.extendContextMenu(&menu, point);
        QCOMPARE(rig.selection.selectedIndices().size(), 1);
        auto *action = menu.findChild<QAction *>("review.context.addComment");
        QVERIFY(action);
        acceptCommentDialog();
        action->trigger();
        QVERIFY(!rig.workspace->projectMode());
        QCOMPARE(rig.workspace->comments().first().toObject().value("anchor").toObject().value("type").toString(), QString("Note"));
        auto moved = original;
        moved.beatNum = 5;
        moved.x = 300;
        rig.controller.moveNote(original, moved);
        auto target = firstNoteTarget(rig.workspace->comments().first().toObject());
        QCOMPARE(target.value("note").toObject().value("beat").toArray(), (QJsonArray{5, 0, 1}));
        QCOMPARE(target.value("note").toObject().value("x").toInt(), 300);
        QCOMPARE(target.value("state").toString(), QString("Modified"));
        QCOMPARE(rig.workspace->comments().first().toObject().value("author").toObject().value("authorNameSnapshot").toString(), QString("Reviewer"));
        rig.controller.undo();
        QCOMPARE(firstNoteTarget(rig.workspace->comments().first().toObject()).value("state").toString(), QString("Valid"));
        rig.controller.redo();
        QCOMPARE(firstNoteTarget(rig.workspace->comments().first().toObject()).value("note").toObject().value("x").toInt(), 300);
    }
    void emptyAreaAndTimeRulerSelection_data()
    {
        QTest::addColumn<bool>("ruler");
        QTest::newRow("empty-spatial-area") << false;
        QTest::newRow("empty-time-range") << true;
    }
    void emptyAreaAndTimeRulerSelection()
    {
        QFETCH(bool, ruler);
        WorkspaceRig rig("empty-drag");
        QVERIFY(rig.initialize());
        rig.canvas.resize(800, 600);
        rig.canvas.setTimeScale(1);
        rig.canvas.setVerticalFlip(false);
        rig.canvas.setScrollPos(0);
        rig.canvas.setMode(ruler ? ChartCanvas::PlaceNote : ChartCanvas::Select);
        const QPoint start(ruler ? 20 : qRound(rig.canvas.chartLaneXToCanvasX(128)), qRound(rig.canvas.chartBeatToY(5)));
        const QPoint end(ruler ? 20 : qRound(rig.canvas.chartLaneXToCanvasX(384)), qRound(rig.canvas.chartBeatToY(7)));
        QTest::mousePress(&rig.canvas, Qt::LeftButton, Qt::NoModifier, start);
        QMouseEvent move(QEvent::MouseMove, QPointF(end), QPointF(rig.canvas.mapToGlobal(end)), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&rig.canvas, &move);
        QTest::mouseRelease(&rig.canvas, Qt::LeftButton, Qt::NoModifier, end);
        QVERIFY(rig.selection.selectedIndices().isEmpty());
        QRectF area;
        QVERIFY(rig.canvas.selectedCommentRegion(area));
        QCOMPARE(area.top(), 5.0);
        QCOMPARE(area.bottom(), 7.0);
        QCOMPARE(qRound(area.left()), ruler ? 0 : 128);
        QCOMPARE(qRound(area.right()), ruler ? 512 : 384);
        QMenu menu;
        rig.canvas.extendContextMenu(&menu, end);
        acceptCommentDialog();
        menu.findChild<QAction *>("review.context.addComment")->trigger();
        const auto anchor = rig.workspace->comments().first().toObject().value("anchor").toObject();
        QCOMPARE(anchor.value("type").toString(), QString("Region"));
        QCOMPARE(anchor.value("beat").toArray(), (QJsonArray{5, 0, 1}));
        QCOMPARE(anchor.value("endBeat").toArray(), (QJsonArray{7, 0, 1}));
    }
    void metadataBodiesAppearWhenSourceTabIsShown()
    {
        WorkspaceRig rig("meta-tab");
        QVERIFY(rig.initialize());
        QTabWidget tabs;
        tabs.resize(700, 650);
        tabs.addTab(new QWidget, "Notes");
        auto *meta = new MetaEditPanel;
        meta->setChartController(&rig.controller);
        tabs.addTab(meta, "Meta");
        rig.workspace->installCommentSources(nullptr, meta);
        QVERIFY(rig.workspace->addCommentForAnchor("Metadata body survives tab switches",
            {{"type", "Metadata"}, {"difficultyId", rig.workspace->project().activeDifficultyId},
             {"fields", QJsonArray{"title"}}, {"metadata", QJsonObject{{"title", "Review test song"}}},
             {"originalMetadata", QJsonObject{{"title", "Review test song"}}}}));
        tabs.show();
        tabs.setCurrentIndex(1);
        QCoreApplication::processEvents();
        auto *body = meta->findChild<QLabel *>("metadataCommentBody");
        QVERIFY(body);
        QVERIFY(body->isVisible());
        QCOMPARE(body->text(), QString("Metadata body survives tab switches"));
        tabs.setCurrentIndex(0);
        QVERIFY(!body->isVisible());
        tabs.setCurrentIndex(1);
        QVERIFY(body->isVisible());
        const auto output = qEnvironmentVariable("CCE_REVIEW_SCREENSHOT_DIR");
        if (!output.isEmpty())
        {
            QDir().mkpath(output);
            QVERIFY(tabs.grab().save(QDir(output).filePath("metadata-comments.png")));
        }
        tabs.hide();
    }
    void ordinaryChartCommentsSaveWithoutProjectMode()
    {
        WorkspaceRig rig("ordinary-review");
        QVERIFY(rig.workspace->identities()->createIdentity("Reviewer"));
        selectNote(rig, 0);
        QVERIFY(rig.workspace->addCommentAtSelection("Saved with the chart", "Note"));
        QVERIFY(!rig.workspace->projectMode());
        const auto path = rig.temp.filePath("saved.mc");
        QVERIFY(rig.controller.saveChart(path));
        QString error;
        QVERIFY2(CceReview::ReviewWorkspace::writeChartReview(path, rig.workspace->chartReviewState(), &error), qPrintable(error));
        WorkspaceRig reopened("ordinary-reopened");
        Chart chart;
        QVERIFY(ChartIO::load(path, chart));
        QVERIFY(reopened.controller.loadChartFromData(path, chart));
        QCOMPARE(reopened.workspace->comments().size(), 1);
        QVERIFY(!reopened.workspace->projectMode());
        QCOMPARE(reopened.workspace->comments().first().toObject().value("body").toString(), QString("Saved with the chart"));
        const auto before = reopened.controller.chart()->notes().first();
        auto after = before;
        after.beatNum = 6;
        reopened.controller.moveNote(before, after);
        QCOMPARE(firstNoteTarget(reopened.workspace->comments().first().toObject()).value("note").toObject().value("beat").toArray(), (QJsonArray{6, 0, 1}));
        // Choosing a project file later is a document decision, not something
        // an older comment's Undo/Redo should silently reverse.
        QVERIFY(rig.workspace->saveProject(rig.temp.filePath("explicit.cceproj"), &error));
        rig.controller.undo();
        QVERIFY(rig.workspace->projectMode());
        rig.controller.redo();
        QVERIFY(rig.workspace->projectMode());
    }
    void mainWindowSaveIncludesCommentsWithoutProjectDialog()
    {
        QTemporaryDir folder;
        const auto path = folder.filePath("ordinary.mc");
        QVERIFY(ChartIO::save(path, sampleChart()));
        ChartController controller;
        SelectionController selection;
        AudioPlayer audio;
        PlaybackController playback(&audio);
        MainWindow window(&controller, &selection, &playback, new Skin);
        window.show();
        QTest::qWait(50); // Finish the constructor's queued recovery/working-copy cleanup before loading.
        QString error;
        QVERIFY2(window.loadChartForAutomation(path, &error), qPrintable(error));
        auto *workspace = window.findChild<CceReview::ReviewWorkspace *>();
        QVERIFY(workspace);
        QVERIFY(workspace->identities()->createIdentity("Chart saver"));
        QVERIFY(workspace->addCommentAtSelection("Saved through Ctrl+S route", "TimePosition"));
        QVERIFY(!workspace->projectMode());
        QVERIFY(QMetaObject::invokeMethod(&window, "saveChart", Qt::DirectConnection));
        const auto sidecar = folder.filePath(".mcce-plugin/ordinary.review.json");
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(sidecar), 15000);
        QFile file(sidecar);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto state = QJsonDocument::fromJson(file.readAll()).object();
        QCOMPARE(state.value("review").toObject().value("comments").toArray().size(), 1);
        QVERIFY(!window.findChild<QFileDialog *>());
        QTest::qWait(30);
        window.hide();
    }
    void emptyTimeRangeInputWorksWithOverlayHidden()
    {
        WorkspaceRig rig("empty-time-input");
        QVERIFY(rig.initialize());
        LongRangeSelector range;
        range.setChartController(&rig.controller);
        range.setSelectionController(&rig.selection);
        connect(&range, &LongRangeSelector::rangeChanged, &rig.canvas, &ChartCanvas::setRangeOverlay);
        connect(&range, &LongRangeSelector::rangeVisibilityChanged, &rig.canvas, &ChartCanvas::setRangeOverlayVisible);
        range.setStartBeat(5.25);
        range.setEndBeat(6.75);
        range.setRangeVisible(false);
        QVERIFY(QMetaObject::invokeMethod(&range, "onSelectClicked", Qt::DirectConnection));
        QVERIFY(rig.selection.selectedIndices().isEmpty());
        double a, b;
        QVERIFY(rig.canvas.selectedBeatRange(a, b));
        QCOMPARE(a, 5.25);
        QCOMPARE(b, 6.75);
        QVERIFY(rig.workspace->addCommentAtSelection("Empty time range", "Region"));
        const auto anchor = rig.workspace->comments().first().toObject().value("anchor").toObject();
        QCOMPARE(anchor.value("beat").toArray(), (QJsonArray{5, 1, 4}));
        QCOMPARE(anchor.value("endBeat").toArray(), (QJsonArray{6, 3, 4}));
    }
    void clickedTimeCommentThickensDivisionLine_data()
    {
        QTest::addColumn<QString>("type");
        QTest::newRow("time-point") << QString("TimePoint");
        QTest::newRow("clicked-position") << QString("TimePosition");
    }
    void clickedTimeCommentThickensDivisionLine()
    {
        QFETCH(QString, type);
        WorkspaceRig rig("thick-timeline");
        QVERIFY(rig.initialize());
        rig.canvas.resize(1000, 700);
        rig.canvas.setTimeScale(1);
        rig.canvas.setVerticalFlip(false);
        rig.canvas.setScrollPos(4000);
        const QPointF target(rig.canvas.chartLaneXToCanvasX(400), rig.canvas.chartBeatToY(5));
        QVERIFY(rig.canvas.rect().contains(target.toPoint()));
        QVERIFY(rig.workspace->addCommentAtSelection("Highlighted reference line", type, nullptr, target));
        QImage image(rig.canvas.size(), QImage::Format_ARGB32_Premultiplied);
        rig.canvas.render(&image);
        const int y = qRound(rig.canvas.chartBeatToY(5));
        for (int offset = -1; offset <= 1; ++offset)
        {
            int bright = 0;
            for (int x = 80; x < 250; ++x)
            {
                const auto pixel = image.pixelColor(x, y + offset);
                if (pixel.blue() > 220 && pixel.green() > 175 && pixel.red() > 110)
                    ++bright;
            }
            QVERIFY2(bright > 150, "The existing division/reference line must become a thick continuous highlight");
        }
    }
    void contextMenuAddsCommentAtClickedPosition()
    {
        WorkspaceRig rig("context");
        QVERIFY(start(rig));
        rig.canvas.resize(640, 600);
        const QPointF position(rig.canvas.chartLaneXToCanvasX(128), rig.canvas.chartBeatToY(2));
        QMenu menu;
        rig.canvas.extendContextMenu(&menu, position);
        auto *action = menu.findChild<QAction *>("review.context.addComment");
        QVERIFY(action);
        QTimer::singleShot(0, [] {
            auto *dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget());
            if (dialog)
            {
                dialog->setTextValue("Comment at pointer");
                dialog->accept();
            }
        });
        action->trigger();
        QCOMPARE(rig.workspace->comments().size(), 1);
        const auto anchor = rig.workspace->comments()[0].toObject().value("anchor").toObject();
        QCOMPARE(anchor.value("type").toString(), QString("TimePosition"));
        QCOMPARE(anchor.value("x").toInt(), 128);
        const auto beat = anchor.value("beat").toArray();
        QCOMPARE(beat[0].toInt() + beat[1].toDouble() / beat[2].toDouble(), 2.0);
    }
    void commentCardDragChangesOnlyDisplayAndDoubleClickOpensComments_data()
    {
        QTest::addColumn<QString>("anchorType");
        QTest::newRow("position") << QString("TimePosition");
        QTest::newRow("time-point") << QString("TimePoint");
    }
    void commentCardDragChangesOnlyDisplayAndDoubleClickOpensComments()
    {
        QFETCH(QString, anchorType);
        WorkspaceRig rig("card");
        QVERIFY(start(rig));
        QString error;
        rig.canvas.resize(640, 600);
        rig.canvas.show();
        const QPointF anchorPoint(rig.canvas.chartLaneXToCanvasX(128), rig.canvas.chartBeatToY(2));
        QVERIFY(rig.workspace->addCommentAtSelection("Card discussion", anchorType, &error, anchorPoint));
        const auto original = rig.workspace->comments()[0].toObject();
        QImage image(rig.canvas.size(), QImage::Format_ARGB32_Premultiplied);
        rig.canvas.render(&image);
        QRectF card(anchorPoint + QPointF(40, -78), QSizeF(208, 54));
        card.moveLeft(qBound(4., card.left(), qMax(4., rig.canvas.width() - card.width() - 4.)));
        card.moveTop(qBound(4., card.top(), qMax(4., rig.canvas.height() - card.height() - 4.)));
        const QPoint point = card.center().toPoint(), delta(30, -20);
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(point), QPointF(rig.canvas.mapToGlobal(point)),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&rig.canvas, &press);
        QMouseEvent move(QEvent::MouseMove, QPointF(point + delta), QPointF(rig.canvas.mapToGlobal(point + delta)),
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&rig.canvas, &move);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(point + delta),
                            QPointF(rig.canvas.mapToGlobal(point + delta)), Qt::LeftButton, Qt::NoButton,
                            Qt::NoModifier);
        QApplication::sendEvent(&rig.canvas, &release);
        QCOMPARE(rig.workspace->comments()[0].toObject().value("anchor"), original.value("anchor"));
        const auto offsetKey = "local-review/commentOffsets/" + original.value("threadId").toString();
        QCOMPARE(QSettings().value(offsetKey).toPointF(), QPointF(delta));
        rig.canvas.render(&image);
        QTest::mouseDClick(&rig.canvas, Qt::LeftButton, Qt::NoModifier, point + delta);
        auto *panel = rig.window.findChild<QDialog *>("reviewWorkspaceWindow");
        QVERIFY(panel && panel->isVisible());
        auto *tree = panel->findChild<QTreeWidget *>("reviewComments");
        QVERIFY(tree && tree->currentItem());
        QCOMPARE(tree->currentItem()->data(0, Qt::UserRole).toString(), original.value("commentId").toString());
        QCOMPARE(panel->findChild<QTabWidget *>()->currentIndex(), 2);
        QSettings().remove(offsetKey);
    }
    void largeLayerReferenceAndCommentPresentation_data()
    {
        QTest::addColumn<int>("noteCount");
        QTest::newRow("5k") << 5000;
        QTest::newRow("20k") << 20000;
        QTest::newRow("100k") << 100000;
    }
    void referenceUsesAudioTimeAndHonorsVisibility()
    {
        WorkspaceRig rig("reference-time");
        QString error;
        QVERIFY(rig.initialize(&error));
        auto project = rig.workspace->project();
        const auto active = project.activeDifficultyId;
        auto &target = project.difficulties[active];
        target.metadata.offset = 100;
        target.timing = {BpmEntry(0, 0, 1, 120), BpmEntry(1, 0, 1, 240)};
        target.oggHash = QString(64, 'a');
        auto meta = target.metadata;
        meta.offset = 200;
        const auto source = project.addDifficulty("Source", meta, {BpmEntry(0, 0, 1, 60)}, target.oggHash);
        const auto layer = project.addLayer(source, "Reference");
        project.layers[layer].notes = {Note(1, 0, 1, 300)};
        QVERIFY(project.addReference(active, source, {}, &error));
        QVERIFY(rig.workspace->setProject(project, &error));
        rig.canvas.resize(800, 600);
        rig.canvas.setScrollPos(800);
        const QPoint point(qRound(rig.canvas.chartLaneXToCanvasX(300)), qRound(rig.canvas.chartBeatToY(2.6)));
        QVERIFY(rig.canvas.rect().contains(point));
        QImage shown(rig.canvas.size(), QImage::Format_ARGB32_Premultiplied);
        rig.canvas.render(&shown);
        project.difficulties[active].references[0].visible = false;
        QVERIFY(rig.workspace->setProject(project, &error));
        QImage hidden(rig.canvas.size(), QImage::Format_ARGB32_Premultiplied);
        rig.canvas.render(&hidden);
        QVERIFY2(shown.pixelColor(point) != hidden.pixelColor(point),
                 "Source beat 1 must be projected to target beat 2.6");
        QCOMPARE(rig.controller.chart()->notes().size(), 2);
        QCOMPARE(rig.workspace->project().layers.value(layer).notes[0].beatNum, 1);
    }
    void recoveryPreservesUnexportedMoveAndComments()
    {
        QVERIFY(QStandardPaths::isTestModeEnabled());
        const auto recovery =
            QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/local-review/recovery.cceproj";
        // Do not parse another run's 100k-note backup while waiting for this
        // small fixture's first recovery write.
        QVERIFY(!QFileInfo::exists(recovery) || QFile::remove(recovery));
        WorkspaceRig author("recovery-author");
        QVERIFY(start(author));
        QString error;
        auto before = author.controller.chart()->notes().first(), after = before;
        after.beatNum = 9;
        after.x = 300;
        author.controller.moveNote(before, after);
        selectNote(author, 1);
        QVERIFY(author.workspace->addCommentAtSelection("Unsent comment", "Note", &error));
        CceDocument::ProjectBundle recovered;
        QTRY_VERIFY_WITH_TIMEOUT(
            CceDocument::readProject(recovery, recovered, &error)
                && recovered.project.projectId == author.workspace->project().projectId
                && hasNoteAt(recovered.project.compose(recovered.project.activeDifficultyId), 9, 300)
                && recovered.review.value("comments").toArray().size() == 1,
            15000);
        WorkspaceRig reopened("recovery-reopened");
        QVERIFY(reopened.initialize(&error));
        QVERIFY2(reopened.workspace->openProject(recovery, &error), qPrintable(error));
        QCOMPARE(reopened.workspace->comments().size(), 1);
        const auto package = reopened.temp.filePath("recovered.ccepr");
        QVERIFY2(reopened.workspace->exportProposal(package, &error), qPrintable(error));
        const auto changes = packageChanges(package);
        QCOMPARE(changes.size(), 1);
        QCOMPARE(changes[0].toObject().value("kind").toString(), QString("Move"));
    }
    void largeLayerReferenceAndCommentPresentation()
    {
        QFETCH(int, noteCount);
        WorkspaceRig rig("large");
        QString error;
        QVERIFY(rig.initialize(&error));
        auto project = rig.workspace->project();
        const auto active = project.activeDifficultyId;
        const auto layer = firstLayerId(project);
        auto &notes = project.layers[layer].notes;
        notes.clear();
        notes.reserve(noteCount);
        for (int i = 0; i < noteCount; ++i)
            notes.append(Note(i, 0, 1, i % 512));
        const auto second = project.addLayer(active, "Second layer");
        project.layers[second].notes = {Note(2, 0, 1, 256)};
        QVERIFY(!second.isEmpty());
        const auto source = project.addDifficulty("Reference", project.difficulties[active].metadata,
                                                  project.difficulties[active].timing);
        const auto referenceLayer = project.addLayer(source, "Reference layer");
        project.layers[referenceLayer].notes = {Note(1, 0, 1, 150), Note(2, 0, 1, 200)};
        QVERIFY(!referenceLayer.isEmpty());
        project.difficulties[active].oggHash = QString(64, 'a');
        project.difficulties[source].oggHash = QString(64, 'a');
        QVERIFY2(project.addReference(active, source, {}, &error), qPrintable(error));
        CceDocument::ProjectBundle bundle;
        bundle.project = project;
        QJsonArray comments;
        for (int i = 0; i < 2000; ++i)
        {
            const auto id = QString::number(i);
            comments.append(CceReview::makeComment(
                id, id, {}, "Comment " + id,
                {{"type", "TimePosition"}, {"difficultyId", active}, {"beat", QJsonArray{i, 0, 1}}, {"x", 128}}));
            comments.append(CceReview::makeComment(id, id + "-reply", {}, "Reply", {}));
        }
        bundle.review["comments"] = comments;
        const auto path = rig.temp.filePath("large.cceproj");
        QVERIFY2(CceDocument::writeProject(path, bundle, &error), qPrintable(error));
        QVERIFY2(rig.workspace->openProject(path, &error), qPrintable(error));
        rig.canvas.resize(1000, 700);
        QImage image(rig.canvas.size(), QImage::Format_ARGB32_Premultiplied);
        QElapsedTimer timer;
        timer.start();
        for (int i = 0; i < 12; ++i)
        {
            rig.canvas.setScrollPos(i * 500.);
            image.fill(Qt::transparent);
            QPainter painter(&image);
            rig.canvas.render(&painter);
        }
        qInfo() << noteCount << "notes with layers, reference and 2000 comment threads: 12 frames in" << timer.elapsed()
                << "ms";
        QVERIFY2(timer.elapsed() < 5000,
                 "Presentation should scale with visible objects, not scan the document per frame");
        auto *inspector = rig.workspace->inspector();
        QVERIFY(inspector);
        selectNote(rig, 0);
        QVERIFY(!inspector->findChildren<QLineEdit *>().isEmpty());
        rig.workspace->showPanel();
        QCOMPARE(rig.workspace->comments().size(), 4000);
        QCOMPARE(rig.controller.chart()->notes().size(), noteCount + 1);
    }
};

QTEST_MAIN(ReviewWorkspaceTests)
#include "review_workspace_tests.moc"
