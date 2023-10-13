/*
 * Xournal++
 *
 * Logging class for debugging
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#include "util/ODebug.h"

#include <cstdarg>
#include <memory>
#include <sstream>
#include <unordered_set>
#include <vector>

#include <glib.h>  // for gboolean
#include <string.h>

bool ODebuggable::excludes_initialized = false;
std::vector<std::string> ODebuggable::entered_functions;
std::unordered_set<std::string> ODebuggable::excludes;

ODebuggable::~ODebuggable() {}

ODebuggable::ODebuggable(std::string classname): classname(classname) {
    if (!excludes_initialized) {
        init_excludes();
    }
}

void ODebuggable::init_excludes() {
    char* odebug_excludes_str = std::getenv("ODEBUG_EXCLUDES");

    if (odebug_excludes_str != nullptr) {
        char delimiter = ',';

        // Use a string stream to split the input string
        std::istringstream iss(odebug_excludes_str);
        std::string odebug_excludes_token;

        while (std::getline(iss, odebug_excludes_token, delimiter)) {
            // Add each token to the unordered set
            excludes.insert(odebug_excludes_token);
        }
    }
    excludes_initialized = true;
}


void ODebuggable::odebug_current_func(const gchar* format, ...) {
    std::string last_funcname = entered_functions.back();
    va_list args;
    va_start(args, format);
    this->odebugv(last_funcname.c_str(), format, args);
    va_end(args);
}

void ODebuggable::odebug(const gchar* log_domain, const gchar* format, ...) {
    va_list args;
    va_start(args, format);
    odebugv(log_domain, format, args);
    va_end(args);
}

static bool inline endsWithAsterisk(std::string expr) { return expr[expr.length() - 1] == '*'; }

void ODebuggable::odebugv(const gchar* log_domain, const gchar* format, va_list args) {
    if (!excludes_initialized) {
        init_excludes();
    }

    bool match_exclude = false;

    // Iterate over values passed in ODEBUG_EXCLUDES
    // until one value matches log_domain
    for (std::unordered_set<std::string>::iterator it = excludes.begin(); it != excludes.end() && !match_exclude;
         ++it) {
        std::string exclude = *it;

        if (endsWithAsterisk(exclude)) {
            // ODEBUG_EXCLUDES contains here an element of the form <expr*>
            // if log_domain matches <expr*>, i.e. log_domain starts with <expr>,
            // this log event should be skipped.
            match_exclude = strncmp(log_domain, exclude.c_str(), exclude.length() - 1) == 0;
        } else {
            // Else, this log event should be skipped
            // only if 'log_domain' matches exactly 'exclude'
            match_exclude = strcmp(log_domain, exclude.c_str()) == 0;
        }
    }

    if (!match_exclude) {
        g_logv(log_domain, G_LOG_LEVEL_DEBUG, format, args);
    }
}

void ODebuggable::odebug_enter(const gchar* function_name) {
    std::string qualified_funcname = this->classname + "::" + function_name;
    this->odebug(qualified_funcname.c_str(), "--> Entered");
    entered_functions.push_back(qualified_funcname);
}

void ODebuggable::odebug_exit() {
    std::string last_funcname = entered_functions.back();
    this->odebug(last_funcname.c_str(), "--> Exited");
    entered_functions.pop_back();
}
