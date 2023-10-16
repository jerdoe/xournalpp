#include "OpacityPreviewToolbox.h"  // for std::round

#include <cmath>

#include <cairo.h>        // for cairo_set_operator, cairo_rectangle, cairo_...
#include <glib-object.h>  // for G_CALLBACK, g_signal_connect
#include <glib.h>         // for gdouble

#include "control/Control.h"      // for Tool...
#include "control/ToolHandler.h"  // for Tool...
#include "gui/toolbarMenubar/ColorToolItem.h"
#include "gui/toolbarMenubar/ToolMenuHandler.h"  // for Tool...
#include "util/Color.h"
#include "util/raii/CairoWrappers.h"
#include "util/raii/GObjectSPtr.h"

#include "MainWindow.h"  // for MainWindow


static int percentToByte(double percent) { return static_cast<int>(std::round(percent * 2.55)); }
static double byteToPercent(int byte) { return byte / 2.55; }

OpacityPreviewToolbox::OpacityPreviewToolbox(MainWindow* theMainWindow, GtkOverlay* overlay):
        ODebuggable("OpacityPreviewToolbox"), theMainWindow(theMainWindow), overlay(overlay, xoj::util::ref) {
    this->odebug_enter("OpacityPreviewToolbox");

    this->opacityPreviewToolbox.widget = theMainWindow->get("opacityPreviewTool");

    gtk_overlay_add_overlay(overlay, this->opacityPreviewToolbox.widget);
    gtk_overlay_set_overlay_pass_through(overlay, this->opacityPreviewToolbox.widget, true);

    this->selectedColor.eventBox.widget = gtk_event_box_new();
    gtk_widget_set_size_request(this->selectedColor.eventBox.widget, 50, 50);

    gtk_overlay_add_overlay(overlay, this->selectedColor.eventBox.widget);
    gtk_overlay_set_overlay_pass_through(overlay, this->selectedColor.eventBox.widget, true);

    g_signal_connect(overlay, "get-child-position", G_CALLBACK(this->getOverlayPosition), this);

    gtk_widget_add_events(this->selectedColor.eventBox.widget, GDK_POINTER_MOTION_MASK);
    g_signal_connect(this->selectedColor.eventBox.widget, "enter-notify-event", G_CALLBACK(this->enterEventBox), this);
    g_signal_connect(this->selectedColor.eventBox.widget, "motion-notify-event", G_CALLBACK(this->motionEventBox),
                     this);
    g_signal_connect(this->selectedColor.eventBox.widget, "leave-notify-event", G_CALLBACK(this->leaveEventBox), this);

    g_signal_connect(theMainWindow->get("opacityPreviewToolScaleAlpha"), "change-value", G_CALLBACK(this->changeValue),
                     this);

    gtk_widget_add_events(this->opacityPreviewToolbox.widget, GDK_LEAVE_NOTIFY_MASK);
    g_signal_connect(this->opacityPreviewToolbox.widget, "leave-notify-event", G_CALLBACK(this->leaveOpacityToolbox),
                     this);

    gtk_widget_show_all(this->selectedColor.eventBox.widget);
    this->odebug_exit();
}

gboolean OpacityPreviewToolbox::enterEventBox(GtkWidget* eventBox, GdkEventCrossing* event,
                                              OpacityPreviewToolbox* self) {
    self->odebug_enter("enterEventBox");
    self->odebug_current_func("event->detail=%i ; event->mode=%i ; event->focus = %i", event->detail, event->mode,
                              event->focus);

    self->showToolbox();

    self->odebug_exit();
    return false;
}

gboolean OpacityPreviewToolbox::motionEventBox(GtkWidget* eventBox, GdkEventMotion* event,
                                               OpacityPreviewToolbox* self) {
    self->odebug_enter("motionEventBox");

    self->odebug_current_func("event->x=%f ; event->y=%f ; event->x_root = %f, event->y_root = %f", event->x, event->y,
                              event->x_root, event->y_root);

    self->selectedColor.eventBox.lastEventMotion.x_root = event->x_root;
    self->selectedColor.eventBox.lastEventMotion.y_root = event->y_root;

    self->odebug_exit();
    return false;
}

