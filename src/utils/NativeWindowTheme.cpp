#include "NativeWindowTheme.h"

#include <QWidget>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
#endif

namespace NativeWindowTheme
{
void apply(QWidget *window, bool dark, bool disableTransitions)
{
#ifdef Q_OS_WIN
    if (!window)
        return;

    const HWND handle = reinterpret_cast<HWND>(window->winId());
    if (!handle)
        return;

    const BOOL darkMode = dark ? TRUE : FALSE;
    // Attribute 20 is used by current Windows 10/11 builds. Attribute 19 is
    // the compatible value used by earlier Windows 10 releases.
    constexpr DWORD immersiveDarkMode = 20;
    constexpr DWORD immersiveDarkModeBefore20H1 = 19;
    if (FAILED(DwmSetWindowAttribute(handle, immersiveDarkMode, &darkMode, sizeof(darkMode))))
    {
        DwmSetWindowAttribute(handle,
                              immersiveDarkModeBefore20H1,
                              &darkMode,
                              sizeof(darkMode));
    }

    if (disableTransitions)
    {
        constexpr DWORD transitionsForceDisabled = 3;
        const BOOL disabled = TRUE;
        DwmSetWindowAttribute(handle,
                              transitionsForceDisabled,
                              &disabled,
                              sizeof(disabled));
    }
#else
    Q_UNUSED(window);
    Q_UNUSED(dark);
    Q_UNUSED(disableTransitions);
#endif
}
}
