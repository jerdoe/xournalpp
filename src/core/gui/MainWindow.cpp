#include "MainWindow.h"

#include <config-dev.h>             // for TOOLBAR_CONFIG
#include <gdk-pixbuf/gdk-pixbuf.h>  // for gdk_pixbuf_new_fr...
#include <gdk/gdk.h>                // for gdk_screen_get_de...
#include <gio/gio.h>                // for g_cancellable_is_...
#include <gtk/gtkcssprovider.h>     // for gtk_css_provider_...

#include "control/AudioController.h"                    // for AudioController
#include "control/Control.h"                            // for Control
#include "control/DeviceListHelper.h"                   // for getSourceMapping
#include "control/ScrollHandler.h"                      // for ScrollHandler
#include "control/actions/ActionDatabase.h"             // for ActionDatabase
#include "control/jobs/XournalScheduler.h"              // for XournalScheduler
#include "control/layer/LayerController.h"              // for LayerController
#include "control/settings/Settings.h"                  // for Settings
#include "control/settings/SettingsEnums.h"             // for SCROLLBAR_HIDE_HO...
#include "control/zoom/ZoomControl.h"                   // for ZoomControl
#include "gui/FloatingToolbox.h"                        // for FloatingToolbox
#include "gui/GladeGui.h"                               // for GladeGui
#include "gui/PdfFloatingToolbox.h"                     // for PdfFloatingToolbox
#include "gui/SearchBar.h"                              // for SearchBar
#include "gui/inputdevices/InputEvents.h"               // for INPUT_DEVICE_TOUC...
#include "gui/menus/menubar/Menubar.h"                  // for Menubar
#include "gui/menus/menubar/ToolbarSelectionSubmenu.h"  // for ToolbarSelectionSubmenu
#include "gui/scroll/ScrollHandling.h"                  // for ScrollHandling
#include "gui/toolbarMenubar/ToolMenuHandler.h"         // for ToolMenuHandler
#include "gui/toolbarMenubar/model/ToolbarData.h"       // for ToolbarData
#include "gui/toolbarMenubar/model/ToolbarModel.h"      // for ToolbarModel
#include "gui/widgets/SpinPageAdapter.h"                // for SpinPageAdapter
#include "gui/widgets/XournalWidget.h"                  // for gtk_xournal_get_l...
#include "util/GListView.h"                             // for GListView, GListV...
#include "util/PathUtil.h"                              // for getConfigFile
#include "util/Util.h"                                  // for execInUiThread, npos
#include "util/XojMsgBox.h"                             // for XojMsgBox
#include "util/glib_casts.h"                            // for wrap_for_once_v
#include "util/i18n.h"                                  // for FS, _F

#include "GladeSearchpath.h"     // for GladeSearchpath
#include "ToolbarDefinitions.h"  // for TOOLBAR_DEFINITIO...
#include "XournalView.h"         // for XournalView
#include "filesystem.h"          // for path, exists

using std::string;