bool OpacityPreviewToolbox::isPointerOverWidget(gint pointer_x_root, gint pointer_y_root, GtkWidget* widget,
                                                OpacityPreviewToolbox* self) {
    self->odebug_enter("isPointerOverWidget");
    gint widget_x_root, widget_y_root;
    gdk_window_get_origin(gtk_widget_get_window(widget), &widget_x_root, &widget_y_root);

    gint widget_height = gtk_widget_get_allocated_height(widget);
    gint widget_width = gtk_widget_get_allocated_width(widget);

    bool result;

    if (pointer_x_root >= widget_x_root && pointer_x_root <= widget_x_root + widget_width &&
        pointer_y_root >= widget_y_root && pointer_y_root <= widget_y_root + widget_height) {
        result = true;
    } else {
        result = false;
    }
    self->odebug_current_func("return result = %i", result);
    self->odebug_exit();
    return result;
}

gboolean OpacityPreviewToolbox::leaveEventBox(GtkWidget* eventBox, GdkEventCrossing* event,
                                              OpacityPreviewToolbox* self) {
    self->odebug_enter("leaveEventBox");
    self->odebug_current_func("event->detail=%i ; event->mode=%i ; event->focus = %i", event->detail, event->mode,
                              event->focus);

    if (!OpacityPreviewToolbox::isPointerOverWidget(
                static_cast<gint>(self->selectedColor.eventBox.lastEventMotion.x_root),
                static_cast<gint>(self->selectedColor.eventBox.lastEventMotion.y_root),
                self->opacityPreviewToolbox.widget, self)) {
        self->hideToolbox();
    }
    self->odebug_exit();
    return false;
}

void OpacityPreviewToolbox::changeValue(GtkRange* range, GtkScrollType scroll, gdouble value,
                                        OpacityPreviewToolbox* self) {
    self->odebug_enter("changeValue");
    gtk_range_set_value(range, value);
    gdouble rangedValue = gtk_range_get_value(range);
    self->color.alpha = static_cast<uint8_t>(percentToByte(rangedValue));
    self->updatePreviewImage();
    self->odebug_exit();
}

gboolean OpacityPreviewToolbox::leaveOpacityToolbox(GtkWidget* opacityToolbox, GdkEventCrossing* event,
                                                    OpacityPreviewToolbox* self) {
    self->odebug_enter("leaveOpacityToolbox");
    self->odebug_current_func("event->detail=%i ; event->mode=%i ; event->focus = %i", event->detail, event->mode,
                              event->focus);

    bool hideToolbox = false;

    switch (event->detail) {
        case GDK_NOTIFY_INFERIOR:
            hideToolbox = false;
            break;
        default:
            hideToolbox = true;
            break;
    }

    if (hideToolbox) {
        GtkRange* range = GTK_RANGE(self->theMainWindow->get("opacityPreviewToolScaleAlpha"));
        double value = gtk_range_get_value(range);

        self->color.alpha = static_cast<uint8_t>(percentToByte(value));

        ToolHandler* toolHandler = self->theMainWindow->getControl()->getToolHandler();

        switch (toolHandler->getToolType()) {
            case TOOL_SELECT_PDF_TEXT_RECT:
            case TOOL_SELECT_PDF_TEXT_LINEAR:
                toolHandler->setSelectPDFTextMarkerOpacity(self->color.alpha);
                break;
            default:
                toolHandler->setColor(self->color, false);
                break;
        }
        self->hideToolbox();
    }
    self->odebug_exit();
    return false;
}

const int PREVIEW_WIDTH = 70;
const int PREVIEW_HEIGHT = 50;
const int PREVIEW_BORDER = 10;

