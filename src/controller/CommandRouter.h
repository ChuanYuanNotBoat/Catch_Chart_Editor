#pragma once

#include <QObject>
#include <QHash>
#include <QKeySequence>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QVector>
#include <functional>

class QAction;
class QWidget;

// One registry owns keyboard bindings; QAction remains the menu/button command.
// Keyboard events are dispatched here so Qt widget focus and floating windows do
// not introduce a second, hardcoded path around user overrides.
class CommandRouter : public QObject
{
    Q_OBJECT
public:
    enum class Scope { Window, Editor, Curve, Plugin };
    struct Command
    {
        QString id;
        QPointer<QAction> action;
        QKeySequence defaultBinding;
        Scope scope = Scope::Editor;
    };

    explicit CommandRouter(QWidget *window);
    void clear();
    void registerAction(QAction *action, const QString &id,
                        const QKeySequence &defaultBinding, Scope scope = Scope::Editor);
    void setScopePredicate(std::function<bool(Scope)> predicate);
    const QVector<Command> &commands() const { return m_commands; }
    QAction *action(const QString &id) const;
    bool validateBindings(const QHash<QString, QKeySequence> &bindings,
                          QString *error = nullptr, QString *conflictingId = nullptr) const;
    bool applyBindings(const QHash<QString, QKeySequence> &bindings,
                       QString *error = nullptr, QString *conflictingId = nullptr);
    QString referenceMarkdown() const;
    static QString scopeLabel(Scope scope);

signals:
    void modifierReleased(int key);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    bool ownsWidget(QWidget *widget) const;
    bool allows(Scope scope, QWidget *target) const;
    void resetSequence();

    QPointer<QWidget> m_window;
    QVector<Command> m_commands;
    std::function<bool(Scope)> m_scopePredicate;
    QVector<int> m_pending;
    QTimer m_sequenceTimer;
    QSet<int> m_consumedKeys;
};
