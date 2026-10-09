#pragma once
#include <QWidget>
#include <QJsonObject>
#include <QHash>
class AnalysisCanvas;
class QTabWidget;
class QTableWidget;
class QPlainTextEdit;
class QLabel;
class QComboBox;
class QCheckBox;
class AnalysisWorkbench : public QWidget
{
    Q_OBJECT
public:
    explicit AnalysisWorkbench(AnalysisCanvas *, QWidget *parent = nullptr);
    void adoptTable(const QString &id, const QString &label, QTableWidget *);
    void setResult(const QJsonObject &);
    void setLevel(int level);
    void showWindowCandidates();
    void showTable(const QString &id);
    QWidget *currentDiagnosticTable() const;
    bool trackVisible(const QString &id) const;
    QJsonObject pairedBaseline() const;
    void selectObject(const QString &trackId, const QString &handle, const QJsonObject &details);
    QJsonObject selectedObject() const
    {
        return m_selectedObject;
    }
    QPlainTextEdit *rawEditor() const
    {
        return m_raw;
    }
    bool loadRecord(const QString &path, QString *error = nullptr);
    QJsonObject comparisonRecord() const;
signals:
    void exportRequested();
    void seekRequested(double milliseconds);
    void rangeRequested(double startMs, double endMs);
    void hypothesisSelected(int index);
    void evidenceSelected(QJsonObject);
    void trackVisibilityChanged();

private:
    void updateCompare();
    QTabWidget *m_tabs, *m_groups[3];
    QPlainTextEdit *m_raw, *m_evidence, *m_loadedRecord, *m_objectDetails;
    QLabel *m_overview, *m_compare, *m_referenceStatus;
    QTableWidget *m_tests;
    QComboBox *m_hypotheses;
    QJsonObject m_result, m_record, m_storedCurrent;
    QHash<QString, QTableWidget *> m_tables;
    QHash<QString, QCheckBox *> m_trackChecks;
    QJsonObject m_selectedObject;
};
