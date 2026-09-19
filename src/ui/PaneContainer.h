#pragma once

#include <QByteArray>
#include <QList>
#include <QStringList>
#include <QWidget>

class QScrollArea;
class QSplitter;

// A stable-ID stack of panes. The container owns the pane hosts while the
// caller keeps ownership of the content widgets through add/takePane.
class PaneContainer : public QWidget
{
    Q_OBJECT
public:
    explicit PaneContainer(const QString &containerId, QWidget *parent = nullptr);

    QString containerId() const { return m_containerId; }

    bool addPane(const QString &paneId,
                 QWidget *content,
                 bool visible = true,
                 bool scrollable = true,
                 bool rememberDefault = true);
    QWidget *takePane(const QString &paneId,
                      bool preserveDefaultOrder = false);
    bool removePane(const QString &paneId);

    bool setPaneVisible(const QString &paneId, bool visible);
    bool setPaneExpanded(const QString &paneId, bool expanded);
    bool movePane(const QString &paneId, int targetIndex);
    bool setPaneSize(const QString &paneId, int size);

    bool containsPane(const QString &paneId) const;
    QStringList paneOrder() const;
    bool paneVisible(const QString &paneId) const;
    bool paneExpanded(const QString &paneId) const;
    int paneSize(const QString &paneId) const;
    bool paneScrollable(const QString &paneId) const;
    bool hasVisiblePanes() const;
    QScrollArea *scrollAreaForPane(const QString &paneId) const;
    QSplitter *splitter() const { return m_splitter; }

    QByteArray saveState() const;
    bool restoreState(const QByteArray &state);
    void resetState();

signals:
    void stateChanged();

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    struct PaneEntry
    {
        QString id;
        QWidget *content = nullptr;
        QWidget *host = nullptr;
        QScrollArea *scrollArea = nullptr;
        bool scrollable = true;
        bool visible = true;
        bool expanded = true;
        int cachedSize = 0;
    };

    int indexOf(const QString &paneId) const;
    void captureSizes() const;
    void applyPaneState();
    void removeHost(PaneEntry &entry, bool deleteContent);

    QString m_containerId;
    QSplitter *m_splitter = nullptr;
    QList<PaneEntry> m_panes;
    QStringList m_defaultOrder;
};
