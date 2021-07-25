#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mubuf.h"
#include "musvg.h"

int check_opt(const char *arg, const char* sopt, const char *lopt)
{
    return (strcmp(arg, sopt) == 0 || strcmp(arg, lopt) == 0);
}

int main(int argc, char **argv)
{
    musvg_parser *p;
    const char* input_filename = NULL;
    const char* output_filename = NULL;
    musvg_format_t input_format = musvg_format_none;
    musvg_format_t output_format = musvg_format_none;
    int help_exit = 0, print_stats = 0;

    int i = 1;
    while (i < argc) {
        if (check_opt(argv[i],"-if","--input-file")   && i + 1 < argc) {
            input_filename = argv[++i];
        } else if (check_opt(argv[i],"-of","--output-file")   && i + 1 < argc) {
            output_filename = argv[++i];
        } else if (check_opt(argv[i],"-i","--input-format") && i + 1 < argc) {
            input_format = musvg_parse_format(argv[++i]);
        } else if (check_opt(argv[i],"-o","--output-format") && i + 1 < argc) {
            output_format = musvg_parse_format(argv[++i]);
        } else if (check_opt(argv[i],"-s","--stats")) {
            print_stats = 1;
        } else if (check_opt(argv[i],"-d","--debug")) {
            mu_set_debug(1);
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
        fprintf(stderr, "*** error: missing --input-file parameter\n");
        help_exit = 1;
    }
    if (!output_filename) {
        fprintf(stderr, "*** error: missing --output-file parameter\n");
        help_exit = 1;
    }
    if (!input_format) {
        fprintf(stderr, "*** error: missing --input-format parameter\n");
        help_exit = 1;
    }
    if (!output_format) {
        fprintf(stderr, "*** error: missing --output-format parameter\n");
        help_exit = 1;
    }

    if (help_exit) {
        fprintf(stderr,
            "\nusage: %s [options]\n"
            "\n"
            "-if,--input-file (<filename>|-)\n"
            "-of,--output-file (<filename>|-)\n"
            "-i,--input-format (xml|svgv|svgb)\n"
            "-o,--output-format (xml|svgv|svgb|text)\n"
            "-s,--stats\n"
            "-d,--debug\n"
            "-h,--help\n",
            argv[0]);
        exit(1);
    }

    p = musvg_parser_create();
    musvg_parse_file(p, input_format, input_filename);
    musvg_emit_file(p, output_format, output_filename);
    if (print_stats) {
        printf("\n");
        musvg_parser_stats(p);
    }
    musvg_parser_destroy(p);

    return 0;
}
