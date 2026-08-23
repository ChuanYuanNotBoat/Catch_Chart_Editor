#pragma once

class QWidget;
class QColor;

namespace NativeWindowTheme
{
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
