#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vf128.h"
#include "musvg.h"

typedef enum {
    format_none,
    format_text,
    format_xml,
    format_binary,
} format_t;

format_t parse_format(const char *format)
{
    if (strcmp(format, "text") == 0) {
        return format_text;
    } else if (strcmp(format, "xml") == 0) {
        return format_xml;
    } else if (strcmp(format, "binary") == 0) {
        return format_binary;
    } else {
        return format_none;
    }
}

int check_opt(const char *arg, const char* sopt, const char *lopt)
{
    return (strcmp(arg, sopt) == 0 || strcmp(arg, lopt) == 0);
}

int main(int argc, char **argv)
{
    musvg_parser *p;
    const char* input_filename = NULL;
    format_t input_format = format_xml;
    format_t output_format = format_xml;
    int help_exit = 0;

    int i = 1;
    while (i < argc) {
        if (check_opt(argv[i],"-f","--input-file")   && i + 1 < argc) {
            input_filename = argv[++i];
        } else if (check_opt(argv[i],"-i","--input-format") && i + 1 < argc) {
            input_format = parse_format(argv[++i]);
        } else if (check_opt(argv[i],"-o","--output-format") && i + 1 < argc) {
            output_format = parse_format(argv[++i]);
        } else if (check_opt(argv[i],"-d","--debug")) {
            vf_set_debug(1);
        } else if (check_opt(argv[i],"-h","--help")) {
            help_exit = 1;
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

    if (help_exit) {
        fprintf(stderr,
            "\nusage: %s [options]\n"
            "\n"
            "-f,--input-file <filename>            input filename\n"
            "-i,--input-format (xml|binary)        input format\n"
            "-o,--output-format (xml|text|binary)  output format\n"
            "-d,--debug                            debug messages\n"
            "-h,--help                             help information\n",
            argv[0]);
        exit(1);
    }

    switch (input_format) {
    case format_text: fprintf(stderr, "text input not supported\n"); exit(1);
    case format_xml: p = musvg_parse_xml_file(input_filename); break;
    case format_binary: p = musvg_parse_binary_file(input_filename); break;
    }

    switch (output_format) {
    case format_text: musvg_emit_text(p); break;
    case format_xml: musvg_emit_xml(p); break;
    case format_binary: musvg_emit_binary(p); break;
    }
    musvg_parser_destroy(p);

    return 0;
}
