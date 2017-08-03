#pragma once

#define TERMINAL_NORMAL "\x1B[0m"
#define TERMINAL_BOLD "\x1B[0;1;39m"

#define TERMINAL_RED "\x1B[0;31m"
#define TERMINAL_GREEN "\x1B[0;32m"
#define TERMINAL_YELLOW "\x1B[0;33m"
#define TERMINAL_BLUE "\x1B[0;34m"
#define TERMINAL_MAGENTA "\x1B[0;35m"
#define TERMINAL_CYAN "\x1B[0;36m"
#define TERMINAL_GRAY "\x1B[0;90m"

#define TERMINAL_RED_BOLD "\x1B[0;1;31m"
#define TERMINAL_GREEN_BOLD "\x1B[0;1;32m"
#define TERMINAL_YELLOW_BOLD "\x1B[0;1;33m"
#define TERMINAL_BLUE_BOLD "\x1B[0;1;34m"
#define TERMINAL_BLUE_MAGENTA "\x1B[0;1;35m"
#define TERMINAL_CYAN_BOLD "\x1B[0;1;36m"
#define TERMINAL_GRAY_BOLD "\x1B[0;1;90m"

#define TERMINAL_UNDERLINE "\x1B[0;4m"
#define TERMINAL_UNDERLINE_BOLD "\x1B[0;1;4m"

const char *terminal_color(const char *color);
