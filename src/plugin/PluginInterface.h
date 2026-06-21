#pragma once

#include <QString>
#include <QStringList>
#include <QList>
#include <QVariantMap>
#include <QWidget>
#include <QColor>
#include <QPointF>
#include <QRectF>
#include <QtGlobal>
#include "model/Note.h"

class PluginInterface
{
public:
    struct ToolAction
    {
        QString actionId;
        QString title;
        QString description;
        QString confirmMessage;
        QString hostAction;               // "" | undo | redo
        QString placement = "tools_menu"; // tools_menu | top_toolbar | left_sidebar | right_note_panel | plugin_context_menu
        bool requiresUndoSnapshot = true;
        bool checkable = false;
        bool checked = false;
        bool syncPluginToolModeWithChecked = false;
    };
    struct FloatingPanelDescriptor
    {
        QString panelId;
        QString title;
        QString description;
        QString panelRole;      // primary | secondary | library
        QString dockPreference; // left | right | bottom | float
    };
    struct CanvasOverlayItem
    {
        enum Kind
        {
            Line,
            Rect,
            Text
        };
        Kind kind = Line;
        QPointF from;
        QPointF to;
        QRectF rect;
        QString text;
        QColor color = QColor(255, 0, 0, 220);
        QColor fillColor = QColor(255, 0, 0, 40);
        double width = 1.5;
        int fontPx = 12;
        bool chartSpace = false; // When true, use chart-space coordinates (lane_x, beat).
        QPointF chartFrom;       // x=lane_x, y=beat.
        QPointF chartTo;         // x=lane_x, y=beat (for line end).
        bool rectCenterOnChartPoint = true;
        bool noteSnapReference = false; // Generic host hint: note dragging may snap to this chart-space line.
    };
    struct BatchEdit
    {
        QVector<Note> notesToAdd;
        QVector<Note> notesToRemove;
        QList<QPair<Note, Note>> notesToMove;
    };
    struct CanvasInputEvent
    {
        QString type; // mouse_down|mouse_move|mouse_up|wheel|key_down|key_up|focus_in|focus_out|cancel
        double x = 0.0;
        double y = 0.0;
        int button = 0;
        int buttons = 0;
        int modifiers = 0;
        bool shiftDown = false;
        bool ctrlDown = false;
        double wheelDelta = 0.0;
        int key = 0;
        qint64 timestampMs = 0;
    };
    struct CanvasInputResult
    {
        bool consumed = false;
        QList<CanvasOverlayItem> overlay;
        BatchEdit previewEdit;
        QString cursor;
        QString statusText;
        bool requestUndoCheckpoint = false;
        QString undoCheckpointLabel;
    };

    // Host ABI/API version for runtime compatibility checks.
    static constexpr int kHostApiVersion = 3;
    static constexpr int kMinSupportedPluginApiVersion = 2;

    // Capability keys.
    static constexpr const char *kCapabilityChartObserver = "chart_observer";
    static constexpr const char *kCapabilityAdvancedColorEditor = "advanced_color_editor";
    static constexpr const char *kCapabilityToolActions = "tool_actions";
    static constexpr const char *kCapabilityFloatingPanel = "floating_panel";
    static constexpr const char *kCapabilityCanvasOverlay = "canvas_overlay";
    static constexpr const char *kCapabilityHostBatchEdit = "host_batch_edit";
    static constexpr const char *kCapabilityCanvasInteraction = "canvas_interaction";
    static constexpr const char *kCapabilityPanelWorkspace = "panel_workspace";
    static constexpr const char *kPlacementToolsMenu = "tools_menu";
    static constexpr const char *kPlacementTopToolbar = "top_toolbar";
    static constexpr const char *kPlacementLeftSidebar = "left_sidebar";
    static constexpr const char *kPlacementRightNotePanel = "right_note_panel";
    static constexpr const char *kPlacementPluginContextMenu = "plugin_context_menu";

    virtual ~PluginInterface() = default;

