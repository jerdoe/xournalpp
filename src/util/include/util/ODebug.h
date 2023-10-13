/*
 * Xournal++
 *
 * Base class for logging messages more easily
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <cstdarg>
#include <memory>
#include <sstream>
#include <unordered_set>
#include <vector>

#include <glib.h>  // for gboolean

class ODebuggable {
private:
    std::string classname;
    static std::vector<std::string> entered_functions;
    static std::unordered_set<std::string> excludes;
    static bool excludes_initialized;

protected:
    ODebuggable(std::string classname);

public:
    virtual ~ODebuggable();

    ODebuggable() = delete;
    ODebuggable(ODebuggable const&) = delete;
    ODebuggable(ODebuggable&&) = delete;
    auto operator=(ODebuggable const&) -> ODebuggable& = delete;
    auto operator=(ODebuggable&&) -> ODebuggable& = delete;

private:
    static void init_excludes();

    void odebug(const gchar* log_domain, const gchar* format, ...);
    void odebugv(const gchar* log_domain, const gchar* format, va_list args);

public:
    void odebug_enter(const gchar* function_name);
    void odebug_current_func(const gchar* format, ...);
    void odebug_exit();
};
