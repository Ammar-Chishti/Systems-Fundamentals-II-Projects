#include "const.h"
#include "sequitur.h"
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

//int utf_8_to_decimal(unsigned int v);

int utf_8_bytes_checker(int utf_8_integer) {

    int is_not_1_byte = utf_8_integer & 0x00000080;
    if (!is_not_1_byte) return 1;

    int is_not_2_byte = utf_8_integer ^ 0xc0;
    is_not_2_byte >>= 5;
    if (!is_not_2_byte) return 2;

    int is_not_3_byte = utf_8_integer ^ 0xe0;
    is_not_3_byte >>= 4;
    if (!is_not_3_byte) return 3;

    int is_not_4_byte = utf_8_integer ^ 0xf0;
    is_not_4_byte >>= 3;
    if (!is_not_4_byte) return 4;

    return 0;
}

int is_byte_msb_10(int x) {
    x >>= 6;
    return (!x^2) ? 1 : 0;
}

// For these set of functions. The MSBytes is passed in arg a and LSBytes are passed in arg d, etc
unsigned int combine_two_bytes(int a, int b) {
    unsigned int answer = 0;
    a <<= 8;
    answer |= a;
    answer |= b;
    return answer;
}

unsigned int combine_three_bytes(int a, int b, int c) {
    unsigned int answer = 0;
    a <<= 16;
    b <<= 8;

    answer |= a;
    answer |= b;
    answer |= c;
    return answer;
}

unsigned int combine_four_bytes(int a, int b, int c, int d) {
    unsigned int answer = 0;
    a <<= 24;
    b <<= 16;
    c <<= 8;

    answer |= a;
    answer |= b;
    answer |= c;
    answer |= d;
    return answer;
}

int utf_8_to_decimal(unsigned int v) {
    int answer = 0;
    if (v < 0xc280) {  // If the utf is 1 byte long ( v < 0xc480)
        return v;
    } else if (v >= 0xc280 && v < 0xe0a080) {    // If the utf is 2 bytes long (v < 0xe08080)
        int temp1 = 0x3f;
        int temp2 = 0x1f00;

        int answerTemp1 = v & temp1;
        int answerTemp2 = v & temp2;
        answerTemp2 >>= 2;

        answer |= answerTemp1;
        answer |= answerTemp2;
        return answer;
    } else if (v >= 0xe0a080 && v < 0xf0808080) {   // If the utf is 3 bytes long (v < 0xf0808080)
        int temp1 = 0x3f;
        int temp2 = 0x3f00;
        int temp3 = 0x0f0000;

        int answerTemp1 = v & temp1;
        int answerTemp2 = v & temp2;
        answerTemp2 >>= 2;
        int answerTemp3 = v & temp3;
        answerTemp3 >>= 4;

        answer |= answerTemp1;
        answer |= answerTemp2;
        answer |= answerTemp3;
        return answer;
    } else {                        // If the utf is 4 bytes long
        int temp1 = 0x3f;
        int temp2 = 0x3f00;
        int temp3 = 0x3f0000;
        int temp4 = 0x07000000;

        int answerTemp1 = v & temp1;
        int answerTemp2 = v & temp2;
        answerTemp2 >>= 2;
        int answerTemp3 = v & temp3;
        answerTemp3 >>= 4;
        int answerTemp4 = v & temp4;
        answerTemp4 >>= 8;

        answer |= answerTemp1;
        answer |= answerTemp2;
        answer |= answerTemp3;
        answer |= answerTemp4;
        return answer;
    }
}

int check_next_char(int int_from_file, FILE* in) {
    int num_of_utf_bytes = utf_8_bytes_checker(int_from_file);
    if (num_of_utf_bytes == 1) {
        return int_from_file;
    }
    else if (num_of_utf_bytes == 2) {
        int next = fgetc(in);
        if (!is_byte_msb_10(next)) { 
            return -1; 
        }
        unsigned int utf_2_byte = combine_two_bytes(int_from_file, next);
        int utf_code_point = utf_8_to_decimal(utf_2_byte);
        return utf_code_point;
    } else if (num_of_utf_bytes == 3) {
        int next1 = fgetc(in);
        int next2 = fgetc(in);
        if (!(is_byte_msb_10(next1) && is_byte_msb_10(next2))) {
            return -1;
        }
        unsigned int utf_3_byte = combine_three_bytes(int_from_file, next1, next2);
        int utf_code_point = utf_8_to_decimal(utf_3_byte);
        return utf_code_point;
    } else if (num_of_utf_bytes == 4) {
        int next1 = fgetc(in);
        int next2 = fgetc(in);
        int next3 = fgetc(in);
        if (!(is_byte_msb_10(next1) && is_byte_msb_10(next2) && is_byte_msb_10(next3))) {
            return -1;
        }
        unsigned int utf_4_byte = combine_four_bytes(int_from_file, next1, next2, next3);
        int utf_code_point = utf_8_to_decimal(utf_4_byte);
        return utf_code_point;
    } else {
        return -1;
    }
}

