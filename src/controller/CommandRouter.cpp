#include "CommandRouter.h"
#include "utils/Settings.h"

#include <QAction>
#include <QApplication>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QDialog>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QWidget>
#include <utility>

namespace
{
int normalizeChord(int chord)
{
    chord &= ~static_cast<int>(Qt::KeypadModifier);
    if ((chord & ~static_cast<int>(Qt::KeyboardModifierMask)) == Qt::Key_Enter)
        chord = (chord & static_cast<int>(Qt::KeyboardModifierMask)) | Qt::Key_Return;
    return chord;
}

QVector<int> chords(const QKeySequence &binding)
{
    QVector<int> result;
    for (int i = 0; i < binding.count(); ++i)
        result.append(normalizeChord(binding[i].toCombined()));
    return result;
}

bool prefix(const QVector<int> &left, const QVector<int> &right)
{
    if (left.isEmpty() || left.size() > right.size())
        return false;
    for (qsizetype i = 0; i < left.size(); ++i)
        if (left[i] != right[i])
            return false;
    return true;
}

bool overlap(CommandRouter::Scope left, CommandRouter::Scope right)
{
    using Scope = CommandRouter::Scope;
    return !((left == Scope::Curve && right == Scope::Plugin) ||
             (left == Scope::Plugin && right == Scope::Curve));
}

bool isTextInput(QWidget *widget)
{
    for (QWidget *current = widget; current; current = current->parentWidget())
        if (qobject_cast<QLineEdit *>(current) || qobject_cast<QTextEdit *>(current) ||
            qobject_cast<QPlainTextEdit *>(current) || qobject_cast<QAbstractSpinBox *>(current) ||
            qobject_cast<QComboBox *>(current))
            return true;
    return false;
}

QString commandLabel(const CommandRouter::Command &command)
{
    QString label = command.action ? command.action->text() : command.id;
    return label.remove('&');
}
}

CommandRouter::CommandRouter(QWidget *window) : QObject(window), m_window(window)
{
    m_sequenceTimer.setSingleShot(true);
    m_sequenceTimer.setInterval(1000);
    connect(&m_sequenceTimer, &QTimer::timeout, this, &CommandRouter::resetSequence);
    connect(qApp, &QApplication::focusChanged, this, [this]() { resetSequence(); });
    qApp->installEventFilter(this);
}

void CommandRouter::resetSequence()
{
    m_pending.clear();
    m_sequenceTimer.stop();
}

void CommandRouter::clear()
{
    resetSequence();
    for (const Command &command : m_commands)
        if (command.action)
        {
            command.action->setShortcut({});
            if (command.action->property("canvasCommand").toBool())
                delete command.action;
        }
    m_commands.clear();
}

void CommandRouter::registerAction(QAction *action, const QString &id,
                                   const QKeySequence &defaultBinding, Scope scope)
{
    if (!action || id.isEmpty())
        return;
    action->setProperty("commandId", id);
    // Menus retain their shortcut labels. Only this router dispatches editor keys;
    // WidgetShortcut prevents Qt from stealing keys from dialog-local shortcuts.
    action->setShortcutContext(Qt::WidgetShortcut);
    const Settings &settings = Settings::instance();
    action->setShortcut(settings.hasShortcut(id) ? settings.shortcut(id) : defaultBinding);
    m_commands.append({id, action, defaultBinding, scope});
}

void CommandRouter::setScopePredicate(std::function<bool(Scope)> predicate)
{
    m_scopePredicate = std::move(predicate);
}

QAction *CommandRouter::action(const QString &id) const
{
    for (const Command &command : m_commands)
        if (command.id == id)
            return command.action;
    return nullptr;
}

bool CommandRouter::validateBindings(const QHash<QString, QKeySequence> &bindings,
                                     QString *error, QString *conflictingId) const
{
    for (qsizetype i = 0; i < m_commands.size(); ++i)
    {
        const Command &left = m_commands[i];
        if (!left.action)
            continue;
        const QKeySequence leftBinding = bindings.value(left.id, left.action->shortcut());
        const QVector<int> leftKeys = chords(leftBinding);
        for (qsizetype j = i + 1; j < m_commands.size(); ++j)
        {
            const Command &right = m_commands[j];
            if (!right.action || !overlap(left.scope, right.scope))
                continue;
            const QVector<int> rightKeys = chords(bindings.value(right.id, right.action->shortcut()));
            if (prefix(leftKeys, rightKeys) || prefix(rightKeys, leftKeys))
            {
                if (error)
                    *error = tr("Shortcut conflict (including sequence prefixes): %1 and %2.")
                                 .arg(commandLabel(left), commandLabel(right));
                if (conflictingId)
                    *conflictingId = right.id;
                return false;
            }
        }
    }
    return true;
}

bool CommandRouter::applyBindings(const QHash<QString, QKeySequence> &bindings,
                                  QString *error, QString *conflictingId)
{
    if (!validateBindings(bindings, error, conflictingId))
        return false;
    resetSequence();
    for (const Command &command : m_commands)
        if (command.action && bindings.contains(command.id))
        {
            const QKeySequence binding = bindings.value(command.id);
            Settings::instance().setShortcut(command.id, binding);
            command.action->setShortcut(binding);
        }
    return true;
}

