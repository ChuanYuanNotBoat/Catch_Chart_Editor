#include "utils/Settings.h"

#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>

#include <cstdio>

namespace
{
bool require(bool condition, const char *message)
{
    if (!condition)
        std::fprintf(stderr, "FAILED: %s\n", message);
    return condition;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir settingsDirectory;
    if (!require(settingsDirectory.isValid(), "temporary settings directory must be available"))
        return 1;

    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       settingsDirectory.path());

    QSettings raw(QSettings::IniFormat, QSettings::UserScope,
                  QStringLiteral("CatchEditor"), QStringLiteral("CatchChartEditor"));
    raw.clear();
    raw.sync();

    Settings &settings = Settings::instance();
    settings.setLanguage(QStringLiteral("zh_CN"));
    settings.setNoteSize(23);
    settings.setPlaybackSpeed(1.5);
    settings.setEditorTimeDivision(12);
    settings.setEditorGridDivision(32);
    settings.setEditorGridSnapEnabled(false);
    settings.setEditorTimeScale(3.5);
    settings.setMirrorAxisX(192);
    settings.setMirrorGuideVisible(true);
    settings.setMirrorPreviewVisible(true);
    settings.setOutlineColor(QColor(12, 34, 56, 78));
    settings.setDisabledPluginIds({QStringLiteral("plugin.alpha"),
                                   QStringLiteral("插件.beta")});
    const QByteArray layoutBytes("layout\0bytes", 12);
    settings.setDockLayoutState(layoutBytes);

    QString error;
    const QString exported = settings.exportTransferText(&error);
    bool ok = true;
    ok &= require(error.isEmpty() && !exported.isEmpty(),
                  "settings export must succeed");
    ok &= require(exported.startsWith(Settings::transferBeginMarker() + QLatin1Char('\n')),
                  "settings export must contain the fixed header");
    ok &= require(exported.trimmed().endsWith(Settings::transferEndMarker()),
                  "settings export must contain the fixed footer");

    settings.setLanguage(QStringLiteral("en_US"));
    settings.setNoteSize(8);
    settings.setPlaybackSpeed(0.25);
    settings.setEditorTimeDivision(4);
    settings.setEditorGridDivision(20);
    settings.setEditorGridSnapEnabled(true);
    settings.setEditorTimeScale(1.0);
    settings.setMirrorAxisX(256);
    settings.setMirrorGuideVisible(false);
    settings.setMirrorPreviewVisible(false);
    settings.setOutlineColor(Qt::red);
    settings.setDisabledPluginIds({});
    settings.setDockLayoutState(QByteArrayLiteral("changed"));
    settings.setShortcut(QStringLiteral("temporary.action"), QKeySequence(QStringLiteral("Ctrl+9")));

    int importedCount = 0;
    ok &= require(settings.importTransferText(exported, &error, &importedCount),
                  "exported settings must import successfully");
    ok &= require(error.isEmpty() && importedCount >= 6,
                  "successful import must report its setting count");
    ok &= require(settings.language() == QLatin1String("zh_CN") && settings.noteSize() == 23,
                  "string and integer settings must round-trip");
    ok &= require(qFuzzyCompare(settings.playbackSpeed(), 1.5),
                  "double settings must round-trip");
    ok &= require(settings.editorTimeDivision() == 12 && settings.editorGridDivision() == 32
                      && !settings.editorGridSnapEnabled(),
                  "grid editor preferences must round-trip");
    ok &= require(qFuzzyCompare(settings.editorTimeScale(), 3.5)
                      && settings.mirrorAxisX() == 192 && settings.mirrorGuideVisible()
                      && settings.mirrorPreviewVisible(),
                  "view and mirror preferences must round-trip");
    ok &= require(settings.outlineColor() == QColor(12, 34, 56, 78),
                  "color settings must round-trip with alpha");
    ok &= require(settings.disabledPluginIds()
                      == QStringList({QStringLiteral("plugin.alpha"), QStringLiteral("插件.beta")}),
                  "string-list settings must round-trip with Unicode");
    ok &= require(settings.dockLayoutState() == layoutBytes,
                  "byte-array settings must round-trip exactly");
    ok &= require(!settings.hasShortcut(QStringLiteral("temporary.action")),
                  "import must replace settings rather than merge them");

    const int noteSizeBeforeInvalidImport = settings.noteSize();
    QString damaged = exported;
    const qsizetype payloadOffset = damaged.indexOf(QLatin1Char('\n')) + 1;
    if (payloadOffset > 0 && payloadOffset < damaged.size())
        damaged[payloadOffset] = damaged[payloadOffset] == QLatin1Char('A')
                                     ? QLatin1Char('B')
                                     : QLatin1Char('A');
    ok &= require(!settings.importTransferText(damaged, &error),
                  "damaged settings text must be rejected");
    ok &= require(!error.isEmpty() && settings.noteSize() == noteSizeBeforeInvalidImport,
                  "a rejected import must not change existing settings");

    ok &= require(!settings.importTransferText(
                      QStringLiteral("not a settings bundle"), &error),
                  "text without the fixed markers must be rejected");
    ok &= require(settings.noteSize() == noteSizeBeforeInvalidImport,
                  "invalid marker rejection must preserve settings");

    if (!ok)
        return 1;
    std::puts("Settings transfer tests passed");
    return 0;
}
