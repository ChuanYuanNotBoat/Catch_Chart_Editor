#include "NativeWindowTheme.h"

#include <QColor>
#include <QCoreApplication>
#include <QEvent>
#include <QObject>
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

namespace
{
#ifdef Q_OS_WIN
constexpr auto captionColorProperty = "_mcce_native_caption_color";
constexpr auto textColorProperty = "_mcce_native_text_color";
constexpr auto borderColorProperty = "_mcce_native_border_color";
constexpr auto transitionsProperty = "_mcce_native_disable_transitions";
constexpr auto watchedProperty = "_mcce_native_theme_watched";

void applyToExistingHandle(QWidget *window)
{
    if (!window)
        return;

    // internalWinId() only returns an existing handle. Unlike winId(), it
    // never forces native window creation during application startup.
    const WId windowId = window->internalWinId();
    if (!windowId)
        return;

    const HWND handle = reinterpret_cast<HWND>(windowId);
    const QColor captionColor = window->property(captionColorProperty).value<QColor>();
    const QColor textColor = window->property(textColorProperty).value<QColor>();
    const QColor borderColor = window->property(borderColorProperty).value<QColor>();
    if (!captionColor.isValid() || !textColor.isValid() || !borderColor.isValid())
        return;

    const BOOL darkMode = textColor.lightnessF() > captionColor.lightnessF() ? TRUE : FALSE;
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

    // Windows 11 exposes the caption, text and one-pixel frame separately.
    // Earlier Windows versions simply ignore these unsupported attributes.
    constexpr DWORD borderColorAttribute = 34;
    constexpr DWORD captionColorAttribute = 35;
    constexpr DWORD textColorAttribute = 36;
    const COLORREF nativeBorder = RGB(borderColor.red(), borderColor.green(), borderColor.blue());
    const COLORREF nativeCaption = RGB(captionColor.red(), captionColor.green(), captionColor.blue());
    const COLORREF nativeText = RGB(textColor.red(), textColor.green(), textColor.blue());
    DwmSetWindowAttribute(handle, borderColorAttribute, &nativeBorder, sizeof(nativeBorder));
    DwmSetWindowAttribute(handle, captionColorAttribute, &nativeCaption, sizeof(nativeCaption));
    DwmSetWindowAttribute(handle, textColorAttribute, &nativeText, sizeof(nativeText));

    if (window->property(transitionsProperty).toBool())
    {
        constexpr DWORD transitionsForceDisabled = 3;
        const BOOL disabled = TRUE;
        DwmSetWindowAttribute(handle,
                              transitionsForceDisabled,
                              &disabled,
                              sizeof(disabled));
    }
}

class NativeWindowThemeFilter final : public QObject
{
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::Show || event->type() == QEvent::WinIdChange)
            applyToExistingHandle(qobject_cast<QWidget *>(watched));
        return false;
    }
};

NativeWindowThemeFilter *themeFilter()
{
    static NativeWindowThemeFilter *filter =
        new NativeWindowThemeFilter(QCoreApplication::instance());
    return filter;
}
#endif
}

namespace NativeWindowTheme
{
void apply(QWidget *window,
           const QColor &captionColor,
           const QColor &textColor,
           const QColor &borderColor,
           bool disableTransitions)
{
#ifdef Q_OS_WIN
    if (!window)
        return;

    window->setProperty(captionColorProperty, captionColor);
    window->setProperty(textColorProperty, textColor);
    window->setProperty(borderColorProperty, borderColor);
    window->setProperty(transitionsProperty, disableTransitions);
    if (!window->property(watchedProperty).toBool())
    {
        window->setProperty(watchedProperty, true);
        window->installEventFilter(themeFilter());
    }
    applyToExistingHandle(window);
#else
    Q_UNUSED(window);
    Q_UNUSED(captionColor);
    Q_UNUSED(textColor);
    Q_UNUSED(borderColor);
    Q_UNUSED(disableTransitions);
#endif
}
}
