#include "OpacityPreviewToolbox.h"

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
        theMainWindow(theMainWindow), overlay(overlay, xoj::util::ref), position({0, 0}) {
    this->opacityPreviewToolbox = theMainWindow->get("opacityPreviewTool");

    gtk_overlay_add_overlay(overlay, this->opacityPreviewToolbox);
    gtk_overlay_set_overlay_pass_through(overlay, this->opacityPreviewToolbox, true);

    g_signal_connect(overlay, "get-child-position", G_CALLBACK(this->getOverlayPosition), this);

    g_signal_connect(theMainWindow->get("opacityPreviewToolScaleAlpha"), "change-value", G_CALLBACK(this->changeValue),
                     this);

    g_signal_connect(theMainWindow->get("opacityPreviewToolScaleAlpha"), "leave-notify-event",
                     G_CALLBACK(this->handleLeave), this);
}

void OpacityPreviewToolbox::changeValue(GtkRange* range, GtkScrollType scroll, gdouble value,
                                        OpacityPreviewToolbox* self) {
    gtk_range_set_value(range, value);
    gdouble rangedValue = gtk_range_get_value(range);
    self->color.alpha = static_cast<uint8_t>(percentToByte(rangedValue));
    self->updatePreviewImage();
}

gboolean OpacityPreviewToolbox::handleLeave(GtkRange* range, GdkEvent* event, OpacityPreviewToolbox* self) {
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
    return false;
}

const int PREVIEW_WIDTH = 70;
const int PREVIEW_HEIGHT = 50;
const int PREVIEW_BORDER = 10;

void OpacityPreviewToolbox::update() {
    MainWindow* win = this->theMainWindow;
    ToolHandler* toolHandler = win->getControl()->getToolHandler();

    bool hidden = false;
    bool addBorder = false;
    Color color = toolHandler->getColor();

    switch (toolHandler->getToolType()) {
        case TOOL_PEN:
            addBorder = true;
            hidden = false;
            break;
        case TOOL_SELECT_PDF_TEXT_RECT:
        case TOOL_SELECT_PDF_TEXT_LINEAR:
            addBorder = false;
            hidden = false;
            break;
        default:
            hidden = true;
            break;
    }

    if (!hidden) {
        this->addBorder = addBorder;
        this->color = color;

        this->updateSelectedColorItem();

        if (this->selectedColorItem != nullptr) {
            this->updateCoordinates();
            this->updatePreviewImage();
            this->updateScaleValue();
        }
        this->show();
    } else {
        this->hide();
    }
}

void OpacityPreviewToolbox::updateSelectedColorItem() {
    const std::vector<ColorToolItem*> colorItems = this->theMainWindow->getToolMenuHandler()->getColorToolItems();

    this->selectedColorItem = nullptr;

    for (const ColorToolItem* colorItem: colorItems) {
        Color toolColor = color;
        // Ignore alpha channel to compare tool color with button color
        toolColor.alpha = 0;
        if (toolColor == colorItem->getColor() && colorItem->getToolDisplayName() != "Custom Color") {
            this->selectedColorItem = colorItem;
        }
    }
}

void OpacityPreviewToolbox::updateCoordinates() {
    Color color = this->color;
    color.alpha = 0;

    // Disregarding alpha channel, if the selected color matches the widget's color,
    // the widget is already in the correct position.
    // Coordinates don't need to be updated
    if (color == this->selectedColorItem->getColor()) {
        GtkWidget* selectedColorWidget = GTK_WIDGET(this->selectedColorItem->getItem());
        GtkWidget* overlayWidget = GTK_WIDGET(overlay.get());

        // Copy coordinates of selectedColorWidget in this->position.x and this->position.y
        // using overlay's coordinate space
        gtk_widget_translate_coordinates(selectedColorWidget, overlayWidget, 0, 0, &this->position.x,
                                         &this->position.y);

        // Below the color button
        this->position.y += gtk_widget_get_allocated_height(selectedColorWidget);
    }
}