MainWindow::MainWindow(GladeSearchpath* gladeSearchPath, Control* control, GtkApplication* parent):
        GladeGui(gladeSearchPath, "main.glade", "mainWindow"),
        control(control),
        toolbar(std::make_unique<ToolMenuHandler>(control, this)),
        menubar(std::make_unique<Menubar>()) {
    gtk_window_set_application(GTK_WINDOW(getWindow()), parent);

    panedContainerWidget.reset(get("panelMainContents"), xoj::util::ref);
    boxContainerWidget.reset(get("mainContentContainer"), xoj::util::ref);
    mainContentWidget.reset(get("boxContents"), xoj::util::ref);
    sidebarWidget.reset(get("sidebar"), xoj::util::ref);

    GtkSettings* appSettings = gtk_settings_get_default();
    g_object_set(appSettings, "gtk-application-prefer-dark-theme", control->getSettings()->isDarkTheme(), nullptr);

    loadMainCSS(gladeSearchPath, "xournalpp.css");

    GtkOverlay* overlay = GTK_OVERLAY(get("mainOverlay"));
    this->pdfFloatingToolBox = std::make_unique<PdfFloatingToolbox>(this, overlay);
    this->floatingToolbox = std::make_unique<FloatingToolbox>(this, overlay);

    for (size_t i = 0; i < TOOLBAR_DEFINITIONS_LEN; i++) {
        this->toolbarWidgets[i].reset(get(TOOLBAR_DEFINITIONS[i].guiName), xoj::util::ref);
    }

    initXournalWidget();

    setSidebarVisible(control->getSettings()->isSidebarVisible());

    // Window handler
    g_signal_connect(this->window, "delete-event", xoj::util::wrap_for_g_callback_v<deleteEventCallback>,
                     this->control);
    g_signal_connect(this->window, "window_state_event", xoj::util::wrap_for_g_callback_v<windowStateEventCallback>,
                     this);

    g_signal_connect(get("buttonCloseSidebar"), "clicked", xoj::util::wrap_for_g_callback_v<buttonCloseSidebarClicked>,
                     this);

    // "watch over" all key events
    g_signal_connect(this->window, "key-press-event", G_CALLBACK(gtk_window_propagate_key_event), nullptr);
    g_signal_connect(this->window, "key-release-event", G_CALLBACK(gtk_window_propagate_key_event), nullptr);

    updateScrollbarSidebarPosition();

    gtk_window_set_default_size(GTK_WINDOW(this->window), control->getSettings()->getMainWndWidth(),
                                control->getSettings()->getMainWndHeight());

    if (control->getSettings()->isMainWndMaximized()) {
        gtk_window_maximize(GTK_WINDOW(this->window));
    } else {
        gtk_window_unmaximize(GTK_WINDOW(this->window));
    }

    // Drag and Drop
    g_signal_connect(this->window, "drag-data-received", G_CALLBACK(dragDataRecived), this);

    gtk_drag_dest_set(this->window, GTK_DEST_DEFAULT_ALL, nullptr, 0, GDK_ACTION_COPY);
    gtk_drag_dest_add_uri_targets(this->window);
    gtk_drag_dest_add_image_targets(this->window);
    gtk_drag_dest_add_text_targets(this->window);
}

void MainWindow::populate(GladeSearchpath* gladeSearchPath) {

    toolbar->populate(gladeSearchPath);
    menubar->populate(gladeSearchPath, this);

    // need to create tool buttons registered in plugins, so they can be added to toolbars
    control->registerPluginToolButtons(this->toolbar.get());

    createToolbar();

    setToolbarVisible(control->getSettings()->isToolbarVisible());
    getSpinPageNo()->addListener(this->control->getScrollHandler());
}

GMenuModel* MainWindow::getMenuModel() const { return menubar->getModel(); }

MainWindow::~MainWindow() = default;

void MainWindow::updateColorscheme() {
    bool darkMode = control->getSettings()->isDarkTheme();
    GtkStyleContext* context = gtk_widget_get_style_context(GTK_WIDGET(this->window));

    if (darkMode) {
        gtk_style_context_add_class(context, "darkMode");
    } else {
        gtk_style_context_remove_class(context, "darkMode");
    }
}

void MainWindow::initXournalWidget() {
    GtkWidget* boxContents = get("boxContents");


    winXournal = gtk_scrolled_window_new(nullptr, nullptr);

    setGtkTouchscreenScrollingForDeviceMapping();

    gtk_container_add(GTK_CONTAINER(boxContents), winXournal);

    GtkWidget* vpXournal = gtk_viewport_new(nullptr, nullptr);

    gtk_container_add(GTK_CONTAINER(winXournal), vpXournal);

    scrollHandling = std::make_unique<ScrollHandling>(GTK_SCROLLABLE(vpXournal));

    this->xournal = std::make_unique<XournalView>(vpXournal, control, scrollHandling.get());

    control->getZoomControl()->initZoomHandler(this->window, winXournal, xournal.get(), control);
    gtk_widget_show_all(winXournal);

    Layout* layout = gtk_xournal_get_layout(this->xournal->getWidget());
    scrollHandling->init(this->xournal->getWidget(), layout);

    updateColorscheme();
}

