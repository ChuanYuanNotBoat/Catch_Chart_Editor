#pragma once

#include <QColor>

class QWidget;

namespace NativeWindowTheme
{
struct ThemeColors
{
    QColor window;
    QColor text;
    QColor base;
    QColor button;
    QColor highlight;
    QColor border;
    QColor disabledText;
    bool dark = false;
};

ThemeColors themeColorsFor(const QColor &background);

// Stylesheet for top-level dialogs and message boxes so their labels, inputs,
// tabs and buttons follow the editor theme instead of the Windows system
// palette (native styles ignore QPalette on Windows).
QString dialogStyleSheet(const QColor &background);

// Minimal application-wide stylesheet that forces theme colors onto controls
// that the native Windows style paints without consulting QPalette: message
// boxes, push buttons, check/radio indicators and context menus. Regenerated
// by applySidebarTheme() whenever the theme changes.
QString applicationStyleSheet(const QColor &background);

// Applies the editor theme to the operating-system title bar. Floating dock
// windows can also opt out of native transitions so redocking is immediate.
// The native handle is never created eagerly: attributes are applied when Qt
// naturally creates the window, avoiding a synchronous winId() stall.
void apply(QWidget *window,
           const QColor &captionColor,
           const QColor &textColor,
           const QColor &borderColor,
           bool disableTransitions = false);
}
