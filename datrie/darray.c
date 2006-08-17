/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * darray.c - Double-array trie structure
 * Created: 2006-08-13
 * Author:  Theppitak Karoonboonyanan <thep@linux.thai.net>
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "darray.h"
#include "fileutils.h"

/*----------------------------------*
 *    INTERNAL TYPES DECLARATIONS   *
 *----------------------------------*/

typedef struct _Symbols Symbols;

struct _Symbols {
    short       num_symbols;
    TrieChar    symbols[256];
};

static Symbols *    symbols_new ();
static void         symbols_free (Symbols *syms);
static void         symbols_add (Symbols *syms, TrieChar c);

#define symbols_num(s)          ((s)->num_symbols)
#define symbols_get(s,i)        ((s)->symbols[i])
#define symbols_add_fast(s,c)   ((s)->symbols[(s)->num_symbols++] = c)

/*-----------------------------------*
 *    PRIVATE METHODS DECLARATIONS   *
 *-----------------------------------*/

#define da_get_free_list(d)      (1)

static Bool         da_check_free_cell (DArray         *d,
                                        TrieIndex       s);

static int          da_num_children    (DArray         *d,
                                        TrieIndex       s);

static Symbols *    da_output_symbols  (DArray         *d,
                                        TrieIndex       s);

static TrieChar *   da_get_state_key   (DArray         *d,
                                        TrieIndex       state);

static TrieIndex    da_find_free_base  (DArray         *d,
                                        const Symbols  *symbols);

static Bool         da_fit_symbols     (DArray         *d,
                                        TrieIndex       base,
                                        const Symbols  *symbols);

static void         da_relocate_base   (DArray         *d,
                                        TrieIndex       s,
                                        TrieIndex       new_base);

static void         da_extend_pool     (DArray         *d,
                                        TrieIndex       to_index);

static void         da_alloc_cell      (DArray         *d,
                                        TrieIndex       cell);

static void         da_free_cell       (DArray         *d,
                                        TrieIndex       cell);

static Bool         da_enumerate_recursive (DArray     *d,
                                            TrieIndex   state,
                                            DAEnumFunc  enum_func,
                                            void       *user_data);

/* ==================== BEGIN IMPLEMENTATION PART ====================  */

/*------------------------------------*
 *   INTERNAL TYPES IMPLEMENTATIONS   *
 *------------------------------------*/

static Symbols *
symbols_new ()
{
    Symbols *syms;

    syms = (Symbols *) malloc (sizeof (Symbols));

    if (!syms)
        return NULL;

    syms->num_symbols = 0;

    return syms;
}

static void
symbols_free (Symbols *syms)
{
    free (syms);
}

static void
symbols_add (Symbols *syms, TrieChar c)
{
    short lower, upper;

    lower = 0;
    upper = syms->num_symbols;
    while (lower < upper) {
        short middle;

        middle = (lower + upper)/2;
        if (c > syms->symbols[middle])
            lower = middle + 1;
        else if (c < syms->symbols[middle])
            upper = middle;
        else
            return;
    }
    if (lower < syms->num_symbols) {
        memmove (syms->symbols + lower + 1, syms->symbols + lower,
                 syms->num_symbols - lower);
    }
    syms->symbols[lower] = c;
    syms->num_symbols++;
}

/*------------------------------*
 *    PRIVATE DATA DEFINITONS   *
 *------------------------------*/

typedef struct {
    TrieIndex   base;
    TrieIndex   check;
} DACell;

struct _DArray {
    TrieIndex   num_cells;
    DACell     *cells;

    FILE       *file;
    Bool        is_dirty;
};

/*-----------------------------*
 *    METHODS IMPLEMENTAIONS   *
 *-----------------------------*/

#define DA_SIGNATURE 0xDAFD