int decompress_expand(SYMBOL* rule_head, FILE* out) {
    int bytes_written = 0;
    SYMBOL* rule_body = rule_head->next;

    while (rule_body->rule == NULL) {
        if (rule_body->value < 256) {
            fputc(rule_body->value, out);
            ++bytes_written;
        } else {
            SYMBOL* new_rule_head = *(rule_map+(rule_body->value - 256));
            bytes_written += decompress_expand(new_rule_head, out);
        }
        rule_body = rule_body->next;
    }

    return bytes_written;
}

unsigned int decimal_to_utf_8(int x) {
    if (x < 0x80) {
        return x;
    } else if (x >= 0x80 && x <= 0x7ff) {
        int temp1 = 0x3f & x;   // First 6 bits
        int temp2 = 0x7c0 & x;  // 5 bits after first 6 bit

        int answer = 0xc080;
        answer |= temp1;
        temp2 <<= 2;
        answer |= temp2;
        return answer;
    } else if (x >= 0x800 && x <= 0xffff) {
        int temp1 = 0x3f & x;   // First 6 bits
        int temp2 = 0xfc0 & x;  // Next 6 bits after 6 bits
        int temp3 = 0xf000 & x; // Next 4 bits after 12 bits

        int answer = 0xe08080;
        answer |= temp1;
        temp2 <<= 2;
        answer |= temp2;
        temp3 <<= 4;
        answer |= temp3;
        return answer;
    } else {
        int temp1 = 0x3f & x;       // First 6 bits
        int temp2 = 0xfc0 & x;      // Next 6 bits after 6 bits
        int temp3 = 0x3f0000 & x;   // Next 6 bits after 12 bits
        int temp4 = 0x7000000 & x;  // Next 3 bits after 18 bits

        unsigned int answer = 0xf0808080;
        answer |= temp1;
        temp2 <<= 2;
        answer |= temp2;
        temp3 <<= 4;
        answer |= temp3;
        temp4 <<= 6;
        answer |= temp4;
        return answer;
    }
    return -1;
}

void write_utf_value_to_file(int v, FILE* out) {
    if (v < 0xc280) {
        fputc(v, out);
    } else if (v >= 0xc280 && v < 0xe0a080) {
        int byte_1 = v & 0xff00;
        byte_1 >>= 8;
        int byte_2 = v & 0xff;

        fputc(byte_1, out);
        fputc(byte_2, out);
    } else if (v >= 0xe0a080 && v < 0xf0808080) {

        int byte_1 = v & 0xff0000;
        int byte_2 = v & 0xff00;
        int byte_3 = v & 0xff;
        byte_1 >>= 16;
        byte_2 >>= 8;

        fputc(byte_1, out);
        fputc(byte_2, out);
        fputc(byte_3, out);
    } else {
        int byte_1 = v & 0xff000000;
        int byte_2 = v & 0xff0000;
        int byte_3 = v & 0xff00;
        int byte_4 = v & 0xff;
        byte_1 >>= 24;
        byte_2 >>= 16;
        byte_3 >>= 8;

        fputc(byte_1, out);
        fputc(byte_2, out);
        fputc(byte_3, out);
        fputc(byte_4, out);
    }
}

int write_compression_data(SYMBOL* current_main_rule, FILE* out) {

    SYMBOL* curs = current_main_rule;
    int num_of_bytes = 0;

    int main_rule_utf_8 = decimal_to_utf_8(curs->value);
    write_utf_value_to_file(main_rule_utf_8, out);
    num_of_bytes += utf_8_bytes_checker(main_rule_utf_8);

    curs = curs->next;
    
    while (curs != current_main_rule) {
        int utf_decimal_to_write = decimal_to_utf_8(curs->value);
        write_utf_value_to_file(utf_decimal_to_write, out);
        num_of_bytes += utf_8_bytes_checker(utf_decimal_to_write);

        curs = curs->next;
    }

    if (main_rule->nextr == main_rule) {
        return num_of_bytes;
    }
    fputc(0x85, out);
    ++num_of_bytes;
    curs = curs->nextr;

    while (curs != current_main_rule) {
        SYMBOL* current_rule_head = curs;

        int current_head_rule_utf_8 = decimal_to_utf_8(curs->value);
        write_utf_value_to_file(current_head_rule_utf_8, out);
        num_of_bytes += utf_8_bytes_checker(current_head_rule_utf_8);

        curs = curs->next;

        while (curs != current_rule_head) {
            int utf_decimal_to_write = decimal_to_utf_8(curs->value);
            write_utf_value_to_file(utf_decimal_to_write, out);
            num_of_bytes += utf_8_bytes_checker(utf_decimal_to_write);

            curs = curs->next;
        }
        curs = curs->nextr;
        if (curs != current_main_rule) {
            fputc(0x85, out);
            ++num_of_bytes;
        }
    }
    return num_of_bytes;
}

