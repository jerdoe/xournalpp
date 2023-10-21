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

#include "FloatingToolbox.h"
#include "MainWindow.h"  // for MainWindow


static int percentToByte(double percent) { return static_cast<int>(std::round(percent * 2.55)); }
static double byteToPercent(int byte) { return byte / 2.55; }

OpacityPreviewToolbox::OpacityPreviewToolbox(MainWindow* theMainWindow, GtkOverlay* overlay):
        ODebuggable("OpacityPreviewToolbox"), theMainWindow(theMainWindow), overlay(overlay, xoj::util::ref) {
    this->odebug_enter("OpacityPreviewToolbox");

    this->opacityPreviewToolbox.widget = theMainWindow->get("opacityPreviewTool");

    gtk_overlay_add_overlay(overlay, this->opacityPreviewToolbox.widget);
    gtk_overlay_set_overlay_pass_through(overlay, this->opacityPreviewToolbox.widget, true);

    g_signal_connect(overlay, "get-child-position", G_CALLBACK(this->getOverlayPosition), this);
    g_signal_connect(theMainWindow->get("opacityPreviewToolScaleAlpha"), "change-value", G_CALLBACK(this->changeValue),
                     this);

    gtk_widget_add_events(this->opacityPreviewToolbox.widget, GDK_LEAVE_NOTIFY_MASK);
    g_signal_connect(this->opacityPreviewToolbox.widget, "leave-notify-event", G_CALLBACK(this->leaveOpacityToolbox),
                     this);

    this->odebug_exit();
}

std::vector<OpacityPreviewToolbox::EventBox>::iterator OpacityPreviewToolbox::findEventBox(GtkWidget* eventBoxWidget) {
    auto criteria = [eventBoxWidget](EventBox eventBox) { return eventBox.widget == eventBoxWidget; };
    return std::find_if(this->eventBoxes.begin(), this->eventBoxes.end(), criteria);
}

gboolean OpacityPreviewToolbox::enterEventBox(GtkWidget* eventBoxWidget, GdkEventCrossing* event,
                                              OpacityPreviewToolbox* self) {
    self->odebug_enter("enterEventBox");
    self->odebug_current_func("event->detail=%i ; event->mode=%i ; event->focus = %i", event->detail, event->mode,
                              event->focus);

    auto result = self->findEventBox(eventBoxWidget);

    if (result != self->eventBoxes.end()) {
        self->showToolbox();
        self->updateOpacityToolboxAllocation(*result);
        self->opacityPreviewToolbox.eventBox = result;
    }

    self->odebug_exit();
    return false;
}