DArray *
da_open (const char *path, const char *name, TrieIOMode mode)
{
    DArray     *d;
    TrieIndex   i;

    d = (DArray *) malloc (sizeof (DArray));

    d->file = file_open (path, name, ".br", mode);
    if (!d->file)
        goto exit1;

    /* init cells data */
    d->num_cells = file_length (d->file) / 4;
    if (0 == d->num_cells) {
        d->num_cells = 3;
        d->cells     = (DACell *) malloc (d->num_cells * sizeof (DACell));
        if (!d->cells)
            goto exit2;
        d->cells[0].base = DA_SIGNATURE;
        d->cells[0].check = 1;
        d->cells[1].base = -1;
        d->cells[1].check = -1;
        d->cells[2].base = 3;
        d->cells[2].check = 0;
        d->is_dirty = TRUE;
    } else {
        d->cells     = (DACell *) malloc (d->num_cells * sizeof (DACell));
        if (!d->cells)
            goto exit2;
        file_read_int16 (d->file, &d->cells[0].base);
        file_read_int16 (d->file, &d->cells[0].check);
        if (DA_SIGNATURE != (uint16) d->cells[0].base)
            goto exit3;
        for (i = 1; i < d->num_cells; i++) {
            file_read_int16 (d->file, &d->cells[i].base);
            file_read_int16 (d->file, &d->cells[i].check);
        }
        d->is_dirty  = FALSE;
    }

    return d;

exit3:
    free (d->cells);
exit2:
    fclose (d->file);
exit1:
    free (d);
    return NULL;
}

int
da_close (DArray *d)
{
    int ret;

    if (0 != (ret = da_save (d)))
        return ret;
    if (0 != (ret = fclose (d->file)))
        return ret;
    free (d->cells);
    free (d);

    return 0;
}

int
da_save (DArray *d)
{
    TrieIndex   i;

    if (!d->is_dirty)
        return 0;

    rewind (d->file);
    for (i = 0; i < d->num_cells; i++) {
        if (!file_write_int16 (d->file, d->cells[i].base) ||
            !file_write_int16 (d->file, d->cells[i].check))
        {
            return -1;
        }
    }
    d->is_dirty = FALSE;

    return 0;
}


TrieIndex
da_get_root (const DArray *d)
{
    /* can be calculated value for multi-index trie */
    return 2;
}


TrieIndex
da_get_base (const DArray *d, TrieIndex s)
{
    return (s < d->num_cells) ? d->cells[s].base : TRIE_INDEX_ERROR;
}

TrieIndex
da_get_check (const DArray *d, TrieIndex s)
{
    return (s < d->num_cells) ? d->cells[s].check : TRIE_INDEX_ERROR;
}


void
da_set_base (DArray *d, TrieIndex s, TrieIndex val)
{
    if (s < d->num_cells) {
        d->cells[s].base = val;
        d->is_dirty = TRUE;
    }
}

void
da_set_check (DArray *d, TrieIndex s, TrieIndex val)
{
    if (s < d->num_cells) {
        d->cells[s].check = val;
        d->is_dirty = TRUE;
    }
}

Bool
da_walk (DArray *d, TrieIndex *s, TrieChar c)
{
    TrieIndex   next;

    next = da_get_base (d, *s) + c;
    if (da_get_check (d, next) == *s) {
        *s = next;
        return TRUE;
    }
    return FALSE;
}

TrieIndex
da_insert_branch (DArray *d, TrieIndex s, TrieChar c)
{
    TrieIndex   base, next;

    base = da_get_base (d, s);

    if (base > 0) {
        next = da_get_base (d, s) + c;

        /* if already there, do not actually insert */
        if (da_get_check (d, next) == s)
            return next;

        if (!da_check_free_cell (d, next)) {
            Symbols    *symbols;
            TrieIndex   new_base;

            /* relocate BASE[s] */
            symbols = da_output_symbols (d, s);
            symbols_add (symbols, c);
            new_base = da_find_free_base (d, symbols);
            symbols_free (symbols);

            da_relocate_base (d, s, new_base);
            next = new_base + c;
        }
    } else {
        Symbols    *symbols;
        TrieIndex   new_base;

        symbols = symbols_new ();
        symbols_add (symbols, c);
        new_base = da_find_free_base (d, symbols);
        symbols_free (symbols);

        da_set_base (d, s, new_base);
        next = new_base + c;
    }
    da_alloc_cell (d, next);
    da_set_check (d, next, s);

    return next;
}

