#pragma once

#include <cstdarg>
#include <cstdint>  // for uint8_t
#include <memory>   // for unique_ptr
#include <sstream>
#include <unordered_set>
#include <vector>

#include <gdk/gdk.h>  // for GdkRectangle
#include <glib.h>     // for gboolean
#include <gtk/gtk.h>  // for GtkButton, GtkOverlay

#include "control/Tool.h"  // for Tool, Tool::toolSizes
#include "gui/toolbarMenubar/ColorToolItem.h"
#include "util/Color.h"             // for Color
#include "util/ODebug.h"            // for ODebuggable
#include "util/raii/GObjectSPtr.h"  // for GObjectSPtr

class MainWindow;

class OpacityPreviewToolbox: ODebuggable {

public:
    OpacityPreviewToolbox(MainWindow* theMainWindow, GtkOverlay* overlay);
    OpacityPreviewToolbox& operator=(const OpacityPreviewToolbox&) = delete;
    OpacityPreviewToolbox(const OpacityPreviewToolbox&) = delete;
    OpacityPreviewToolbox& operator=(OpacityPreviewToolbox&&) = delete;
    OpacityPreviewToolbox(OpacityPreviewToolbox&&) = delete;
    ~OpacityPreviewToolbox();

public:
    void show();
    void update(bool partial = false);
    void hide();

private:
    void updatePreviewImage();
    void updateSelectedColorItem();
    void updateEventBoxAllocation();
    void updateOpacityToolboxAllocation();
    void updateScaleValue();

    /// Returns true if the toolbox is currently hidden.
    bool isHidden() const;

    static void changeValue(GtkRange* range, GtkScrollType scroll, gdouble value, OpacityPreviewToolbox* self);

    static gboolean getOverlayPosition(GtkOverlay* overlay, GtkWidget* widget, GdkRectangle* allocation,
                                       OpacityPreviewToolbox* self);

    static gboolean leaveOpacityToolbox(GtkWidget* opacityToolbox, GdkEventCrossing* event,
                                        OpacityPreviewToolbox* self);
    static gboolean motionOpacityToolbox(GtkWidget* opacityToolbox, GdkEventMotion* event, OpacityPreviewToolbox* self);


    static gboolean enterEventBox(GtkWidget* eventBox, GdkEventCrossing* event, OpacityPreviewToolbox* self);
    static gboolean leaveEventBox(GtkWidget* eventBox, GdkEventCrossing* event, OpacityPreviewToolbox* self);
    static gboolean motionEventBox(GtkWidget* eventBox, GdkEventMotion* event, OpacityPreviewToolbox* self);
    static bool isPointerOverWidget(gint pointer_x_root, gint pointer_y_root, GtkWidget* widget,
                                    OpacityPreviewToolbox* self);

private:
    MainWindow* theMainWindow;

    /// The overlay that the toolbox should be displayed in.
    xoj::util::GObjectSPtr<GtkOverlay> overlay;

    bool enabled = false;
    Color color;
    bool addBorder;
    Tool* lastActiveTool;

    struct {
        GtkWidget* widget;
        GtkAllocation allocation;
    } opacityPreviewToolbox;

    struct {
        // The purpose of this small GtkEventBox is to detect when the mouse enters or leaves
        // the area of the selected ColorToolItem. While it may be possible to directly implement
        // a leave/enter signal handler in the ColorToolItem itself, doing so would require establishing
        // a signal connection for every ColorToolItem individually.
        //
        // By using this GtkEventBox, we can centralize the handling of mouse enter/leave
        // events in one place. This simplifies the code and avoids the need for extra
        // signal connections for each ColorToolItem.
        struct {
            GtkWidget* widget;
            GtkAllocation allocation;
            struct {
                gdouble x_root, y_root;
            } lastEventMotion;
        } eventBox;
        const ColorToolItem* item;
    } selectedColor;
};
