#pragma once

class QWidget;

namespace NativeWindowTheme
{
// Applies the editor theme to the operating-system title bar. Floating dock
// windows can also opt out of native transitions so redocking is immediate.
void apply(QWidget *window, bool dark, bool disableTransitions = false);
}
