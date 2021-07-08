#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "musvg.h"

typedef enum {
    format_none,
    format_text,
    format_html,
    format_binary,
} format_t;

int main(int argc, char **argv)
{
    const char* input_filename = NULL;
    musvg_parser *p;
    format_t format = format_none;
    int help_exit = 0;

    int i = 1;
    while (i < argc) {
        if (argc == 3 && strcmp(argv[i],"--debug") == 0) {
            musvg_set_debug(1);
        } else if (strcmp(argv[i],"--text") == 0) {
            format = format_text;
        } else if (strcmp(argv[i],"--html") == 0) {
            format = format_html;
        } else if (strcmp(argv[i],"--binary") == 0) {
            format = format_binary;
        } else if (strcmp(argv[i],"--help") == 0) {
            help_exit = 1;
        } else if (strcmp(argv[i],"--input") == 0 && i + 1 < argc) {
            input_filename = argv[++i];
        } else {
            fprintf(stderr, "*** error: unknown option: %s\n", argv[i]);
            help_exit = 1;
            break;
        }
        i++;
    }

    if (!input_filename) {
        fprintf(stderr, "*** error: missing input filename\n");
        help_exit = 1;
    }

    if (format == format_none) {
        fprintf(stderr, "*** error: missing format (--text|--html|--binary)\n");
        help_exit = 1;
    }

    if (help_exit) {
        fprintf(stderr, "\nusage: %s [options]\n\n"
                "formats:\n"
                "--input <filename>   input SVG filename\n"
                "--text               output in text format\n"
                "--html               output in html format\n"
                "--binary             output in binary format\n\n"
                "options:\n"
                "--debug              enable debug messages\n"
                "--help               print help information\n", argv[0]);
        exit(1);
    }

    p = musvg_parse_file(input_filename);
    switch (format) {
    case format_text: musvg_emit_text(p); break;
    case format_binary: musvg_emit_binary(p); break;
    case format_html: musvg_emit_html(p); break;
    }
    musvg_parser_destroy(p);

    return 0;
}