void MainWindow::setGtkTouchscreenScrollingForDeviceMapping() {
    InputDeviceClass touchscreenClass =
            DeviceListHelper::getSourceMapping(GDK_SOURCE_TOUCHSCREEN, this->getControl()->getSettings());

    setGtkTouchscreenScrollingEnabled(touchscreenClass == INPUT_DEVICE_TOUCHSCREEN &&
                                      !control->getSettings()->getTouchDrawingEnabled());
}

void MainWindow::setGtkTouchscreenScrollingEnabled(bool enabled) {
    if (!control->getSettings()->getGtkTouchInertialScrollingEnabled()) {
        enabled = false;
    }

    if (enabled == gtkTouchscreenScrollingEnabled.load() || winXournal == nullptr) {
        return;
    }

    gtkTouchscreenScrollingEnabled.store(enabled);

    Util::execInUiThread(
            [=]() {
                const bool touchScrollEnabled = gtkTouchscreenScrollingEnabled.load();

                gtk_scrolled_window_set_kinetic_scrolling(GTK_SCROLLED_WINDOW(winXournal), touchScrollEnabled);
            },
            G_PRIORITY_HIGH);
}

auto MainWindow::getLayout() const -> Layout* { return gtk_xournal_get_layout(this->xournal->getWidget()); }

auto MainWindow::getNegativeXournalWidgetPos() const -> utl::Point<double> {
    return Util::toWidgetCoords(this->winXournal, utl::Point{0.0, 0.0});
}

auto cancellable_cancel(GCancellable* cancel) -> bool {
    g_cancellable_cancel(cancel);

    g_warning("Timeout... Cancel loading URL");

    return false;
}

void MainWindow::dragDataRecived(GtkWidget* widget, GdkDragContext* dragContext, gint x, gint y, GtkSelectionData* data,
                                 guint info, guint time, MainWindow* win) {
    GtkWidget* source = gtk_drag_get_source_widget(dragContext);
    if (source && widget == gtk_widget_get_toplevel(source)) {
        gtk_drag_finish(dragContext, false, false, time);
        return;
    }

    guchar* text = gtk_selection_data_get_text(data);
    if (text) {
        win->control->clipboardPasteText(reinterpret_cast<const char*>(text));

        g_free(text);
        gtk_drag_finish(dragContext, true, false, time);
        return;
    }

    xoj::util::GObjectSPtr<GdkPixbuf> image(gtk_selection_data_get_pixbuf(data), xoj::util::adopt);
    if (image) {
        win->control->clipboardPasteImage(image.get());

        gtk_drag_finish(dragContext, true, false, time);
        return;
    }

    gchar** uris = gtk_selection_data_get_uris(data);
    if (uris) {
        for (int i = 0; uris[i] != nullptr && i < 3; i++) {
            const char* uri = uris[i];

            GCancellable* cancel = g_cancellable_new();
            auto cancelTimeout = g_timeout_add(3000, xoj::util::wrap_for_once_v<cancellable_cancel>, cancel);

            xoj::util::GObjectSPtr<GFile> file(g_file_new_for_uri(uri), xoj::util::adopt);
            GError* err = nullptr;
            GFileInputStream* in = g_file_read(file.get(), cancel, &err);
            if (g_cancellable_is_cancelled(cancel)) {
                continue;
            }

            if (err == nullptr) {
                xoj::util::GObjectSPtr<GdkPixbuf> pixbuf(
                        gdk_pixbuf_new_from_stream(G_INPUT_STREAM(in), cancel, nullptr), xoj::util::adopt);
                if (g_cancellable_is_cancelled(cancel)) {
                    continue;
                }
                g_input_stream_close(G_INPUT_STREAM(in), cancel, nullptr);
                if (g_cancellable_is_cancelled(cancel)) {
                    continue;
                }

                if (pixbuf) {
                    win->control->clipboardPasteImage(pixbuf.get());
                }
            } else {
                g_error_free(err);
            }

            if (!g_cancellable_is_cancelled(cancel)) {
                g_source_remove(cancelTimeout);
            }
            g_object_unref(cancel);
        }

        gtk_drag_finish(dragContext, true, false, time);

        g_strfreev(uris);
    }

    gtk_drag_finish(dragContext, false, false, time);
}

