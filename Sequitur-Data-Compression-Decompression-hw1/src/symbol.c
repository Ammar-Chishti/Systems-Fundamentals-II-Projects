#include "const.h"
#include "sequitur.h"

/*
 * Symbol management.
 *
 * The functions here manage a statically allocated array of SYMBOL structures,
 * together with a stack of "recycled" symbols.
 */

/*
 * Initialization of this global variable that could not be performed in the header file.
 */
int next_nonterminal_value = FIRST_NONTERMINAL;

SYMBOL* recycled_list = NULL;

/**
 * Initialize the symbols module.
 * Frees all symbols, setting num_symbols to 0, and resets next_nonterminal_value
 * to FIRST_NONTERMINAL;
 */
void init_symbols(void) {
    num_symbols = 0;
    next_nonterminal_value = FIRST_NONTERMINAL;
    recycled_list = NULL;
}

/**
 * Get a new symbol.
 *
 * @param value  The value to be used for the symbol.  Whether the symbol is a terminal
 * symbol or a non-terminal symbol is determined by its value: terminal symbols have
 * "small" values (i.e. < FIRST_NONTERMINAL), and nonterminal symbols have "large" values
 * (i.e. >= FIRST_NONTERMINAL).
 * @param rule  For a terminal symbol, this parameter should be NULL.  For a nonterminal
 * symbol, this parameter can be used to specify a rule having that nonterminal at its head.
 * In that case, the reference count of the rule is increased by one and a pointer to the rule
 * is stored in the symbol.  This parameter can also be NULL for a nonterminal symbol if the
 * associated rule is not currently known and will be assigned later.
 * @return  A pointer to the new symbol, whose value and rule fields have been initialized
 * according to the parameters passed, and with other fields zeroed.  If the symbol storage
 * is exhausted and a new symbol cannot be created, then a message is printed to stderr and
 * abort() is called.
 *
 * When this function is called, if there are any recycled symbols, then one of those is removed
 * from the recycling list and used to satisfy the request.
 * Otherwise, if there currently are no recycled symbols, then a new symbol is allocated from
 * the main symbol_storage array and the num_symbols variable is incremented to record the
 * allocation.
 */
SYMBOL *new_symbol(int value, SYMBOL *rule) {
    
    SYMBOL *new_symb;
    if (recycled_list) {    // If there is something in the recycled_list we write to that location
        new_symb = recycled_list;
        recycled_list = recycled_list->prev;    // Popping from the "stack"
        if (recycled_list) {
            recycled_list->next = NULL;
        }
    } else {

        if (num_symbols == MAX_SYMBOLS) {   // Else we need to write to the symbol_storage. But if symbol_storage is full we have to exit
            fprintf(stderr, "Symbol storage exhausted");
            abort();
        } else {    // Else we can write to the symbol storage location
            new_symb = (symbol_storage+num_symbols);
            ++num_symbols;
        }
    }

    // This is where we do the actual writing
    new_symb->value = value;
    // if (rule) {
    //     new_symb->rule = new_symb;
    // } else {
    //     new_symb->rule = NULL;
    // }
    new_symb->rule = NULL;
    new_symb->refcnt = 0;
    new_symb->next = NULL;
    new_symb->prev = NULL;
    new_symb->nextr = NULL;
    new_symb->prevr = NULL;

    return new_symb;
}

/**
 * Recycle a symbol that is no longer being used.
 *
 * @param s  The symbol to be recycled.  The caller must not use this symbol any more
 * once it has been recycled.
 *
 * Symbols being recycled are added to the recycled_symbols list, where they will
 * be made available for re-allocation by a subsequent call to get_symbol.
 * The recycled_symbols list is managed as a LIFO list (i.e. a stack), using the
 * next field of the SYMBOL structure to chain together the entries.
 */
void recycle_symbol(SYMBOL *s) {
    if (!recycled_list) {
        recycled_list = s;
        s->prev = NULL, s->next = NULL;
    } else {
        recycled_list->next = s;
        s->prev = recycled_list;
        s->next = NULL;
        recycled_list = s;
    }
}
