#include "file/PluginLoader.h"
#include "plugin/ExternalProcessPlugin.h"
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

class CtbProcessPluginTests : public QObject
{
    Q_OBJECT

    QVector<PluginInterface *> m_plugins;
    ExternalProcessPlugin *m_plugin = nullptr;

    static bool writeFile(const QString &path, const QByteArray &data)
    {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
    }

    bool runAction(const QVariantMap &context, bool &called)
    {
        QEventLoop loop;
        bool success = false;
        called = false;
        QTimer timer;
        timer.setSingleShot(true);
        connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        const auto id = m_plugin->runToolActionAsync(
            QStringLiteral("convert_current_catch_to_osz"), context, &loop,
            [&](bool ok) { called = true; success = ok; loop.quit(); });
        if (id) {
            timer.start(5000);
            loop.exec();
            if (!called)
                m_plugin->cancelAsyncRequest(id);
        }
        return success;
    }

private slots:
    void initTestCase()
    {
        m_plugins = PluginLoader::loadPlugins(QString::fromUtf8(CTB_PLUGIN_RUNTIME_DIR));
        QCOMPARE(m_plugins.size(), 1);
        m_plugin = dynamic_cast<ExternalProcessPlugin *>(m_plugins.first());
        QVERIFY(m_plugin);
        QCOMPARE(m_plugin->pluginId(), QStringLiteral("tool.malodycatch2ctb.d"));
        QVERIFY(m_plugin->initialize(nullptr));
        QTest::qWait(ExternalProcessPlugin::kPostStartRefreshDelayMs);
        const auto actions = m_plugin->toolActions();
        QCOMPARE(actions.size(), 1);
        QCOMPARE(actions.first().actionId, QStringLiteral("convert_current_catch_to_osz"));
        QVERIFY(!actions.first().requiresUndoSnapshot);
    }

    void exportLatestWorkingCopyBesideSource()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString workingDir = temp.filePath(QStringLiteral("session"));
        const QString sourceDir = temp.filePath(QStringLiteral("用户谱面"));
        QVERIFY(QDir().mkpath(workingDir));
        QVERIFY(QDir().mkpath(sourceDir));
        const QString source = sourceDir + QStringLiteral("/original.mc");
        const QString working = workingDir + QStringLiteral("/working.mc");
        const QByteArray original = "saved source stays unchanged";
        QVERIFY(writeFile(source, original));
        QVERIFY(writeFile(working, R"({"meta":{"mode":3,"song":{"title":"最新谱面"}},
            "time":[{"beat":[0,0,1],"bpm":120}],
            "note":[{"type":1,"beat":[0,0,1],"sound":"track.ogg"},
                    {"type":0,"beat":[1,0,1],"x":256}]})"));
        QVERIFY(writeFile(workingDir + QStringLiteral("/track.ogg"), "fixture"));
        const QVariantMap context{{QStringLiteral("chart_path"), working},
                                  {QStringLiteral("chart_path_source"), source}};
        bool called = false;
        QVERIFY(runAction(context, called));
        QVERIFY(called);
        const QDir output(sourceDir + QStringLiteral("/ctb-output"));
        QVERIFY(output.exists(QStringLiteral("最新谱面.osz")));
        QVERIFY(!QDir(workingDir).exists(QStringLiteral("ctb-output")));
        QVERIFY(runAction(context, called));
        QVERIFY(called);
        QCOMPARE(output.entryList({QStringLiteral("*.osz")}, QDir::Files).size(), 2);
        QFile sourceFile(source);
        QVERIFY(sourceFile.open(QIODevice::ReadOnly));
        QCOMPARE(sourceFile.readAll(), original);
    }

    void missingChartReturnsFailure()
    {
        bool called = false;
        QVERIFY(!runAction({}, called));
        QVERIFY(called);
    }

    void cleanupTestCase()
    {
        if (m_plugin)
            m_plugin->shutdown();
        PluginLoader::unloadPlugins(m_plugins);
    }
};

QTEST_GUILESS_MAIN(CtbProcessPluginTests)
#include "ctb_process_plugin_tests.moc"