auto MainWindow::getControl() const -> Control* { return control; }

void MainWindow::updateScrollbarSidebarPosition() {
    GtkWidget* panelMainContents = get("panelMainContents");

    if (winXournal != nullptr) {
        GtkScrolledWindow* scrolledWindow = GTK_SCROLLED_WINDOW(winXournal);

        ScrollbarHideType type = this->getControl()->getSettings()->getScrollbarHideType();

        bool scrollbarOnLeft = control->getSettings()->isScrollbarOnLeft();
        if (scrollbarOnLeft) {
            gtk_scrolled_window_set_placement(scrolledWindow, GTK_CORNER_TOP_RIGHT);
        } else {
            gtk_scrolled_window_set_placement(scrolledWindow, GTK_CORNER_TOP_LEFT);
        }

        gtk_widget_set_visible(gtk_scrolled_window_get_hscrollbar(scrolledWindow), !(type & SCROLLBAR_HIDE_HORIZONTAL));
        gtk_widget_set_visible(gtk_scrolled_window_get_vscrollbar(scrolledWindow), !(type & SCROLLBAR_HIDE_VERTICAL));

        gtk_scrolled_window_set_overlay_scrolling(scrolledWindow,
                                                  !control->getSettings()->isScrollbarFadeoutDisabled());
    }

    // If the sidebar isn't visible, we can't change its position!
    if (!this->sidebarVisible) {
        return;
    }

    GtkWidget* sidebar = get("sidebar");
    GtkWidget* boxContents = get("boxContents");

    int divider = gtk_paned_get_position(GTK_PANED(panelMainContents));
    bool sidebarRight = control->getSettings()->isSidebarOnRight();
    if (sidebarRight == (gtk_paned_get_child2(GTK_PANED(panelMainContents)) == sidebar)) {
        // Already correct
        return;
    }


    GtkAllocation allocation;
    gtk_widget_get_allocation(panelMainContents, &allocation);
    divider = allocation.width - divider;


    g_object_ref(sidebar);
    g_object_ref(boxContents);

    gtk_container_remove(GTK_CONTAINER(panelMainContents), sidebar);
    gtk_container_remove(GTK_CONTAINER(panelMainContents), boxContents);

    if (sidebarRight) {
        gtk_paned_pack1(GTK_PANED(panelMainContents), boxContents, true, false);
        gtk_paned_pack2(GTK_PANED(panelMainContents), sidebar, false, false);
    } else {
        gtk_paned_pack1(GTK_PANED(panelMainContents), sidebar, false, false);
        gtk_paned_pack2(GTK_PANED(panelMainContents), boxContents, true, false);
    }

    gtk_paned_set_position(GTK_PANED(panelMainContents), divider);
    g_object_unref(sidebar);
    g_object_unref(boxContents);
}

void MainWindow::buttonCloseSidebarClicked(GtkButton* button, MainWindow* win) {
    Settings* settings = win->control->getSettings();
    if (settings->getActiveViewMode() == PresetViewModeIds::VIEW_MODE_DEFAULT) {
        settings->setSidebarVisible(false);
        ViewMode viewMode = settings->getViewModes()[PresetViewModeIds::VIEW_MODE_DEFAULT];
        viewMode.showSidebar = false;
        settings->setViewMode(PresetViewModeIds::VIEW_MODE_DEFAULT, viewMode);
    }
    win->setSidebarVisible(false);
}