void OpacityPreviewToolbox::updateScaleValue() {
    GtkRange* rangeWidget = (GtkRange*)this->theMainWindow->get("opacityPreviewToolScaleAlpha");
    gtk_range_set_value(rangeWidget, byteToPercent(this->color.alpha));
}

void OpacityPreviewToolbox::updatePreviewImage() {
    xoj::util::CairoSurfaceSPtr surface(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, PREVIEW_WIDTH, PREVIEW_HEIGHT),
                                        xoj::util::adopt);
    xoj::util::CairoSPtr cairo(cairo_create(surface.get()), xoj::util::adopt);
    cairo_t* cr = cairo.get();

    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgb(cr, 255, 255, 255);
    cairo_paint(cr);

    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    Util::cairo_set_source_argb(cr, color);
    cairo_rectangle(cr, PREVIEW_BORDER, PREVIEW_BORDER, PREVIEW_WIDTH - PREVIEW_BORDER * 2,
                    PREVIEW_HEIGHT - PREVIEW_BORDER * 2);
    cairo_fill(cr);

    if (addBorder) {
        cairo_set_line_width(cr, 5);
        cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
        Color borderColor = color;
        borderColor.alpha = 255;
        Util::cairo_set_source_argb(cr, borderColor);
        cairo_rectangle(cr, PREVIEW_BORDER, PREVIEW_BORDER, PREVIEW_WIDTH - PREVIEW_BORDER * 2,
                        PREVIEW_HEIGHT - PREVIEW_BORDER * 2);
        cairo_stroke(cr);
    }

    xoj::util::GObjectSPtr<GdkPixbuf> pixbuf(
            gdk_pixbuf_get_from_surface(surface.get(), 0, 0, PREVIEW_WIDTH, PREVIEW_HEIGHT), xoj::util::adopt);
    gtk_image_set_from_pixbuf(GTK_IMAGE(theMainWindow->get("opacityPreviewToolImg")), pixbuf.get());
}
OpacityPreviewToolbox::~OpacityPreviewToolbox() = default;

void OpacityPreviewToolbox::show() {
    this->hidden = false;
    gtk_widget_hide(this->opacityPreviewToolbox);  // force showing in new position
    gtk_widget_show_all(this->opacityPreviewToolbox);
}

void OpacityPreviewToolbox::hide() {
    this->hidden = true;

    if (isHidden())
        return;

    gtk_widget_hide(this->opacityPreviewToolbox);
}

auto OpacityPreviewToolbox::getOverlayPosition(GtkOverlay* overlay, GtkWidget* widget, GdkRectangle* allocation,
                                               OpacityPreviewToolbox* self) -> gboolean {
    if (widget == self->opacityPreviewToolbox && !self->hidden) {
        // Get existing width and height
        GtkRequisition natural;
        gtk_widget_get_preferred_size(widget, nullptr, &natural);
        allocation->width = natural.width;
        allocation->height = natural.height;

        // Make sure the "OpacityPreviewToolbox" is fully displayed.
        //        const int gap = 5;
        const int gap = 0;

        int colorItem_width = gtk_widget_get_allocated_width(GTK_WIDGET(self->selectedColorItem->getItem()));

        // Adjust self->position.x to center it vertically with selected color item.
        int offset_x = static_cast<int>(std::round((colorItem_width - allocation->width) / 2));
        int adjusted_position_x = self->position.x + offset_x;

        // If the toolbox will go out of the window, then we'll flip the corresponding directions.
        GtkAllocation windowAlloc{};
        gtk_widget_get_allocation(GTK_WIDGET(overlay), &windowAlloc);

        bool rightOK = adjusted_position_x + allocation->width + gap <= windowAlloc.width;
        bool bottomOK = self->position.y + allocation->height + gap <= windowAlloc.height;

        allocation->x = rightOK ? adjusted_position_x + gap : adjusted_position_x - allocation->width - gap;
        allocation->y = bottomOK ? self->position.y + gap : self->position.y - allocation->height - gap;

        return true;
    }

    return false;
}

bool OpacityPreviewToolbox::isHidden() const { return !gtk_widget_is_visible(this->opacityPreviewToolbox); }
