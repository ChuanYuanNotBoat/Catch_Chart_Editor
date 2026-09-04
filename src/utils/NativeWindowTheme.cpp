#include "NativeWindowTheme.h"

#include <QColor>
#include <QCoreApplication>
#include <QEvent>
#include <QObject>
#include <QWidget>
#include <cmath>

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
constexpr auto kMinContrastForDarkText = 0.5;

// Calculate relative luminance using proper sRGB linearization (WCAG formula)
double linearizedChannel(int value)
{
    const double normalized = value / 255.0;
    return normalized <= 0.03928
               ? normalized / 12.92
               : std::pow((normalized + 0.055) / 1.055, 2.4);
}

double relativeLuminance(const QColor &color)
{
    return 0.2126 * linearizedChannel(color.red()) +
           0.7152 * linearizedChannel(color.green()) +
           0.0722 * linearizedChannel(color.blue());
}

QColor chooseTextColorFor(const QColor &background)
{
    if (!background.isValid())
        return Qt::white;

    return relativeLuminance(background) >= kMinContrastForDarkText ? QColor(20, 20, 20) : QColor(245, 245, 245);
}

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
ThemeColors themeColorsFor(const QColor &background)
{
    const QColor text = chooseTextColorFor(background);
    // dark = true means light text (lightness > 128), which corresponds to a dark background
    const bool dark = text.lightness() > 128;
    const QColor window = dark ? background.lighter(108) : background.darker(103);
    const QColor base = dark ? window.lighter(120) : window.darker(105);
    const QColor button = dark ? window.lighter(132) : window.darker(112);
    const QColor border = dark ? window.lighter(165) : window.darker(145);
    const QColor highlight = dark ? button.lighter(120) : button.lighter(108);
    const QColor disabledText = dark ? QColor("#9A9A9A") : QColor("#707070");

    return ThemeColors{window, text, base, button, highlight, border, disabledText, dark};
}

QString dialogStyleSheet(const QColor &background)
{
    const auto theme = themeColorsFor(background);
    const QColor selectedTabBg = theme.dark ? theme.button.lighter(120) : theme.button.darker(110);

    return QString(
               "QDialog { background-color: %1; color: %2; }"
               "QLabel, QCheckBox, QRadioButton, QGroupBox { color: %2; }"
               "QLineEdit, QAbstractSpinBox, QComboBox, QTextEdit, QPlainTextEdit, QTextBrowser {"
               "  background-color: %3; color: %2; border: 1px solid %4; }"
               "QPushButton, QDialogButtonBox QPushButton, QMessageBox QPushButton { background-color: %5; color: %2; border: 1px solid %4; padding: 3px 8px; }"
               "QPushButton:disabled, QDialogButtonBox QPushButton:disabled, QMessageBox QPushButton:disabled { color: %6; }"
               "QPushButton:default, QDialogButtonBox QPushButton:default, QMessageBox QPushButton:default { background-color: %7; border-color: %4; }"
               "QGroupBox { border: 1px solid %4; margin-top: 8px; padding-top: 10px; }"
               "QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; color: %2; }"
               "QTabWidget::pane { border: 1px solid %4; }"
               "QTabBar::tab { background-color: %3; color: %2; padding: 4px 12px; border: 1px solid %4; }"
               "QTabBar::tab:selected { background-color: %7; color: %2; border: 1px solid %4; }"
               "QTreeWidget { background-color: %3; color: %2; border: 1px solid %4; }"
               "QTableWidget { background-color: %3; color: %2; border: 1px solid %4; }"
               "QHeaderView::section { background-color: %5; color: %2; border: 1px solid %4; padding: 4px; }")
            .arg(theme.window.name(), theme.text.name(), theme.base.name(), theme.border.name(),
                 theme.button.name(), theme.disabledText.name(), selectedTabBg.name());
}

QString applicationStyleSheet(const QColor &background)
{
    const auto theme = themeColorsFor(background);
    const QColor buttonPressed = theme.dark ? theme.button.darker(115) : theme.button.darker(118);

    return QString(
               "QMessageBox { background-color: %1; color: %2; }"
               "QMessageBox QLabel { color: %2; }"
               "QPushButton { background-color: %3; color: %2; border: 1px solid %4; border-radius: 4px; padding: 3px 10px; }"
               "QPushButton:hover { background-color: %5; }"
               "QPushButton:pressed { background-color: %6; }"
               "QPushButton:disabled { color: %7; }"
               "QRadioButton::indicator, QCheckBox::indicator { width: 13px; height: 13px; border: 1px solid %4; border-radius: 3px; background-color: %8; }"
               "QRadioButton::indicator { border-radius: 7px; }"
               "QRadioButton::indicator:checked, QCheckBox::indicator:checked { background-color: %5; border: 1px solid %2; }"
               "QMenu { background-color: %1; color: %2; border: 1px solid %4; }"
               "QMenu::item { padding: 4px 24px 4px 12px; }"
               "QMenu::item:selected { background-color: %3; }"
               "QMenu::item:disabled { color: %7; }"
               "QMenu::separator { height: 1px; background-color: %4; margin: 3px 6px; }")
            .arg(theme.window.name(), theme.text.name(), theme.button.name(), theme.border.name(),
                 theme.highlight.name(), buttonPressed.name(), theme.disabledText.name(), theme.base.name());
}

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