    // Metadata (required).
    virtual QString pluginId() const = 0;
    virtual QString displayName() const = 0;
    virtual QString version() const = 0;
    virtual QString description() const = 0;
    virtual QString author() const = 0;
    virtual QString pluginSourcePath() const
    {
        return QString();
    }
    virtual QString localizedDisplayName(const QString &locale) const
    {
        (void)locale;
        return displayName();
    }
    virtual QString localizedDescription(const QString &locale) const
    {
        (void)locale;
        return description();
    }
    virtual int pluginApiVersion() const = 0;
    virtual QStringList capabilities() const = 0;

    // Lifecycle.
    virtual bool initialize(QWidget *mainWindow) = 0;
    virtual void shutdown() = 0;

    // Optional lifecycle events.
    virtual void onChartChanged() {}
    virtual void onChartLoaded(const QString &chartPath)
    {
        (void)chartPath;
    }
    virtual void onChartSaved(const QString &chartPath)
    {
        (void)chartPath;
    }
    virtual void onHostUndo(const QString &actionText)
    {
        (void)actionText;
    }
    virtual void onHostRedo(const QString &actionText)
    {
        (void)actionText;
    }
    virtual void onHostDiscardChanges(const QString &reasonText)
    {
        (void)reasonText;
    }

    // Optional UI extension point.
    virtual bool openAdvancedColorEditor(const QVariantMap &context)
    {
        (void)context;
        return false;
    }

    // Optional tool action extension points.
    virtual QList<ToolAction> toolActions() const
    {
        return {};
    }
    virtual bool runToolAction(const QString &actionId, const QVariantMap &context)
    {
        (void)actionId;
        (void)context;
        return false;
    }
    // Optional host-side batch edit path.
    // If true is returned, host applies all edits as ONE undo step.
    virtual bool buildToolActionBatchEdit(const QString &actionId, const QVariantMap &context, BatchEdit *outEdit)
    {
        (void)actionId;
        (void)context;
        if (outEdit)
            *outEdit = BatchEdit{};
        return false;
    }
    // Optional floating panel extension points (native plugin embedding).
    virtual QList<FloatingPanelDescriptor> floatingPanels() const
    {
        return {};
    }
    virtual QWidget *createFloatingPanel(const QString &panelId, QWidget *parent, const QVariantMap &context)
    {
        (void)panelId;
        (void)parent;
        (void)context;
        return nullptr;
    }
    // Optional canvas overlay rendering extension point.
    virtual QList<CanvasOverlayItem> canvasOverlays(const QVariantMap &context) const
    {
        (void)context;
        return {};
    }
    // Optional interactive canvas input extension point.
    virtual bool handleCanvasInput(const QVariantMap &context,
                                   const CanvasInputEvent &event,
                                   CanvasInputResult *outResult)
    {
        (void)context;
        (void)event;
        if (outResult)
            *outResult = CanvasInputResult{};
        return false;
    }
    // Optional workspace metadata for dockable/mergeable plugin panels.
    virtual QVariantMap panelWorkspaceConfig(const QVariantMap &context) const
    {
        (void)context;
        return {};
    }

    bool hasCapability(const QString &capability) const
    {
        return capabilities().contains(capability, Qt::CaseInsensitive);
    }
};

// Runtime loader contract (mandatory exports in plugin dynamic library).
using CreatePluginFn = PluginInterface *(*)();
using DestroyPluginFn = void (*)(PluginInterface *);
using PluginApiVersionFn = int (*)();

#define PLUGIN_EXPORT_API extern "C" Q_DECL_EXPORT
#define PLUGIN_EXPORT_CREATE PLUGIN_EXPORT_API PluginInterface *createPlugin()
#define PLUGIN_EXPORT_DESTROY PLUGIN_EXPORT_API void destroyPlugin(PluginInterface *plugin)
#define PLUGIN_EXPORT_VERSION PLUGIN_EXPORT_API int pluginApiVersion()