bool OpacityPreviewToolbox::isEnabled() {
    this->odebug_enter("isEnabled");

    MainWindow* win = this->theMainWindow;
    ToolHandler* toolHandler = win->getControl()->getToolHandler();

    bool result;

    switch (toolHandler->getToolType()) {
        case TOOL_PEN:
        case TOOL_SELECT_PDF_TEXT_RECT:
        case TOOL_SELECT_PDF_TEXT_LINEAR:
            result = true;
            break;
        default:
            result = false;
            break;
    }

    this->odebug_exit();
    return result;
}

void OpacityPreviewToolbox::updateColor() {
    this->odebug_enter("updateColor");
    this->color = theMainWindow->getControl()->getToolHandler()->getColor();
    this->odebug_exit();
}
void OpacityPreviewToolbox::update() {
    this->odebug_enter("update");

    ToolHandler* toolHandler = theMainWindow->getControl()->getToolHandler();

    bool enabled = this->isEnabled();

    if (enabled) {
        gtk_widget_show_all(this->selectedColor.eventBox.widget);

        this->updateEventBoxAllocation();
        this->updateOpacityToolboxAllocation();

        // The opacity toolbox must be shown only if switching color of the SAME tool.
        if (this->lastActiveTool == toolHandler->getActiveTool()) {
            this->showToolbox();
        }
    } else {
        gtk_widget_hide(this->selectedColor.eventBox.widget);
        this->hideToolbox();
    }

    this->lastActiveTool = toolHandler->getActiveTool();
    this->odebug_exit();
}

void OpacityPreviewToolbox::updateSelectedColorItem() {
    this->odebug_enter("updateSelectedColorItem");
    const std::vector<std::unique_ptr<ColorToolItem>>& colorItems =
            this->theMainWindow->getToolMenuHandler()->getColorToolItems();

    this->selectedColor.item = nullptr;

    for (const std::unique_ptr<ColorToolItem>& colorItem: colorItems) {
        // Ignore alpha channel to compare tool color with button color
        Color toolColorMaskAlpha = this->color;
        toolColorMaskAlpha.alpha = 255;

        if (toolColorMaskAlpha == colorItem.get()->getColor()) {
            this->selectedColor.item = colorItem.get();
        }
    }
    this->odebug_exit();
}

/**
 * Adjust size and position of the eventbox
 * to match those of the selected ColorToolItem
 */
void OpacityPreviewToolbox::updateEventBoxAllocation() {
    this->odebug_enter("updateEventBoxAllocation");

    this->updateColor();
    this->updateSelectedColorItem();

    if (this->selectedColor.item == nullptr) {
        this->selectedColor.eventBox.allocation.width = 0;
        this->selectedColor.eventBox.allocation.height = 0;
        this->selectedColor.eventBox.allocation.x = 0;
        this->selectedColor.eventBox.allocation.y = 0;
    } else {
        GtkWidget* selectedColorWidget = GTK_WIDGET(this->selectedColor.item->getItem());
        GtkWidget* overlayWidget = GTK_WIDGET(overlay.get());

        this->selectedColor.eventBox.allocation.width = gtk_widget_get_allocated_width(selectedColorWidget);
        this->selectedColor.eventBox.allocation.height = gtk_widget_get_allocated_height(selectedColorWidget);

        // Copy coordinates of selectedColorWidget
        // in selectedColor.eventBox.allocation.x and selectedColor.eventBox.allocation.y
        // using overlay's coordinate space
        gtk_widget_translate_coordinates(selectedColorWidget, overlayWidget, 0, 0,
                                         &this->selectedColor.eventBox.allocation.x,
                                         &this->selectedColor.eventBox.allocation.y);
    }

    this->odebug_current_func("allocation.x=%i, allocation.y=%i, allocation.width=%i, allocation.height=%i",
                              this->selectedColor.eventBox.allocation.x, this->selectedColor.eventBox.allocation.y,
                              this->selectedColor.eventBox.allocation.width,
                              this->selectedColor.eventBox.allocation.height);
    this->odebug_exit();
}

/**
 * Position the opacity toolbox below the selected ColorToolItem
 * and align their centers vertically.
 */
