#include "OpacityPreviewToolbox.h"

#include <cmath>

#include <cairo.h>        // for cairo_set_operator, cairo_rectangle, cairo_...
#include <glib-object.h>  // for G_CALLBACK, g_signal_connect
#include <glib.h>         // for gdouble

#include "control/Control.h"      // for Tool...
#include "control/ToolHandler.h"  // for Tool...
#include "util/Color.h"
#include "util/raii/CairoWrappers.h"
#include "util/raii/GObjectSPtr.h"

#include "MainWindow.h"  // for MainWindow

static int percentToByte(double percent) { return static_cast<int>(std::round(percent * 2.55)); }

OpacityPreviewToolbox::OpacityPreviewToolbox(MainWindow* theMainWindow, GtkOverlay* overlay):
        theMainWindow(theMainWindow), overlay(overlay, xoj::util::ref), position({0, 0}) {
    this->opacityPreviewToolbox = theMainWindow->get("opacityPreviewTool");

    gtk_overlay_add_overlay(overlay, this->opacityPreviewToolbox);
    gtk_overlay_set_overlay_pass_through(overlay, this->opacityPreviewToolbox, true);

    g_signal_connect(overlay, "get-child-position", G_CALLBACK(this->getOverlayPosition), this);

    g_signal_connect(theMainWindow->get("opacityPreviewToolScaleAlpha"), "change-value", G_CALLBACK(this->changeValue),
                     this);
    this->hide();
}

void OpacityPreviewToolbox::changeValue(GtkRange* range, GtkScrollType scroll, gdouble value,
                                        OpacityPreviewToolbox* self) {
    Color color = self->color;
    color.alpha = static_cast<uint8_t>(percentToByte(value));
    self->update(color, self->addBorder);
    gtk_range_set_value(range, value);

    ToolHandler* toolHandler = self->theMainWindow->getControl()->getToolHandler();
    switch (toolHandler->getToolType()) {
        case TOOL_SELECT_PDF_TEXT_RECT:
        case TOOL_SELECT_PDF_TEXT_LINEAR:
            toolHandler->setSelectPDFTextMarkerOpacity(color.alpha);
            break;
        default:
            toolHandler->setColor(color, true);
            break;
    }
}

const int PREVIEW_WIDTH = 70;
const int PREVIEW_HEIGHT = 50;
const int PREVIEW_BORDER = 10;

void OpacityPreviewToolbox::update(Color color, bool addBorder) {
    this->color = color;
    this->addBorder = addBorder;

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
        color.alpha = 255;
        Util::cairo_set_source_argb(cr, color);
        cairo_rectangle(cr, PREVIEW_BORDER, PREVIEW_BORDER, PREVIEW_WIDTH - PREVIEW_BORDER * 2,
                        PREVIEW_HEIGHT - PREVIEW_BORDER * 2);
        cairo_stroke(cr);
    }

    xoj::util::GObjectSPtr<GdkPixbuf> pixbuf(
            gdk_pixbuf_get_from_surface(surface.get(), 0, 0, PREVIEW_WIDTH, PREVIEW_HEIGHT), xoj::util::adopt);
    gtk_image_set_from_pixbuf(GTK_IMAGE(theMainWindow->get("opacityPreviewToolImg")), pixbuf.get());
}
OpacityPreviewToolbox::~OpacityPreviewToolbox() = default;

void OpacityPreviewToolbox::show(int x, int y) {
    // (x, y) are in the gtk window's coordinates.
    // However, we actually show the toolbox in the overlay's coordinate system.
    gtk_widget_translate_coordinates(gtk_widget_get_toplevel(this->opacityPreviewToolbox), GTK_WIDGET(overlay.get()), x,
                                     y, &this->position.x, &this->position.y);
    this->show();
}

void OpacityPreviewToolbox::hide() {
    if (isHidden())
        return;

    gtk_widget_hide(this->opacityPreviewToolbox);
}

auto OpacityPreviewToolbox::getOverlayPosition(GtkOverlay* overlay, GtkWidget* widget, GdkRectangle* allocation,
                                               OpacityPreviewToolbox* self) -> gboolean {
    if (widget == self->opacityPreviewToolbox) {
        // Get existing width and height
        GtkRequisition natural;
        gtk_widget_get_preferred_size(widget, nullptr, &natural);
        allocation->width = natural.width;
        allocation->height = natural.height;

        // Make sure the "OpacityPreviewToolbox" is fully displayed.
        const int gap = 5;

        // By default, we show the toolbox below and to the right of the selected text.
        // If the toolbox will go out of the window, then we'll flip the corresponding directions.

        GtkAllocation windowAlloc{};
        gtk_widget_get_allocation(GTK_WIDGET(overlay), &windowAlloc);

        bool rightOK = self->position.x + allocation->width + gap <= windowAlloc.width;
        bool bottomOK = self->position.y + allocation->height + gap <= windowAlloc.height;

        allocation->x = rightOK ? self->position.x + gap : self->position.x - allocation->width - gap;
        allocation->y = bottomOK ? self->position.y + gap : self->position.y - allocation->height - gap;

        return true;
    }

    return false;
}

void OpacityPreviewToolbox::show() {
    gtk_widget_hide(this->opacityPreviewToolbox);  // force showing in new position
    gtk_widget_show_all(this->opacityPreviewToolbox);
}


bool OpacityPreviewToolbox::isHidden() const { return !gtk_widget_is_visible(this->opacityPreviewToolbox); }