/*
 * You may modify this file and/or move the functions contained here
 * to other source files (except for main.c) as you wish.
 *
 * IMPORTANT: You MAY NOT use any array brackets (i.e. [ and ]) and
 * you MAY NOT declare any arrays or allocate any storage with malloc().
 * The purpose of this restriction is to force you to use pointers.
 * Variables to hold the pathname of the current file or directory
 * as well as other data have been pre-declared for you in const.h.
 * You must use those variables, rather than declaring your own.
 * IF YOU VIOLATE THIS RESTRICTION, YOU WILL GET A ZERO!
 *
 * IMPORTANT: You MAY NOT use floating point arithmetic or declare
 * any "float" or "double" variables.  IF YOU VIOLATE THIS RESTRICTION,
 * YOU WILL GET A ZERO!
 */

/**
 * Main compression function.
 * Reads a sequence of bytes from a specified input stream, segments the
 * input data into blocks of a specified maximum number of bytes,
 * uses the Sequitur algorithm to compress each block of input to a list
 * of rules, and outputs the resulting compressed data transmission to a
 * specified output stream in the format detailed in the header files and
 * assignment handout.  The output stream is flushed once the transmission
 * is complete.
 *
 * The maximum number of bytes of uncompressed data represented by each
 * block of the compressed transmission is limited to the specified value
 * "bsize".  Each compressed block except for the last one represents exactly
 * "bsize" bytes of uncompressed data and the last compressed block represents
 * at most "bsize" bytes.
 *
 * @param in  The stream from which input is to be read.
 * @param out  The stream to which the block is to be written.
 * @param bsize  The maximum number of bytes read per block.
 * @return  The number of bytes written, in case of success,
 * otherwise EOF.
 */
int compress(FILE *in, FILE *out, int bsize) {
    if (in == NULL || out == NULL) {
        return EOF;
    }
    int bytes_written = 0;

    fputc(0x81, out);   // Always place the start of transmission as the first byte
    ++bytes_written;

    int c;
    while (!feof(in)) {
        fputc(0x83, out);
        ++bytes_written;

        init_symbols(); // Initialize everything
        init_rules();
        init_digram_hash();
        SYMBOL* current_main_rule = new_rule(next_nonterminal_value);
        ++next_nonterminal_value;

        int rule_size = 0;
        while (rule_size < bsize) {
            int c = fgetc(in);
            if (c == -1) {
                break;
            }
            SYMBOL* second_to_last_symbol = current_main_rule->prev;
            SYMBOL* terminal_symbol = new_symbol(c, NULL);
            insert_after(second_to_last_symbol, terminal_symbol);
            check_digram(second_to_last_symbol);
            ++rule_size;
        }
        bytes_written += write_compression_data(current_main_rule, out);
        fputc(0x84, out);
        ++bytes_written;
    }

    fputc(0x82, out);
    ++bytes_written;
    return bytes_written;
}

/**
 * Main decompression function.
 * Reads a compressed data transmission from an input stream, expands it,
 * and and writes the resulting decompressed data to an output stream.
 * The output stream is flushed once writing is complete.
 *
 * @param in  The stream from which the compressed block is to be read.
 * @param out  The stream to which the uncompressed data is to be written.
 * @return  The number of bytes written, in case of success, otherwise EOF.
 */
