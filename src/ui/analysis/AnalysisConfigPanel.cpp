#include "AnalysisConfigPanel.h"
#include "analysis/AnalysisConfig.h"
#include <QBoxLayout>
#include <QFormLayout>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QPlainTextEdit>
#include <QFile>
#include <QFileDialog>
#include <QInputDialog>
#include <QStandardPaths>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalBlocker>
#include <QLineEdit>
#include <QFileInfo>
#include <QMessageBox>
#include <QGridLayout>
#include <QSettings>
AnalysisConfigPanel::AnalysisConfigPanel(QWidget *parent, const QString &storeOverride)
    : QWidget(parent), m_config(analysis::defaultConfig()), m_base(m_config), m_storeOverride(storeOverride)
{
    setObjectName("analysis.config");
    auto *root = new QVBoxLayout(this);
    m_presets = new QComboBox;
    m_presets->setObjectName("analysis.profile");
    root->addWidget(m_presets);
    QFile saved(storePath());
    if (saved.exists())
    {
        QJsonParseError parse;
        const bool opened = saved.open(QIODevice::ReadOnly) && saved.size() < 1024 * 1024;
        const auto object = opened ? QJsonDocument::fromJson(saved.readAll(), &parse).object() : QJsonObject();
        m_storeReadable = opened && parse.error == QJsonParseError::NoError
                          && object.value("schemaVersion").toInt() == 1 && object.value("profiles").isObject();
        if (m_storeReadable)
            m_profiles = object.value("profiles").toObject();
        else
            m_notice = tr("The global preset store is unreadable. Saving is disabled to preserve it; current/imported "
                          "settings can still be used.");
    }
    rebuildPresets();
    connect(m_presets, &QComboBox::currentTextChanged, this, &AnalysisConfigPanel::selectPreset);
    auto *buttons = new QGridLayout;
    int buttonIndex = 0;
    auto add = [&](const QString &title, const QString &id, auto fn) {
        auto *b = new QPushButton(title);
        b->setObjectName("analysis.profile." + id);
        buttons->addWidget(b, buttonIndex / 2, buttonIndex % 2);
        ++buttonIndex;
        connect(b, &QPushButton::clicked, this, fn);
    };
    add(tr("Save As…"), "save", [this] {
        savePreset(false);
    });
    add(tr("Copy preset…"), "copy", [this] {
        savePreset(true);
    });
    add(tr("Import…"), "import", [this] {
        importProfile();
    });
    add(tr("Export…"), "export", [this] {
        exportProfile();
    });
    root->addLayout(buttons);
    auto *project = new QVBoxLayout;
    auto *saveProject = new QPushButton(tr("Save project override")),
         *clearProject = new QPushButton(tr("Clear project override"));
    saveProject->setObjectName("analysis.saveProjectProfile");
    clearProject->setObjectName("analysis.clearProjectProfile");
    project->addWidget(saveProject);
    project->addWidget(clearProject);
    root->addLayout(project);
    connect(saveProject, &QPushButton::clicked, this, [this] {
        const auto errors = analysis::validateConfig(m_config);
        if (m_projectPath.isEmpty() || !errors.isEmpty())
        {
            m_notice = tr("Open a chart and provide a valid configuration first.");
            refresh();
            return;
        }
        QJsonObject data = projectBaseMetadata();
        for (auto key : {"stable", "debug"})
        {
            QJsonObject diff;
            auto section = m_config.value(key).toObject(), base = m_base.value(key).toObject();
            for (auto it = section.begin(); it != section.end(); ++it)
                if (it.value() != base.value(it.key()))
                    diff[it.key()] = it.value();
            data[key] = diff;
        }
        QString error;
        if (analysis::saveJson(m_projectPath, data, &error))
        {
            m_project = data;
            m_notice = tr("Project override saved. The .mc chart is unchanged.");
        }
        else
            m_notice = error;
        refresh();
    });
    connect(clearProject, &QPushButton::clicked, this, [this] {
        if (m_projectPath.isEmpty())
            return;
        QJsonObject empty = projectBaseMetadata();
        empty["stable"] = QJsonObject();
        empty["debug"] = QJsonObject();
        QString error;
        if (analysis::saveJson(m_projectPath, empty, &error))
        {
            m_project = {};
            m_notice = tr("Project override cleared.");
            setConfig(m_base);
        }
        else
        {
            m_notice = error;
            refresh();
        }
    });
    auto *reset = new QGridLayout;
    m_sections = new QComboBox;
    for (auto key : {"Tempo", "Phase", "Rhythm", "Windows", "Export"})
        m_sections->addItem(tr(key), QString::fromLatin1(key));
    m_sections->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_sections->setMinimumContentsLength(3);
    reset->addWidget(m_sections, 0, 0);
    auto *section = new QPushButton(tr("Reset section")), *all = new QPushButton(tr("Reset all"));
    section->setObjectName("analysis.resetSection");
    all->setObjectName("analysis.resetAll");
    reset->addWidget(section, 0, 1);
    reset->addWidget(all, 1, 0, 1, 2);
    root->addLayout(reset);
    connect(section, &QPushButton::clicked, this, &AnalysisConfigPanel::resetSection);
    connect(all, &QPushButton::clicked, this, [this] {
        m_notice = tr("Current configuration reset; saved presets and project override are unchanged.");
        setConfig(analysis::defaultConfig());
    });
    auto *form = new QFormLayout;
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    for (const auto &f : analysis::configFields())
    {
        auto *label = new QLabel(tr(f.label));
        label->setWordWrap(true);
        QWidget *editor;
        if (f.boolean)
        {
            auto *check = new QCheckBox;
            editor = check;
            connect(check, &QCheckBox::toggled, this, [this, f](bool value) {
                if (m_refreshing)
                    return;
                auto s = m_config.value(f.debug ? "debug" : "stable").toObject();
                s[f.id] = value;
                m_config[f.debug ? "debug" : "stable"] = s;
                m_notice.clear();
                refresh();
                emit configChanged();
            });
        }
        else
        {
            auto *spin = new QDoubleSpinBox;
            editor = spin;
            spin->setDecimals(f.integer ? 0 : 6);
            spin->setRange(f.minimum, f.maximum);
            spin->setKeyboardTracking(false);
            connect(spin, &QDoubleSpinBox::valueChanged, this, [this, f](double value) {
                if (m_refreshing)
                    return;
                auto s = m_config.value(f.debug ? "debug" : "stable").toObject();
                s[f.id] = value;
                m_config[f.debug ? "debug" : "stable"] = s;
                m_notice.clear();
                refresh();
                emit configChanged();
            });
        }
        const QString id = QString::fromLatin1(f.id);
        editor->setObjectName("analysis.option." + id);
        editor->setToolTip(QString::fromLatin1(f.section) + QStringLiteral(" · ") + id
                           + (f.debug ? tr(" · Core build dependent") : tr(" · stable profile field"))
                           + tr("\nCCE Default: %1 · range %2–%3").arg(f.initial).arg(f.minimum).arg(f.maximum));
        m_editors[id] = editor;
        m_labels[id] = label;
        auto *row = new QWidget;
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        auto *resetField = new QPushButton(tr("Reset"));
        resetField->setObjectName("analysis.reset." + id);
        resetField->setToolTip(tr("Restore this field to the CCE Default"));
        rowLayout->addWidget(editor, 1);
        rowLayout->addWidget(resetField);
        m_rows[id] = row;
        connect(resetField, &QPushButton::clicked, this, [this, f] {
            auto section = m_config.value(f.debug ? "debug" : "stable").toObject();
            section[f.id] = analysis::defaultConfig().value(f.debug ? "debug" : "stable").toObject().value(f.id);
            m_config[f.debug ? "debug" : "stable"] = section;
            refresh();
            emit configChanged();
        });
        form->addRow(label, row);
    }
    root->addLayout(form);
    m_windows = new QPlainTextEdit;
    m_windows->setObjectName("analysis.windowSpecs");
    m_windows->setMaximumHeight(80);
    m_windows->setToolTip(tr("Window duration / hop pairs in seconds, e.g. [[8,4],[24,12],[48,24]]"));
    root->addWidget(m_windows);
    connect(m_windows, &QPlainTextEdit::textChanged, this, [this] {
        if (m_refreshing)
            return;
        auto s = m_config.value("stable").toObject();
        QJsonParseError error;
        auto doc = QJsonDocument::fromJson(m_windows->toPlainText().toUtf8(), &error);
        s["windowSpecs"] = error.error == QJsonParseError::NoError && doc.isArray() ? doc.array() : QJsonArray();
        m_config["stable"] = s;
        const auto errors = analysis::validateConfig(m_config);
        m_status->setText(errors.isEmpty() ? tr("Configuration changed; analyze again.") : errors.join('\n'));
        emit configChanged();
    });
    m_status = new QLabel;
    m_status->setWordWrap(true);
    m_status->setTextFormat(Qt::PlainText);
    m_status->setObjectName("analysis.configStatus");
    root->addWidget(m_status);
    m_differences = new QPlainTextEdit;
    m_differences->setObjectName("analysis.configDifferences");
    m_differences->setReadOnly(true);
    m_differences->setMaximumHeight(140);
    root->addWidget(m_differences);
    root->addStretch();
    refresh();
    if (m_storeOverride.isEmpty())
    {
        QSettings settings("CatchEditor", "CatchChartEditor");
        const auto selected = settings.value("analysisEditor/profileName", "Default").toString();
        if (selected == "Default" || m_profiles.contains(selected))
        {
            const QSignalBlocker block(m_presets);
            m_presets->setCurrentText(selected);
            selectPreset(selected);
        }
    }
}
QJsonObject AnalysisConfigPanel::projectBaseMetadata() const
{
    return {{"schemaVersion", 1},
            {"algorithmVersion", AutoTiming2Bridge::algorithmVersion()},
            {"basePresetName", m_selectedPreset == "Project base (read-only)" ? m_projectBaseName : m_selectedPreset},
            {"basePresetHash", analysis::configHash(m_base)},
            {"basePresetSnapshot", m_base}};
}
void AnalysisConfigPanel::selectPreset(const QString &name)
{
    auto preset = analysis::defaultConfig();
    QStringList report;
    if (name == "Project base (read-only)" && !m_projectBase.isEmpty())
        preset = m_projectBase;
    else if (name != "Default" && !analysis::importConfig(m_profiles.value(name).toObject(), preset, report))
    {
        m_notice = report.join('\n');
        const QSignalBlocker block(m_presets);
        m_presets->setCurrentText(m_selectedPreset);
        refresh();
        return;
    }
    m_base = preset;
    m_selectedPreset = name;
    if (name != "Project base (read-only)")
    {
        m_globalSelectedPreset = name;
        if (m_storeOverride.isEmpty())
        {
            QSettings s("CatchEditor", "CatchChartEditor");
            s.setValue("analysisEditor/profileName", name);
        }
    }
    if (m_storeReadable)
        m_notice = report.join('\n');
    setConfig(analysis::effectiveConfig(m_base, m_project));
}
QString AnalysisConfigPanel::storePath() const
{
    if (!m_storeOverride.isEmpty())
        return m_storeOverride;
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/analysis-profiles.json";
}
void AnalysisConfigPanel::rebuildPresets(const QString &selected)
{
    QSignalBlocker block(m_presets);
    m_presets->clear();
    m_presets->addItem("Default");
    m_presets->addItems(m_profiles.keys());
    if (!m_projectBase.isEmpty())
        m_presets->addItem("Project base (read-only)");
    m_presets->setCurrentText(selected);
}
void AnalysisConfigPanel::setConfig(const QJsonObject &config)
{
    m_config = config;
    m_refreshing = true;
    m_windows->setPlainText(
        QString::fromUtf8(QJsonDocument(m_config.value("stable").toObject().value("windowSpecs").toArray())
                              .toJson(QJsonDocument::Compact)));
    m_refreshing = false;
    refresh();
    emit configChanged();
}
void AnalysisConfigPanel::setLevel(int level)
{
    m_level = level;
    refresh();
}
void AnalysisConfigPanel::refresh()
{
    m_refreshing = true;
    for (const auto &f : analysis::configFields())
    {
        auto *editor = m_editors.value(f.id);
        const auto value = m_config.value(f.debug ? "debug" : "stable").toObject().value(f.id);
        if (auto *check = qobject_cast<QCheckBox *>(editor))
            check->setChecked(value.toBool());
        else
            qobject_cast<QDoubleSpinBox *>(editor)->setValue(value.toDouble());
        bool visible = m_level >= 1 && (!f.debug || m_level >= 2);
        editor->setVisible(visible);
        m_rows.value(f.id)->setVisible(visible);
        m_labels.value(f.id)->setVisible(visible);
    }
    m_windows->setVisible(m_level >= 1);
    if (!m_windows->hasFocus() && analysis::validateConfig(m_config).isEmpty())
        m_windows->setPlainText(
            QString::fromUtf8(QJsonDocument(m_config.value("stable").toObject().value("windowSpecs").toArray())
                                  .toJson(QJsonDocument::Compact)));
    const auto errors = analysis::validateConfig(m_config);
    const bool dirty =
        analysis::configHash(m_config) != analysis::configHash(analysis::effectiveConfig(m_base, m_project));
    QStringList differences;
    auto describe = [](const QJsonValue &v) {
        return QString::fromUtf8(QJsonDocument(QJsonArray{v}).toJson(QJsonDocument::Compact)).mid(1).chopped(1);
    };
    const auto defaults = analysis::defaultConfig();
    for (const auto &field : analysis::configFields())
    {
        if (field.debug && m_level < 2)
            continue;
        const auto section = field.debug ? "debug" : "stable";
        const auto current = m_config.value(section).toObject().value(field.id),
                   initial = defaults.value(section).toObject().value(field.id);
        if (current != initial)
            differences
                << QStringLiteral("%1: %2 → %3").arg(QLatin1String(field.id), describe(initial), describe(current));
    }
    if (m_config.value("stable").toObject().value("windowSpecs")
        != defaults.value("stable").toObject().value("windowSpecs"))
        differences << tr("Analysis window schedule differs from Default.");
    m_differences->setVisible(m_level >= 1);
    m_differences->setPlainText(differences.isEmpty() ? tr("All visible fields use CCE Default values.")
                                                      : tr("Changes from CCE Default:\n") + differences.join('\n'));
    const bool projectActive =
        !m_project.value("stable").toObject().isEmpty() || !m_project.value("debug").toObject().isEmpty();
    m_status->setText((dirty ? tr("Current configuration: modified\n") : tr("Current configuration: saved values\n"))
                      + tr("Default is read-only. Project override: %1").arg(projectActive ? tr("active") : tr("none"))
                      + (m_level >= 2
                             ? tr("\nCore: %1\nSnapshot: %2")
                                   .arg(AutoTiming2Bridge::algorithmVersion(), analysis::configHash(m_config).left(12))
                             : QString())
                      + (errors.isEmpty() ? QString() : "\n" + errors.join('\n'))
                      + (m_notice.isEmpty() ? QString() : "\n" + m_notice));
    m_refreshing = false;
}
void AnalysisConfigPanel::loadProject(const QString &path)
{
    m_projectPath = path.isEmpty() ? QString() : path + ".analysis-config.json";
    m_project = {};
    m_projectBase = {};
    m_projectBaseName.clear();
    m_notice.clear();
    m_base = analysis::defaultConfig();
    QStringList baseReport;
    if (m_globalSelectedPreset != "Default")
        analysis::importConfig(m_profiles.value(m_globalSelectedPreset).toObject(), m_base, baseReport);
    m_selectedPreset = m_globalSelectedPreset;
    QFile file(m_projectPath);
    if (file.open(QIODevice::ReadOnly) && file.size() < 1024 * 1024)
    {
        QJsonParseError parse;
        auto doc = QJsonDocument::fromJson(file.readAll(), &parse);
        auto raw = doc.object();
        auto fail = [&](const QString &message) {
            m_notice = message;
            auto invalid = m_base;
            invalid["configurationError"] = message;
            rebuildPresets(m_selectedPreset);
            setConfig(invalid);
        };
        if (parse.error != QJsonParseError::NoError || !doc.isObject())
        {
            fail(tr("Invalid project analysis configuration. Restore or clear the sidecar before analyzing."));
            return;
        }
        if (raw.contains("basePresetSnapshot"))
        {
            const auto original = raw.value("basePresetSnapshot").toObject();
            if (raw.contains("basePresetHash")
                && raw.value("basePresetHash").toString() != analysis::configHash(original))
            {
                fail(tr("Project base snapshot hash does not match. Restore or clear the sidecar before analyzing."));
                return;
            }
            QJsonObject snapshot;
            QStringList report;
            if (!analysis::importConfig(original, snapshot, report))
            {
                fail(report.join('\n'));
                return;
            }
            const auto name = raw.value("basePresetName").toString("Default");
            auto installed = analysis::defaultConfig();
            QStringList installedReport;
            bool exists = name == "Default"
                          || analysis::importConfig(m_profiles.value(name).toObject(), installed, installedReport);
            m_base = snapshot;
            if (exists && analysis::configHash(installed) == analysis::configHash(snapshot))
                m_selectedPreset = name;
            else
            {
                m_projectBase = snapshot;
                m_projectBaseName = name;
                m_selectedPreset = "Project base (read-only)";
                report << tr("Using the saved project base; installed preset is missing or differs.");
            }
            baseReport += report;
        }
        else if (raw.contains("basePresetName"))
        {
            const auto name = raw.value("basePresetName").toString();
            QStringList report;
            if (name != "Default" && !analysis::importConfig(m_profiles.value(name).toObject(), m_base, report))
            {
                fail(tr("Project base preset is unavailable. Restore its snapshot or select a preset explicitly."));
                return;
            }
            m_selectedPreset = name;
            baseReport += report;
        }
        QJsonObject verified;
        QStringList report;
        if (raw.value("schemaVersion").toInt() != 1 || !raw.value("stable").isObject()
            || !raw.value("debug").isObject())
        {
            fail(tr("Unsupported/incomplete project analysis configuration."));
            return;
        }
        if (analysis::importConfig(analysis::effectiveConfig(m_base, raw), verified, report))
        {
            for (auto key : {"stable", "debug"})
            {
                auto values = raw.value(key).toObject(), valid = verified.value(key).toObject();
                for (auto it = values.begin(); it != values.end();)
                {
                    if (!valid.contains(it.key())
                        || (QString::fromLatin1(key) == "debug"
                            && raw.value("algorithmVersion") != verified.value("algorithmVersion")))
                        it = values.erase(it);
                    else
                        ++it;
                }
                m_project[key] = values;
            }
            m_project["algorithmVersion"] = verified.value("algorithmVersion");
        }
        else
        {
            fail(report.join('\n'));
            return;
        }
        if (raw.value("algorithmVersion").toString() != AutoTiming2Bridge::algorithmVersion())
            report << tr("Project Debug overrides target another Core build and are inactive.");
        m_notice = (baseReport + report).join('\n');
    }
    else if (file.exists())
    {
        auto invalid = m_base;
        invalid["configurationError"] = tr("Project analysis configuration cannot be read (maximum 1 MiB).");
        setConfig(invalid);
        return;
    }
    rebuildPresets(m_selectedPreset);
    setConfig(analysis::effectiveConfig(m_base, m_project));
}
void AnalysisConfigPanel::savePreset(bool duplicate)
{
    if (!m_storeReadable)
    {
        m_notice = tr("Global preset store is unreadable; it will not be overwritten.");
        refresh();
        return;
    }
    const auto value = duplicate ? m_base : m_config;
    const auto errors = analysis::validateConfig(value);
    if (!errors.isEmpty())
    {
        m_notice = errors.join('\n');
        refresh();
        return;
    }
    bool ok = false;
    const auto name = QInputDialog::getText(this, tr("Save analysis preset"), tr("New name (Default is read-only)"),
                                            QLineEdit::Normal, {}, &ok)
                          .trimmed();
    if (!ok || name.isEmpty())
        return;
    if (name == QLatin1String("Default") || name == QLatin1String("Project base (read-only)")
        || m_profiles.contains(name))
    {
        m_notice = tr("Choose an unused preset name.");
        refresh();
        return;
    }
    auto candidate = m_profiles;
    candidate[name] = value;
    QDir().mkpath(QFileInfo(storePath()).absolutePath());
    QString error;
    if (!analysis::saveJson(storePath(), {{"schemaVersion", 1}, {"profiles", candidate}}, &error))
    {
        m_notice = error;
        refresh();
        return;
    }
    m_profiles = candidate;
    m_base = value;
    m_selectedPreset = name;
    m_globalSelectedPreset = name;
    if (m_storeOverride.isEmpty())
    {
        QSettings s("CatchEditor", "CatchChartEditor");
        s.setValue("analysisEditor/profileName", name);
    }
    rebuildPresets(name);
    m_notice = tr("Preset saved.");
    refresh();
}
void AnalysisConfigPanel::importProfile()
{
    auto path = QFileDialog::getOpenFileName(this, tr("Import analysis profile"), {}, tr("JSON (*.json)"));
    if (path.isEmpty())
        return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly) || f.size() > 1024 * 1024)
    {
        m_notice = tr("Cannot read profile (maximum 1 MiB).");
        refresh();
        return;
    }
    QJsonParseError parse;
    auto doc = QJsonDocument::fromJson(f.readAll(), &parse);
    QJsonObject value;
    QStringList report;
    if (parse.error != QJsonParseError::NoError || !doc.isObject())
    {
        m_notice = tr("Invalid profile JSON; configuration unchanged.");
        refresh();
        return;
    }
    if (analysis::importConfig(doc.object(), value, report))
    {
        QStringList changed;
        for (const auto &field : analysis::configFields())
        {
            const auto section = field.debug ? "debug" : "stable";
            if (value.value(section).toObject().value(field.id) != m_config.value(section).toObject().value(field.id))
                changed << QString::fromLatin1(field.id);
        }
        if (value.value("stable").toObject().value("windowSpecs")
            != m_config.value("stable").toObject().value("windowSpecs"))
            changed << "windowSpecs";
        if (QMessageBox::question(
                this, tr("Replace current analysis configuration"),
                tr("%1 field(s) will change:\n%2\n%3\nSaved presets are preserved. This does not start an analysis.")
                    .arg(changed.size())
                    .arg(changed.join(", "), report.join('\n')),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
            != QMessageBox::Yes)
            return;
        m_notice = report.join('\n');
        setConfig(value);
    }
    else
    {
        m_notice = report.join('\n');
        refresh();
    }
}
void AnalysisConfigPanel::exportProfile()
{
    auto errors = analysis::validateConfig(m_config);
    if (!errors.isEmpty())
    {
        m_notice = errors.join('\n');
        refresh();
        return;
    }
    auto path = QFileDialog::getSaveFileName(this, tr("Export effective profile"), "analysis-profile.json",
                                             tr("JSON (*.json)"));
    if (path.isEmpty())
        return;
    QString error;
    m_notice = analysis::saveJson(path, m_config, &error) ? tr("Effective profile exported.") : error;
    refresh();
}
void AnalysisConfigPanel::resetSection()
{
    auto defaults = analysis::defaultConfig();
    for (const auto &f : analysis::configFields())
        if (QString::fromLatin1(f.section) == m_sections->currentData().toString())
        {
            auto key = f.debug ? "debug" : "stable";
            auto s = m_config.value(key).toObject();
            s[f.id] = defaults.value(key).toObject().value(f.id);
            m_config[key] = s;
        }
    if (m_sections->currentIndex() == 3)
    {
        auto s = m_config.value("stable").toObject();
        s["windowSpecs"] = defaults.value("stable").toObject().value("windowSpecs");
        m_config["stable"] = s;
    }
    m_notice = tr("Section reset in current configuration.");
    setConfig(m_config);
}
