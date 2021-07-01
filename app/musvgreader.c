#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "musvg.h"

int main(int argc, char **argv)
{
    const char* filename;
    musvg_parser *p;

    if (argc == 2) {
        filename = argv[1];
    } else if (argc == 3 && strcmp(argv[1],"--debug") == 0) {
        musvg_set_debug(1);
        filename = argv[2];
    } else {
        fprintf(stderr, "usage: %s [--debug] <filename>\n", argv[0]);
        exit(1);
    }

    p = musvg_parse_file(filename);
    musvg_parser_dump(p);
    musvg_parser_destroy(p);

    return 0;
}