void OpacityPreviewToolbox::updateOpacityToolboxAllocation() {
    this->odebug_enter("updateOpacityToolboxAllocation");
    // Below the color button
    // At this time, eventbox allocation matches with those of the selected ColorToolItem
    this->opacityPreviewToolbox.allocation.y =
            this->selectedColor.eventBox.allocation.y + this->selectedColor.eventBox.allocation.height;

    // Get existing width and height
    GtkRequisition natural;

    gtk_widget_get_preferred_size(this->opacityPreviewToolbox.widget, nullptr, &natural);

    this->odebug_current_func("natural.width=%i, natural.height=%i", natural.width, natural.height);

    this->opacityPreviewToolbox.allocation.width = natural.width;
    this->opacityPreviewToolbox.allocation.height = natural.height;

    // Make sure the "OpacityPreviewToolbox" is fully displayed.
    //        const int gap = 5;
    const int gap = 0;

    // Calculate offset_x needed so that the center is vertically with the selected color item.
    int offset_x = static_cast<int>(std::round((this->selectedColor.eventBox.allocation.width - natural.width) / 2));

    int adjusted_position_x = this->selectedColor.eventBox.allocation.x + offset_x;

    // If the toolbox will go out of the window, then we'll flip the corresponding directions.
    GtkAllocation windowAlloc{};
    gtk_widget_get_allocation(GTK_WIDGET(overlay.get()), &windowAlloc);

    bool rightOK = adjusted_position_x + natural.width + gap <= windowAlloc.width;
    bool bottomOK = this->opacityPreviewToolbox.allocation.y + natural.height + gap <= windowAlloc.height;

    this->opacityPreviewToolbox.allocation.x =
            rightOK ? adjusted_position_x + gap : adjusted_position_x - natural.width - gap;

    this->opacityPreviewToolbox.allocation.y = bottomOK ?
                                                       this->opacityPreviewToolbox.allocation.y + gap :
                                                       this->opacityPreviewToolbox.allocation.y - natural.height - gap;

    // Ensure an overlap between selected ColorToolItem and opacityPreviewToolbox
    // for handling the "notify-leave-event" signal.
    // OpacityPreviewToolbox should not be hidden when leaving the selected ColorToolItem
    // if the pointer is at the intersection.
    this->opacityPreviewToolbox.allocation.y -= 5;

    this->odebug_current_func("allocation.x=%i, allocation.y=%i, allocation.width=%i, allocation.height=%i",
                              this->opacityPreviewToolbox.allocation.x, this->opacityPreviewToolbox.allocation.y,
                              this->opacityPreviewToolbox.allocation.width,
                              this->opacityPreviewToolbox.allocation.height);
    this->odebug_exit();
}

void OpacityPreviewToolbox::updateScaleValue() {
    this->odebug_enter("updateScaleValue");
    GtkRange* rangeWidget = (GtkRange*)this->theMainWindow->get("opacityPreviewToolScaleAlpha");
    gtk_range_set_value(rangeWidget, byteToPercent(this->color.alpha));
    this->odebug_exit();
}

static bool inline useBorderForPreview(ToolType tooltype) {
    switch (tooltype) {
        case TOOL_PEN:
            return true;
        default:
            return false;
    }
}