static Bool
da_check_free_cell (DArray         *d,
                    TrieIndex       s)
{
    da_extend_pool (d, s);
    return (da_get_check (d, s) < 0);
}

static int
da_num_children    (DArray         *d,
                    TrieIndex       s)
{
    int         num_children;
    TrieIndex   base;
    uint16      c;

    num_children = 0;

    base = da_get_base (d, s);
    if (TRIE_INDEX_ERROR == base || base < 0)
        return 0;

    for (c = 0; c < TRIE_CHAR_MAX; c++)
        if (da_get_check (d, base + c) == s)
            ++num_children;

    return num_children;
}

static Symbols *
da_output_symbols  (DArray         *d,
                    TrieIndex       s)
{
    Symbols    *syms;
    TrieIndex   base;
    uint16      c;

    syms = symbols_new ();

    base = da_get_base (d, s);
    for (c = 0; c < TRIE_CHAR_MAX; c++)
        if (da_get_check (d, base + c) == s)
            symbols_add_fast (syms, (TrieChar) c);

    return syms;
}

static TrieChar *
da_get_state_key   (DArray         *d,
                    TrieIndex       state)
{
    TrieChar   *key;
    int         key_size, key_length;
    int         i;

    key_size = 20;
    key_length = 0;
    key = (TrieChar *) malloc (key_size);

    /* trace back to root */
    while (da_get_root (d) != state) {
        TrieIndex   parent;

        if (key_length + 1 >= key_size) {
            key_size += 20;
            key = (TrieChar *) realloc (key, key_size);
        }
        parent = da_get_check (d, state);
        key[key_length++] = (TrieChar) (state - da_get_base (d, parent));
        state = parent;
    }
    key[key_length] = '\0';

    /* reverse the string */
    for (i = 0; i < --key_length; i++) {
        TrieChar temp;

        temp = key[i];
        key[i] = key[key_length];
        key[key_length] = temp;
    }

    return key;
}

#define DA_EXTENDING_STEPS 16

static TrieIndex
da_find_free_base  (DArray         *d,
                    const Symbols  *symbols)
{
    TrieChar    first_sym;
    TrieIndex   s;

    /* find first free cell that is beyond the first symbol */
    first_sym = symbols_get (symbols, 0);
    s = -da_get_check (d, da_get_free_list (d));
    while (s != da_get_free_list (d) && s < (TrieIndex) first_sym + 3)
        s = -da_get_check (d, s);
    if (s == da_get_free_list (d)) {
        s = first_sym + 3;
        while (da_extend_pool (d, s), da_get_check (d, s) > 0)
            ++s;
    }

    /* search for next free cell that fits the symbols set */
    while (!da_fit_symbols (d, s - first_sym, symbols))
        s = -da_get_check (d, s);

    return s - first_sym;
}

static Bool
da_fit_symbols     (DArray         *d,
                    TrieIndex       base,
                    const Symbols  *symbols)
{
    int         i;

    for (i = 0; i < symbols_num (symbols); i++) {
        if (!da_check_free_cell (d, base + symbols_get (symbols, i)))
            return FALSE;
    }
    return TRUE;
}

