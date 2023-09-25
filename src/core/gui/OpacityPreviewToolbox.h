#pragma once

#include <cstdint>  // for uint8_t
#include <memory>   // for unique_ptr

#include <gdk/gdk.h>  // for GdkRectangle
#include <glib.h>     // for gboolean
#include <gtk/gtk.h>  // for GtkButton, GtkOverlay

#include "util/Color.h"             // for Color
#include "util/raii/GObjectSPtr.h"  // for GObjectSPtr

class MainWindow;

class OpacityPreviewToolbox {
public:
    OpacityPreviewToolbox(MainWindow* theMainWindow, GtkOverlay* overlay);
    OpacityPreviewToolbox& operator=(const OpacityPreviewToolbox&) = delete;
    OpacityPreviewToolbox(const OpacityPreviewToolbox&) = delete;
    OpacityPreviewToolbox& operator=(OpacityPreviewToolbox&&) = delete;
    OpacityPreviewToolbox(OpacityPreviewToolbox&&) = delete;
    ~OpacityPreviewToolbox();

public:
    /// Show the toolbox at the provided coordinates (relative to the application GTK window).
    ///
    /// Must have an active selection.
    void show(int x, int y);

    /// Hide the floating toolbox widget (keeping the current selection).
    void hide();

    /// Returns true if the toolbox is currently hidden.
    bool isHidden() const;

    void update(Color color, bool addBorder);

private:
    void show();
    static void changeValue(GtkRange* range, GtkScrollType scroll, gdouble value, OpacityPreviewToolbox* self);

    static gboolean getOverlayPosition(GtkOverlay* overlay, GtkWidget* widget, GdkRectangle* allocation,
                                       OpacityPreviewToolbox* self);

private:
    GtkWidget* opacityPreviewToolbox;
    MainWindow* theMainWindow;

    /// The overlay that the toolbox should be displayed in.
    xoj::util::GObjectSPtr<GtkOverlay> overlay;

    Color color;
    bool addBorder;

    struct {
        int x;
        int y;
    } position;
};