auto MainWindow::deleteEventCallback(GtkWidget* widget, GdkEvent* event, Control* control) -> bool {
    control->quit();

    return true;
}

void MainWindow::setSidebarVisible(bool visible) {
    if (!visible && (control->getSidebar() != nullptr)) {
        saveSidebarSize();
    }

    if (visible != this->sidebarVisible) {
        // Due to a GTK bug, we can't just hide the sidebar widget in the GtkPaned.
        // If we do this, we create a dead region where the pane separator was previously.
        // In this region, we can't use the touchscreen to start horizontal strokes.
        // As such:
        if (!visible) {
            gtk_container_remove(GTK_CONTAINER(panedContainerWidget.get()), mainContentWidget.get());
            gtk_container_remove(GTK_CONTAINER(boxContainerWidget.get()), panedContainerWidget.get());
            gtk_container_add(GTK_CONTAINER(boxContainerWidget.get()), mainContentWidget.get());
            this->sidebarVisible = false;
        } else {
            gtk_container_remove(GTK_CONTAINER(boxContainerWidget.get()), mainContentWidget.get());
            gtk_container_add(GTK_CONTAINER(panedContainerWidget.get()), mainContentWidget.get());
            gtk_container_add(GTK_CONTAINER(boxContainerWidget.get()), panedContainerWidget.get());
            this->sidebarVisible = true;

            updateScrollbarSidebarPosition();
        }
    }

    gtk_widget_set_visible(sidebarWidget.get(), visible);

    if (visible) {
        gtk_paned_set_position(GTK_PANED(panedContainerWidget.get()), control->getSettings()->getSidebarWidth());
    }
}

void MainWindow::setToolbarVisible(bool visible) {
    Settings* settings = control->getSettings();

    settings->setToolbarVisible(visible);
    for (auto& w: this->toolbarWidgets) {
        if (!visible || (gtk_toolbar_get_n_items(GTK_TOOLBAR(w.get())) != 0)) {
            gtk_widget_set_visible(w.get(), visible);
        }
    }
}

void MainWindow::setMenubarVisible(bool visible) {
    gtk_application_window_set_show_menubar(GTK_APPLICATION_WINDOW(this->getWindow()), visible);
}

void MainWindow::saveSidebarSize() {
    this->control->getSettings()->setSidebarWidth(gtk_paned_get_position(GTK_PANED(panedContainerWidget.get())));
}

void MainWindow::setMaximized(bool maximized) { this->maximized = maximized; }

auto MainWindow::isMaximized() const -> bool { return this->maximized; }

auto MainWindow::setFullscreen(bool enabled) const -> void {
    if (enabled) {
        gtk_window_fullscreen(GTK_WINDOW(this->getWindow()));
    } else {
        gtk_window_unfullscreen(GTK_WINDOW(this->getWindow()));
    }
}

auto MainWindow::getXournal() const -> XournalView* { return xournal.get(); }

auto MainWindow::windowStateEventCallback(GtkWidget* window, GdkEventWindowState* event, MainWindow* win) -> bool {
    win->setMaximized(gtk_window_is_maximized(GTK_WINDOW(window)));

    return false;
}

void MainWindow::toolbarSelected(const std::string& id) {
    const auto& toolbars = *toolbar->getModel()->getToolbars();
    auto it = std::find_if(toolbars.begin(), toolbars.end(), [&](const ToolbarData* d) { return d->getId() == id; });
    toolbarSelected(it == toolbars.end() ? nullptr : *it);
}

void MainWindow::toolbarSelected(ToolbarData* d) {
    if (!d || this->selectedToolbar == d) {
        return;
    }

    Settings* settings = control->getSettings();
    settings->setSelectedToolbar(d->getId());

    this->clearToolbar();
    this->loadToolbar(d);
}

auto MainWindow::clearToolbar() -> ToolbarData* {
    if (this->selectedToolbar != nullptr) {
        for (size_t i = 0; i < TOOLBAR_DEFINITIONS_LEN; i++) {
            ToolMenuHandler::unloadToolbar(this->toolbarWidgets[i].get());
        }

        this->toolbar->freeDynamicToolbarItems();
    }

    ToolbarData* oldData = this->selectedToolbar;

    this->selectedToolbar = nullptr;

    return oldData;
}