bool OpacityPreviewToolbox::isPointerOverWidget(gint pointer_x_root, gint pointer_y_root, GtkWidget* widget,
                                                OpacityPreviewToolbox* self) {
    self->odebug_enter("isPointerOverWidget");
    gint widget_x_root, widget_y_root;
    if (GDK_IS_WINDOW(gtk_widget_get_window(widget))) {
        gdk_window_get_origin(gtk_widget_get_window(widget), &widget_x_root, &widget_y_root);
    }

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
    self->odebug_current_func("event->detail=%i ; event->mode=%i ; event->focus = %i ; "
                              "event->x_root = %f ; event->y_root = %f",
                              event->detail, event->mode, event->focus, event->x_root, event->y_root);

    if (!OpacityPreviewToolbox::isPointerOverWidget(static_cast<gint>(event->x_root), static_cast<gint>(event->y_root),
                                                    self->opacityPreviewToolbox.widget, self)) {
        self->hideToolbox();

        // When exiting an eventbox matching a ColorToolItem within the floating toolbox
        // and the pointer is outside the toolbox, hide it.
        auto result = self->findEventBox(eventBox);
        FloatingToolbox* oFloatingToolbox = self->theMainWindow->getFloatingToolbox();
        if (result->inFloatingToolbox == true) {
            if (!self->isPointerOverWidget(static_cast<int>(event->x_root), static_cast<int>(event->y_root),
                                           oFloatingToolbox->floatingToolbox, self)) {
                oFloatingToolbox->hide();
            }
        }
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
            case TOOL_PEN:
                toolHandler->setPenFill(self->color.alpha);
                break;
            case TOOL_HIGHLIGHTER:
                toolHandler->setHighlighterFill(self->color.alpha);
                break;
            default:
                toolHandler->setColor(self->color, false);
                break;
        }

        FloatingToolbox* oFloatingToolbox = self->theMainWindow->getFloatingToolbox();
        if (!isPointerOverWidget(static_cast<int>(event->x_root), static_cast<int>(event->y_root),
                                 oFloatingToolbox->floatingToolbox, self)) {
            oFloatingToolbox->hide();
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
            result = toolHandler->getPenFillEnabled() ? true : false;
            break;
        case TOOL_HIGHLIGHTER:
            result = toolHandler->getHighlighterFillEnabled() ? true : false;
            break;
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
    ToolHandler* toolHandler = this->theMainWindow->getControl()->getToolHandler();

    ToolType tooltype = toolHandler->getToolType();

    switch (tooltype) {
        case TOOL_PEN:
            this->color.alpha = static_cast<uint8_t>(toolHandler->getPenFill());
            break;
        case TOOL_HIGHLIGHTER:
            this->color.alpha = static_cast<uint8_t>(toolHandler->getHighlighterFill());
            break;
        default:
            break;
    }
    this->odebug_exit();
}

void OpacityPreviewToolbox::update() {
    this->odebug_enter("update");

    bool enabled = this->isEnabled();

    if (enabled) {
        this->resetEventBoxes();
    } else {
        for (EventBox eventBox: this->eventBoxes) {
            gtk_widget_hide(eventBox.widget);
        }
        this->hideToolbox();
    }

    this->odebug_exit();
}

void OpacityPreviewToolbox::hideEventBoxesInFloatingToolBox() {
    for (EventBox eventBox: this->eventBoxes) {
        if (eventBox.inFloatingToolbox == true) {
            gtk_widget_hide(eventBox.widget);
        }
    }
}

void OpacityPreviewToolbox::showEventBoxesInFloatingToolBox() {
    for (EventBox eventBox: this->eventBoxes) {
        if (eventBox.inFloatingToolbox == true) {
            gtk_widget_hide(eventBox.widget);
            gtk_widget_show_all(eventBox.widget);
        }
    }
}

static bool inline isAncestor(GtkWidget* ancestor, GtkWidget* child, OpacityPreviewToolbox* self) {
    self->odebug_enter("isAncestor");
    GtkWidget* it = child;

    while (it != nullptr) {
        self->odebug_current_func("ancestor = %s:%p ; child.name = %s:%p ; it.name = %s:%p",
                                  gtk_widget_get_name(ancestor), (void*)ancestor, gtk_widget_get_name(child),
                                  (void*)child, gtk_widget_get_name(it), (void*)it);
        if (it == ancestor) {
            self->odebug_current_func("return true");
            self->odebug_exit();
            return true;
        }
        it = gtk_widget_get_parent(it);
    }
    self->odebug_current_func("return false");
    self->odebug_exit();
    return false;
}

void OpacityPreviewToolbox::resetEventBoxes() {
    this->odebug_enter("resetEventBoxes");
    for (EventBox eventBox: this->eventBoxes) {
        gtk_widget_destroy(eventBox.widget);
    }
    this->eventBoxes.clear();
    this->opacityPreviewToolbox.eventBox = this->eventBoxes.end();

    this->updateColor();
    const std::vector<std::unique_ptr<ColorToolItem>>& colorItems =
            this->theMainWindow->getToolMenuHandler()->getColorToolItems();

    int index = 0;

    GtkWidget* floatingToolbox = this->theMainWindow->getFloatingToolbox()->floatingToolbox;

    for (const std::unique_ptr<ColorToolItem>& colorItem: colorItems) {
        // Ignore alpha channel to compare tool color with button color
        Color toolColorMaskAlpha = this->color;
        toolColorMaskAlpha.alpha = 255;

        // For every colorItem matching the current tool color
        // an EventBox is created and added to the overlay
        if (toolColorMaskAlpha == colorItem.get()->getColor()) {
            EventBox eventBox;
            this->eventBoxes.push_back(eventBox);

            EventBox& refEventBox = this->eventBoxes.back();

            initEventBox(refEventBox, colorItem.get(), index++);

            refEventBox.inFloatingToolbox = isAncestor(floatingToolbox, refEventBox.item->getItem(), this);
        }
    }

    gtk_overlay_reorder_overlay(this->overlay.get(), this->opacityPreviewToolbox.widget, -1);
    this->odebug_exit();
}

void OpacityPreviewToolbox::initEventBox(EventBox& eventBox, ColorToolItem* colorItem, int index) {
    this->odebug_enter("initEventBox");
    eventBox.item = colorItem;
    eventBox.widget = gtk_event_box_new();

    std::string widget_name = "OpacityEventBox" + std::to_string(index);
    gtk_widget_set_name(eventBox.widget, widget_name.c_str());

    gtk_widget_set_size_request(eventBox.widget, 50, 50);

    gtk_overlay_add_overlay(this->overlay.get(), eventBox.widget);
    gtk_overlay_set_overlay_pass_through(this->overlay.get(), eventBox.widget, true);

    g_signal_connect(eventBox.widget, "enter-notify-event", G_CALLBACK(this->enterEventBox), this);
    g_signal_connect(eventBox.widget, "leave-notify-event", G_CALLBACK(this->leaveEventBox), this);

    gtk_widget_show_all(eventBox.widget);

    this->updateEventBoxAllocation(eventBox);
    this->odebug_exit();
}

/**
 * Adjust size and position of the eventbox
 * to match those of the selected ColorToolItem
 */
void OpacityPreviewToolbox::updateEventBoxAllocation(EventBox& eventBox) {
    this->odebug_enter("updateEventBoxAllocation");

    GtkWidget* selectedColorWidget = GTK_WIDGET(eventBox.item->getItem());
    GtkWidget* overlayWidget = GTK_WIDGET(overlay.get());

    eventBox.allocation.width = gtk_widget_get_allocated_width(selectedColorWidget);
    eventBox.allocation.height = gtk_widget_get_allocated_height(selectedColorWidget);

    // Copy coordinates of selectedColorWidget
    // in eventBox.allocation.x and eventBox.allocation.y
    // using overlay's coordinate space
    gtk_widget_translate_coordinates(selectedColorWidget, overlayWidget, 0, 0, &eventBox.allocation.x,
                                     &eventBox.allocation.y);

    this->odebug_current_func("allocation.x=%i, allocation.y=%i, allocation.width=%i, allocation.height=%i",
                              eventBox.allocation.x, eventBox.allocation.y, eventBox.allocation.width,
                              eventBox.allocation.height);
    this->odebug_exit();
}

void OpacityPreviewToolbox::updateOpacityToolboxSizeAllocation() {
    this->odebug_enter("updateOpacityToolboxSizeAllocation");

    // Get existing width and height
    GtkRequisition natural;

    gtk_widget_get_preferred_size(this->opacityPreviewToolbox.widget, nullptr, &natural);

    this->odebug_current_func("natural.width=%i, natural.height=%i", natural.width, natural.height);

    this->opacityPreviewToolbox.allocation.width = natural.width;
    this->opacityPreviewToolbox.allocation.height = natural.height;

    this->odebug_exit();
}

/**
 * Adjust the position of the opacity toolbox so that it sits below, above, on the right or
 * on the left of the selected color item, in a manner that makes it fully visible.
 */
void OpacityPreviewToolbox::updateOpacityToolboxAllocation(EventBox eventBox) {
    this->odebug_enter("updateOpacityToolboxAllocation");

    // At this moment, eventbox allocation matches with the selected ColorToolItem

    this->updateOpacityToolboxSizeAllocation();
    int toolbox_width = this->opacityPreviewToolbox.allocation.width;
    int toolbox_height = this->opacityPreviewToolbox.allocation.height;

    // If the toolbox will go out of the window, then we'll flip the corresponding directions.
    GtkAllocation windowAlloc{};
    gtk_widget_get_allocation(GTK_WIDGET(overlay.get()), &windowAlloc);

    // Make sure the "OpacityPreviewToolbox" is fully displayed.
    //        const int gap = 5;
    const int gap = 0;

    bool isColorItemTooFarLeft = eventBox.allocation.x - toolbox_width - gap < 0;
    bool isColorItemTooFarRight =
            eventBox.allocation.x + eventBox.allocation.width + toolbox_width + gap > windowAlloc.width;
    bool isColorItemTooFarBottom =
            eventBox.allocation.y + eventBox.allocation.height + toolbox_height + gap > windowAlloc.height;

    // Ensure an overlap between the selected ColorToolItem and the opacity toolbox
    // for handling the "notify-leave-event" signal, so that the user can leave
    // the selected ColorToolItem and enter the opacity toolbox without making it
    // disappear.
    int OVERLAP_OFFSET_VALUE = 2;

    // Increase overlap in corners for an improved user experience,
    // especially due to the rounded corners of the opacity toolbox.
    if (isColorItemTooFarBottom && (isColorItemTooFarRight || isColorItemTooFarLeft)) {
        OVERLAP_OFFSET_VALUE = 15;
    }

    this->updateOpacityToolboxAllocationX(eventBox, toolbox_width, isColorItemTooFarLeft, isColorItemTooFarRight,
                                          OVERLAP_OFFSET_VALUE);
    this->updateOpacityToolboxAllocationY(eventBox, toolbox_height, isColorItemTooFarLeft, isColorItemTooFarRight,
                                          isColorItemTooFarBottom, OVERLAP_OFFSET_VALUE);

    this->odebug_current_func("allocation.x=%i, allocation.y=%i, allocation.width=%i, allocation.height=%i",
                              this->opacityPreviewToolbox.allocation.x, this->opacityPreviewToolbox.allocation.y,
                              this->opacityPreviewToolbox.allocation.width,
                              this->opacityPreviewToolbox.allocation.height);

    this->odebug_exit();
}

/**
 * Adjust the horizontal position of the opacity toolbox so that it sits on the right, on the left
 * or is vertically centered with the selected color item, in a manner that makes it fully visible.
 */
void OpacityPreviewToolbox::updateOpacityToolboxAllocationX(EventBox& eventBox, const int toolbox_width,
                                                            const bool isColorItemTooFarLeft,
                                                            const bool isColorItemTooFarRight,
                                                            const int overlap_offset_value) {
    int overlap_offset_x = 0;

    if (isColorItemTooFarLeft) {
        // Position the opacity toolbox to the right of the ColorToolItem
        this->opacityPreviewToolbox.allocation.x = eventBox.allocation.x + eventBox.allocation.width;
        overlap_offset_x = -overlap_offset_value;
    } else if (isColorItemTooFarRight) {
        // Position the opacity toolbox to the left of the ColorToolItem
        this->opacityPreviewToolbox.allocation.x = eventBox.allocation.x - toolbox_width;
        overlap_offset_x = overlap_offset_value;
    } else {
        // Centers vertically the opacity toolbox with the selected ColorToolItem
        int offset_x = static_cast<int>(std::round(eventBox.allocation.width - toolbox_width) / 2);
        this->opacityPreviewToolbox.allocation.x = eventBox.allocation.x + offset_x;
    }

    this->opacityPreviewToolbox.allocation.x += overlap_offset_x;
}

/**
 * Adjust the vertical position of the opacity toolbox so that it sits below, above or
 * horizontally centered with the selected color item, in a manner that makes it fully visible.
 */
void OpacityPreviewToolbox::updateOpacityToolboxAllocationY(EventBox& eventBox, const int toolbox_height,
                                                            const bool isColorItemTooFarLeft,
                                                            const bool isColorItemTooFarRight,
                                                            const bool isColorItemTooFarBottom,
                                                            const int overlap_offset_value) {
    int overlap_offset_y = 0;

    if (isColorItemTooFarBottom) {
        // Position the opacity toolbox to the top of the ColorToolItem
        this->opacityPreviewToolbox.allocation.y = eventBox.allocation.y - toolbox_height;
        overlap_offset_y = overlap_offset_value;
    } else if (isColorItemTooFarLeft || isColorItemTooFarRight) {
        // Centers horizontally the opacity toolbox with the selected ColorToolItem
        int offset_y = static_cast<int>(std::round(eventBox.allocation.height - toolbox_height) / 2);
        this->opacityPreviewToolbox.allocation.y = eventBox.allocation.y + offset_y;
    } else {
        // Position the opacity toolbox below the ColorToolIltem
        this->opacityPreviewToolbox.allocation.y = eventBox.allocation.y + eventBox.allocation.height;
        overlap_offset_y = -overlap_offset_value;
    }

    this->opacityPreviewToolbox.allocation.y += overlap_offset_y;
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
    // Find if widget to be positioned is an eventbox
    // to be overlaid over a ColorToolItem.
    auto eventBoxIterator = self->findEventBox(widget);

    // Ignore widgets that are neither the opacity toolbox
    // or the eventboxes to be positioned over ColorToolItems
    if (widget != self->opacityPreviewToolbox.widget && eventBoxIterator == self->eventBoxes.end()) {
        return false;
    }

    self->odebug_enter("getOverlayPosition");
    if (self->isEnabled()) {
        if (widget == self->opacityPreviewToolbox.widget) {
            self->updateOpacityToolboxSizeAllocation();

            allocation->x = self->opacityPreviewToolbox.allocation.x;
            allocation->y = self->opacityPreviewToolbox.allocation.y;
            allocation->width = self->opacityPreviewToolbox.allocation.width;
            allocation->height = self->opacityPreviewToolbox.allocation.height;
        } else {
            EventBox& eventBox = *eventBoxIterator;
            self->updateEventBoxAllocation(eventBox);

            allocation->x = eventBox.allocation.x;
            allocation->y = eventBox.allocation.y;
            allocation->width = eventBox.allocation.width;
            allocation->height = eventBox.allocation.height;
        }
        self->odebug_current_func("widget_name='%s' ; "
                                  "allocation->x ='%i' ; allocation->y='%i' ; "
                                  "allocation->width='%i' ; allocation->height='%i'",
                                  gtk_widget_get_name(widget), allocation->x, allocation->y, allocation->width,
                                  allocation->height);

        self->odebug_exit();
        return true;
    } else {
        // Todo : Remove after making sure the conditional-block might not be useful in certain cases.
        if (false) {
            allocation->width = gtk_widget_get_allocated_width(widget);
            allocation->height = gtk_widget_get_allocated_height(widget);

            // Copy coordinates of selectedColorWidget
            // in selectedColor.eventBox.allocation.x and selectedColor.eventBox.allocation.y
            // using overlay's coordinate space
            gtk_widget_translate_coordinates(widget, GTK_WIDGET(overlay), 0, 0, &allocation->x, &allocation->y);

            self->odebug_exit();
            return true;
        }
        self->odebug_exit();
        return false;
    }
}

bool OpacityPreviewToolbox::isHidden() const { return !gtk_widget_is_visible(this->opacityPreviewToolbox.widget); }
