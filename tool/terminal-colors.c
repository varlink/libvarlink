#include "terminal-colors.h"

#include <unistd.h>

const char *terminal_color(const char *color) {
        static int is_tty = -1;

        if (is_tty == -1)
                is_tty = isatty(STDOUT_FILENO);

        if (!is_tty)
                return "";

        return color;
}