void MainWindow::loadToolbar(ToolbarData* d) {
    this->selectedToolbar = d;

    for (size_t i = 0; i < TOOLBAR_DEFINITIONS_LEN; i++) {
        this->toolbar->load(d, this->toolbarWidgets[i].get(), TOOLBAR_DEFINITIONS[i].propName,
                            TOOLBAR_DEFINITIONS[i].horizontal);
    }

    this->floatingToolbox->flagRecalculateSizeRequired();
}

auto MainWindow::getSelectedToolbar() const -> ToolbarData* { return this->selectedToolbar; }

auto MainWindow::getToolbarWidgets() const -> const ToolbarWidgetArray& { return toolbarWidgets; }

auto MainWindow::getToolbarName(GtkToolbar* toolbar) const -> const char* {
    for (size_t i = 0; i < TOOLBAR_DEFINITIONS_LEN; i++) {
        if (static_cast<void*>(this->toolbarWidgets[i].get()) == static_cast<void*>(toolbar)) {
            return TOOLBAR_DEFINITIONS[i].propName;
        }
    }

    return "";
}

void MainWindow::setDynamicallyGeneratedSubmenuDisabled(bool disabled) { menubar->setDisabled(disabled); }

void MainWindow::updateToolbarMenu() {
    menubar->getToolbarSelectionSubmenu().update(toolbar.get(), this->selectedToolbar);
}

void MainWindow::createToolbar() {
    toolbarSelected(control->getSettings()->getSelectedToolbar());

    this->control->getScheduler()->unblockRerenderZoom();
}

void MainWindow::updatePageNumbers(size_t page, size_t pagecount, size_t pdfpage) {
    SpinPageAdapter* spinPageNo = getSpinPageNo();
    if (!spinPageNo) {
        // Toolbar is not yet setup
        return;
    }

    size_t min = 0;
    size_t max = pagecount;

    if (pagecount == 0) {
        min = 0;
        page = 0;
    } else {
        min = 1;
        page++;
    }

    spinPageNo->setMinMaxPage(min, max);
    spinPageNo->setPage(page);

    if (pdfpage != npos) {
        toolbar->setPageInfo(pagecount, pdfpage + 1);
    } else {
        toolbar->setPageInfo(pagecount);
    }
}

auto MainWindow::getMenubar() const -> Menubar* { return menubar.get(); }

void MainWindow::show(GtkWindow* parent) { gtk_widget_show(this->window); }

void MainWindow::setUndoDescription(const string& description) {
    toolbar->setUndoDescription(description);
    menubar->setUndoDescription(description);
}

void MainWindow::setRedoDescription(const string& description) {
    toolbar->setRedoDescription(description);
    menubar->setRedoDescription(description);
}

auto MainWindow::getSpinPageNo() const -> SpinPageAdapter* { return toolbar->getPageSpinner(); }

auto MainWindow::getToolbarModel() const -> ToolbarModel* { return this->toolbar->getModel(); }

auto MainWindow::getToolMenuHandler() const -> ToolMenuHandler* { return this->toolbar.get(); }

void MainWindow::loadMainCSS(GladeSearchpath* gladeSearchPath, const gchar* cssFilename) {
    auto filepath = gladeSearchPath->findFile("", cssFilename);
    xoj::util::GObjectSPtr<GtkCssProvider> provider(gtk_css_provider_new(), xoj::util::adopt);
    gtk_css_provider_load_from_path(provider.get(), filepath.u8string().c_str(), nullptr);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(provider.get()),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

PdfFloatingToolbox* MainWindow::getPdfToolbox() const { return this->pdfFloatingToolBox.get(); }

FloatingToolbox* MainWindow::getFloatingToolbox() const { return this->floatingToolbox.get(); }