int decompress(FILE *in, FILE *out) {
    if (in == NULL || out == NULL) {
        return EOF;
    }
    int first_byte_read = 0;
    int bytes_written = 0;
    int block_in_progress = 0;
    int start_of_a_rule = 0;
    int is_block_occupied = 0;
    SYMBOL* last_symbol;

    int c;
    while ((c = fgetc(in)) != EOF) {
        if (c == 0x81) {
            if (!first_byte_read) {
                init_rules();
                init_symbols();
                ++first_byte_read;
                continue;
            } else {
                return EOF;
            }   
        }
        if (c == 0x82) {
            break;
        }
        if (c == 0x83) {
            is_block_occupied = 0;
            if (block_in_progress) {
                return EOF;
            }
            init_rules();
            init_symbols();
            block_in_progress = 1;
            start_of_a_rule = 1;
            continue;
        } else if (c == 0x84) {
            if (is_block_occupied == 0) {
                return EOF;
            }
            if (!block_in_progress) {
                return EOF;
            }
            if (start_of_a_rule) {
                start_of_a_rule = 0;
            }
            block_in_progress = 0;
            bytes_written += decompress_expand(main_rule, stdout);
            continue;
        } else if (c == 0x85) { // When we reach the end of a rule
            start_of_a_rule = 1;
            continue;
        }

        int int_read = check_next_char(c, in);
        if (int_read == -1) {
            return EOF;
        }
        is_block_occupied = 1;

        if (start_of_a_rule) {  // We are at the start of a rule
            SYMBOL* new_rule_head = new_rule(int_read);
            last_symbol = new_rule_head;
            start_of_a_rule = 0;
            continue;
        }
        
        SYMBOL* new_symbol_node = new_symbol(int_read, NULL);

        new_symbol_node->next = last_symbol->next;
        new_symbol_node->next->prev = new_symbol_node;
        new_symbol_node->prev = last_symbol;
        last_symbol->next = new_symbol_node;
        last_symbol = new_symbol_node;
    }
    if (first_byte_read == 0) {
        return EOF;
    }
    return bytes_written;
}

int compare_strings(char* str1, char* str2) {
    while ((*str1 != '\0') || (*str2 != '\0')) {
        if (*str1 != *str2) {
            return 0;
        }
        ++str1;
        ++str2;
    }
    return 1;
}

int integer_pow(int base, int exponent) {
    int i;
    int answer = 1;
    for (i = 0; i < exponent; i++) {
        answer *= base;
    }
    return answer;
}

int convert_block_size(char* BlockAmount) {   // Checks if a String number is [1, 1024] and if it is converts it into an int, returns -1 if not
    int block_amount_size = 0;
    char* block_amount_size_pointer = BlockAmount;
    while (*block_amount_size_pointer != '\0') {   // To get the size of the str
        block_amount_size += 1;
        ++block_amount_size_pointer;
    }

    int block_amount_value = 0;
    int i;
    for (i = 0; i < block_amount_size; ++i) {
        char c = *(BlockAmount+i);
        if (c < 48 || c > 57) {
            return -1;
        }
        c = c - 48;

        block_amount_value += c * integer_pow(10, block_amount_size-i-1);
    }

    if (block_amount_value < 1 || block_amount_value > 1024) {
        return -1;
    }

    return block_amount_value;
}

/**
 * @brief Validates command line arguments passed to the program.
 * @details This function will validate all the arguments passed to the
 * program, returning 0 if validation succeeds and -1 if validation fails.
 * Upon successful return, the selected program options will be set in the
 * global variable "global_options", where they will be accessible
 * elsewhere in the program.
 *
 * @param argc The number of arguments passed to the program from the CLI.
 * @param argv The argument strings passed to the program from the CLI.
 * @return 0 if validation succeeds and -1 if validation fails.
 * Refer to the homework document for the effects of this function on
 * global variables.
 * @modifies global variable "global_options" to contain a bitmap representing
 * the selected options.
 */
int validargs(int argc, char **argv) {
    int c_flag_count = 0;
    int d_flag_count = 0;
    int b_flag_count = 0;

    if (argc == 1) {    // If there are no arguments
        global_options = 0;
        return -1;
    }
    if (compare_strings(*(argv+1), "-h")) {     // If the second argument is a -h
        global_options = 1;
        return 0;
    }

    if (compare_strings(*(argv+(argc-2)),"-b")) {   // If the second to last argument is a -b
        int block_size = convert_block_size(*(argv+(argc-1)));
        if (block_size < 1 || block_size > 1024) {  // If the block size is valid
            global_options = 0;
            return -1;
        }
        b_flag_count = 1;
        global_options |= block_size;   // Setting the 16 most-significant bits of global_options to block_size
        global_options <<= 16;
    }

    int i;
    for (i = 1; i < argc; ++i) {
        if (b_flag_count && i >= argc - 2) {
            continue;
        }

        if (compare_strings(*(argv+i), "-d")) {
            d_flag_count += 1;
        } else if (compare_strings(*(argv+i), "-c")) {
            c_flag_count += 1;
        } else {
            global_options = 0;
            return -1;
        }
    }

    if ((c_flag_count && d_flag_count) || c_flag_count > 1 || d_flag_count > 1 || (d_flag_count && b_flag_count) || (!c_flag_count && !d_flag_count)) {
        global_options = 0;
        return -1;
    }

    if (c_flag_count) {
        global_options |= 2;
    } else if (d_flag_count) {
        global_options |= 4;
    }

    if (!b_flag_count) {
        global_options |= 0x04000000;
    }

    return 0;
}