QString CommandRouter::scopeLabel(Scope scope)
{
    switch (scope)
    {
    case Scope::Window: return tr("Editor window");
    case Scope::Editor: return tr("Editing (outside text inputs)");
    case Scope::Curve: return tr("Native curve tool");
    case Scope::Plugin: return tr("Process plugin tool");
    }
    return {};
}

QString CommandRouter::referenceMarkdown() const
{
    QString result = tr("| Command | Current shortcut | Default | Scope |\n| --- | --- | --- | --- |\n");
    for (const Command &command : m_commands)
        if (command.action)
        {
            auto escape = [](QString text) { return text.replace('|', "\\|").replace('\n', ' '); };
            const QString binding = command.action->shortcut().isEmpty()
                                        ? tr("Disabled") : command.action->shortcut().toString();
            result += QStringLiteral("| %1 | %2 | %3 | %4 |\n")
                          .arg(escape(commandLabel(command)), escape(binding),
                               escape(command.defaultBinding.toString()), escape(scopeLabel(command.scope)));
        }
    return result;
}

bool CommandRouter::ownsWidget(QWidget *widget) const
{
    for (QObject *current = widget; current; current = current->parent())
        if (current == m_window)
            return true;
    return false;
}

bool CommandRouter::allows(Scope scope, QWidget *target) const
{
    if (!ownsWidget(target) || QApplication::activeModalWidget() || QApplication::activePopupWidget())
        return false;
    if (target->window() != m_window && target->window()->property("cceOwnCommandRouter").toBool())
        return false;
    for (QWidget *current = target; current && current != m_window; current = current->parentWidget())
        if (qobject_cast<QDialog *>(current))
            return false;
    if (scope != Scope::Window && isTextInput(target))
        return false;
    return !m_scopePredicate || m_scopePredicate(scope);
}

bool CommandRouter::eventFilter(QObject *watched, QEvent *event)
{
    // QAction keeps its displayed shortcut, but has no independent keyboard path.
    if (event->type() == QEvent::Shortcut)
        for (const Command &command : m_commands)
            if (command.action == watched)
                return true;
    if (event->type() != QEvent::ShortcutOverride && event->type() != QEvent::KeyPress &&
        event->type() != QEvent::KeyRelease)
        return QObject::eventFilter(watched, event);
    QWidget *target = qobject_cast<QWidget *>(watched);
    if (!target || !ownsWidget(target))
        return QObject::eventFilter(watched, event);
    // An ignored key propagates through parents; its editing context must remain
    // the focused input/dialog rather than becoming the parent editor window.
    if (QWidget *focused = QApplication::focusWidget(); focused && ownsWidget(focused))
        target = focused;
    auto *keyEvent = static_cast<QKeyEvent *>(event);
    const int key = keyEvent->key();
    if (event->type() == QEvent::KeyRelease)
    {
        if (key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Alt ||
            key == Qt::Key_AltGr || key == Qt::Key_Meta)
            emit modifierReleased(key);
        return m_consumedKeys.remove(key);
    }
    if (key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Alt || key == Qt::Key_Meta)
        return false;
    const int chord = normalizeChord(keyEvent->keyCombination().toCombined());
    QVector<int> attempted = m_pending;
    attempted.append(chord);
    if (event->type() == QEvent::ShortcutOverride)
    {
        for (const Command &command : m_commands)
            if (command.action && allows(command.scope, target) &&
                (prefix(attempted, chords(command.action->shortcut())) ||
                                   prefix({chord}, chords(command.action->shortcut()))))
            {
                keyEvent->accept();
                return true;
            }
        return false;
    }

    QAction *matched = nullptr;
    int fullMatches = 0;
    bool partialMatch = false;
    for (const Command &command : m_commands)
    {
        if (!command.action || !allows(command.scope, target))
            continue;
        const QVector<int> keys = chords(command.action->shortcut());
        if (prefix(attempted, keys))
        {
            if (attempted.size() == keys.size())
            {
                matched = command.action;
                ++fullMatches;
            }
            else
                partialMatch = true;
        }
    }
    if (fullMatches || partialMatch)
    {
        m_consumedKeys.insert(key);
        if (fullMatches)
        {
            resetSequence();
            // Imported conflicting settings fail closed until the user resolves them.
            if (fullMatches == 1 && !partialMatch && matched->isEnabled() &&
                (!keyEvent->isAutoRepeat() || matched->autoRepeat()))
                matched->trigger();
        }
        else if (!keyEvent->isAutoRepeat())
        {
            m_pending = attempted;
            m_sequenceTimer.start();
        }
        keyEvent->accept();
        return true;
    }
    if (!m_pending.isEmpty())
    {
        resetSequence();
        m_consumedKeys.insert(key);
        return true;
    }
    // Old default keys must not fall through to raw plugin key handlers or widget
    // navigation after remapping/disabling; native text controls still get them.
    for (const Command &command : m_commands)
        if (allows(command.scope, target) && prefix({chord}, chords(command.defaultBinding)))
        {
            m_consumedKeys.insert(key);
            keyEvent->accept();
            return true;
        }
    return false;
}