static void
da_relocate_base   (DArray         *d,
                    TrieIndex       s,
                    TrieIndex       new_base)
{
    TrieIndex   old_base;
    Symbols    *symbols;
    int         i;

    old_base = da_get_base (d, s);
    symbols = da_output_symbols (d, s);

    for (i = 0; i < symbols_num (symbols); i++) {
        TrieIndex   old_next, new_next, old_next_base;

        old_next = old_base + symbols_get (symbols, i);
        new_next = new_base + symbols_get (symbols, i);
        old_next_base = da_get_base (d, old_next);

        /* allocate new next node and copy BASE value */
        da_alloc_cell (d, new_next);
        da_set_check (d, new_next, s);
        da_set_base (d, new_next, old_next_base);

        /* old_next node is now moved to new_next
         * so, all cells belonging to old_next
         * must be given to new_next
         */
        /* preventing the case of TAIL pointer */
        if (old_next_base > 0) {
            uint16      c;

            for  (c = 0; c < TRIE_CHAR_MAX; c++)
                if (da_get_check (d, old_next_base + c) == old_next)
                    da_set_check (d, old_next_base + c, new_next);
        }

        /* free old_next node */
        da_free_cell (d, old_next);
    }

    symbols_free (symbols);

    /* finally, make BASE[s] point to new_base */
    da_set_base (d, s, new_base);
}

static void
da_extend_pool     (DArray         *d,
                    TrieIndex       to_index)
{
    TrieIndex   new_begin;
    TrieIndex   i;
    TrieIndex   free_tail;

    if (to_index < d->num_cells)
        return;

    d->cells = (DACell *) realloc (d->cells, (to_index + 1) * sizeof (DACell));
    new_begin = d->num_cells;
    d->num_cells = to_index + 1;

    /* initialize new free list */
    for (i = new_begin; i < to_index; i++) {
        da_set_check (d, i, -(i + 1));
        da_set_base (d, i + 1, -i);
    }

    /* merge the new circular list to the old */
    free_tail = -da_get_base (d, da_get_free_list (d));
    da_set_check (d, free_tail, -new_begin);
    da_set_base (d, new_begin, -free_tail);
    da_set_check (d, to_index, -da_get_free_list (d));
    da_set_base (d, da_get_free_list (d), -to_index);
}

void
da_prune (DArray *d, TrieIndex s)
{
    while (da_get_root (d) != s && 0 == da_num_children (d, s)) {
        TrieIndex   parent;

        parent = da_get_check (d, s);
        da_free_cell (d, s);
        s = parent;
    }
}

static void
da_alloc_cell      (DArray         *d,
                    TrieIndex       cell)
{
    TrieIndex   prev, next;

    prev = -da_get_base (d, cell);
    next = -da_get_check (d, cell);

    /* remove the cell from free list */
    da_set_check (d, prev, -next);
    da_set_base (d, next, -prev);
}

static void
da_free_cell       (DArray         *d,
                    TrieIndex       cell)
{
    TrieIndex   i, prev;

    /* find insertion point */
    i = -da_get_check (d, da_get_free_list (d));
    while (i != da_get_free_list (d) && i < cell)
        i = -da_get_check (d, i);

    prev = -da_get_base (d, i);

    /* insert cell before i */
    da_set_check (d, cell, -i);
    da_set_base (d, cell, -prev);
    da_set_check (d, prev, -cell);
    da_set_base (d, i, -cell);
}

Bool
da_enumerate (DArray *d, DAEnumFunc enum_func, void *user_data)
{
    return da_enumerate_recursive (d, da_get_root (d), enum_func, user_data);
}

static Bool
da_enumerate_recursive (DArray     *d,
                        TrieIndex   state,
                        DAEnumFunc  enum_func,
                        void       *user_data)
{
    Bool        ret;
    TrieIndex   base;

    base = da_get_base (d, state);

    if (base < 0) {
        TrieChar   *key;

        key = da_get_state_key (d, state);
        ret = (*enum_func) (key, state, user_data);
        free (key);
    } else {
        Symbols *symbols;
        int      i;

        ret = TRUE;
        symbols = da_output_symbols (d, state);
        for (i = 0; ret && i < symbols_num (symbols); i++) {
            ret = da_enumerate_recursive (d, base + symbols_get (symbols, i),
                                          enum_func, user_data);
        }

        symbols_free (symbols);
    }

    return ret;
}

/*
vi:ts=4:ai:expandtab
*/