void OpacityPreviewToolbox::updatePreviewImage() {
    this->odebug_enter("updatePreviewImage");

    bool addBorder = useBorderForPreview(this->theMainWindow->getControl()->getToolHandler()->getToolType());

    xoj::util::CairoSurfaceSPtr surface(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, PREVIEW_WIDTH, PREVIEW_HEIGHT),
                                        xoj::util::adopt);
    xoj::util::CairoSPtr cairo(cairo_create(surface.get()), xoj::util::adopt);
    cairo_t* cr = cairo.get();

    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgb(cr, 255, 255, 255);
    cairo_paint(cr);

    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    Util::cairo_set_source_argb(cr, this->color);
    cairo_rectangle(cr, PREVIEW_BORDER, PREVIEW_BORDER, PREVIEW_WIDTH - PREVIEW_BORDER * 2,
                    PREVIEW_HEIGHT - PREVIEW_BORDER * 2);
    cairo_fill(cr);

    if (addBorder) {
        cairo_set_line_width(cr, 5);
        cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
        Color borderColor = this->color;
        borderColor.alpha = 255;
        Util::cairo_set_source_argb(cr, borderColor);
        cairo_rectangle(cr, PREVIEW_BORDER, PREVIEW_BORDER, PREVIEW_WIDTH - PREVIEW_BORDER * 2,
                        PREVIEW_HEIGHT - PREVIEW_BORDER * 2);
        cairo_stroke(cr);
    }

    xoj::util::GObjectSPtr<GdkPixbuf> pixbuf(
            gdk_pixbuf_get_from_surface(surface.get(), 0, 0, PREVIEW_WIDTH, PREVIEW_HEIGHT), xoj::util::adopt);
    gtk_image_set_from_pixbuf(GTK_IMAGE(theMainWindow->get("opacityPreviewToolImg")), pixbuf.get());
    this->odebug_exit();
}
OpacityPreviewToolbox::~OpacityPreviewToolbox() = default;

void OpacityPreviewToolbox::showToolbox() {
    this->odebug_enter("showToolbox");

    this->updateColor();
    this->updateScaleValue();
    this->updatePreviewImage();

    gtk_widget_hide(this->opacityPreviewToolbox.widget);  // force showing in new position
    gtk_widget_show_all(this->opacityPreviewToolbox.widget);

    this->odebug_exit();
}

void OpacityPreviewToolbox::hideToolbox() {
    this->odebug_enter("hideToolbox");
    if (isHidden())
        return;

    gtk_widget_hide(this->opacityPreviewToolbox.widget);
    this->odebug_exit();
}

auto OpacityPreviewToolbox::getOverlayPosition(GtkOverlay* overlay, GtkWidget* widget, GdkRectangle* allocation,
                                               OpacityPreviewToolbox* self) -> gboolean {
    if (widget != self->opacityPreviewToolbox.widget && widget != self->selectedColor.eventBox.widget) {
        return false;
    }

    self->odebug_enter("getOverlayPosition");

    if (self->isEnabled()) {
        self->updateEventBoxAllocation();
        self->updateOpacityToolboxAllocation();

        if (widget == self->opacityPreviewToolbox.widget) {
            allocation->x = self->opacityPreviewToolbox.allocation.x;
            allocation->y = self->opacityPreviewToolbox.allocation.y;
            allocation->width = self->opacityPreviewToolbox.allocation.width;
            allocation->height = self->opacityPreviewToolbox.allocation.height;
        } else if (widget == self->selectedColor.eventBox.widget) {
            allocation->x = self->selectedColor.eventBox.allocation.x;
            allocation->y = self->selectedColor.eventBox.allocation.y;
            allocation->width = self->selectedColor.eventBox.allocation.width;
            allocation->height = self->selectedColor.eventBox.allocation.height;
        }
    } else {
        allocation->width = gtk_widget_get_allocated_width(widget);
        allocation->height = gtk_widget_get_allocated_height(widget);

        // Copy coordinates of selectedColorWidget
        // in selectedColor.eventBox.allocation.x and selectedColor.eventBox.allocation.y
        // using overlay's coordinate space
        gtk_widget_translate_coordinates(widget, GTK_WIDGET(overlay), 0, 0, &allocation->x, &allocation->y);
    }

    self->odebug_current_func("widget_name='%s' ; "
                              "allocation->x ='%i' ; allocation->y='%i' ; "
                              "allocation->width='%i' ; allocation->height='%i'",
                              gtk_widget_get_name(widget), allocation->x, allocation->y, allocation->width,
                              allocation->height);

    self->odebug_exit();
    return true;
}

bool OpacityPreviewToolbox::isHidden() const { return !gtk_widget_is_visible(this->opacityPreviewToolbox.widget); }
