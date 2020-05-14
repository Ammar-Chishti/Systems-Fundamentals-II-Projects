#include <stdio.h>
#include <stdlib.h>

#include "const.h"
#include "debug.h"

#ifdef _STRING_H
#error "Do not #include <string.h>. You will get a ZERO."
#endif

#ifdef _STRINGS_H
#error "Do not #include <strings.h>. You will get a ZERO."
#endif

#ifdef _CTYPE_H
#error "Do not #include <ctype.h>. You will get a ZERO."
#endif

int main(int argc, char **argv)
{
    int ret;
    if(validargs(argc, argv))
        USAGE(*argv, EXIT_FAILURE);
    debug("Options: 0x%x", global_options);
    if(global_options & 1) {
        USAGE(*argv, EXIT_SUCCESS);
    } else if (global_options & 2) {
        int block_size = global_options & 0x0fff0000;
        block_size >>= 16;
        block_size *= 1024;
        compress(stdin, stdout, block_size);
    } else if (global_options & 4) {
        int result = decompress(stdin, stdout);
        if (result == EOF) {
            return EXIT_FAILURE;
        }
    }

    return EXIT_SUCCESS;
}

/*
 * Just a reminder: All non-main functions should
 * be in another file not named main.c
 */
