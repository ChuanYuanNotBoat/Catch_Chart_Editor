#pragma once

#include <QByteArray>
#include <QWidget>

class PaneContainer;
class QSplitter;

class WorkbenchLayout : public QWidget
{
    Q_OBJECT
public:
    enum class Part
    {
        Editor,
        PrimarySidebar,
        AuxiliarySidebar,
        BottomPanel
    };

    explicit WorkbenchLayout(QWidget *parent = nullptr);

    QWidget *editorWidget() const { return m_editorWidget; }
    PaneContainer *primarySidebar() const { return m_primarySidebar; }
    PaneContainer *auxiliarySidebar() const { return m_auxiliarySidebar; }
    PaneContainer *bottomPanel() const { return m_bottomPanel; }
    PaneContainer *paneContainer(Part part) const;

    bool setEditorWidget(QWidget *widget);
    bool addPane(Part part,
                 const QString &paneId,
                 QWidget *content,
                 bool visible = true,
                 bool scrollable = true);
    QWidget *takePane(Part part, const QString &paneId);

    QByteArray saveState() const;
    bool restoreState(const QByteArray &state);
    void resetState();

    QSplitter *horizontalSplitter() const { return m_horizontalSplitter; }
    QSplitter *verticalSplitter() const { return m_verticalSplitter; }

private:
    QWidget *m_editorHost = nullptr;
    QWidget *m_editorWidget = nullptr;
    PaneContainer *m_primarySidebar = nullptr;
    PaneContainer *m_auxiliarySidebar = nullptr;
    PaneContainer *m_bottomPanel = nullptr;
    QSplitter *m_horizontalSplitter = nullptr;
    QSplitter *m_verticalSplitter = nullptr;
};
