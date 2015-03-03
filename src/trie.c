#include "trie.h"
#include <math.h>

/*
Constructors
*/

trie_t *trie_new_empty(uint8_t *alphabet, uint32_t alphabet_size) {
    trie_t *self = malloc(sizeof(trie_t));
    if (!self)
        goto exit_no_malloc;

    self->nodes = trie_node_array_new_size(DEFAULT_NODE_ARRAY_SIZE);
    if (!self->nodes)
        goto exit_trie_created;

    self->null_node = NULL_NODE;

    self->tail = uchar_array_new_size(1);
    if (!self->tail)
        goto exit_node_array_created;

    self->alphabet = malloc(alphabet_size);
    if (!self->alphabet)
        goto exit_tail_created;
    memcpy(self->alphabet, alphabet, alphabet_size);

    self->alphabet_size = alphabet_size;

    for (int i = 0; i < self->alphabet_size; i++) {
        self->alpha_map[alphabet[i]] = i;
    }

    self->data = trie_data_array_new_size(1);
    if (!self->data)
        goto exit_alphabet_created;

    return self;

exit_alphabet_created:
    free(self->alphabet);
exit_tail_created:
    uchar_array_destroy(self->tail);
exit_node_array_created:
    trie_node_array_destroy(self->nodes);
exit_trie_created:
    free(self);
exit_no_malloc:
    return NULL;
}

trie_t *trie_new(uint8_t *alphabet, uint32_t alphabet_size) {
    trie_t *self = trie_new_empty(alphabet, alphabet_size);
    if (!self)
        return NULL;

    trie_node_array_push(self->nodes, (trie_node_t){0, 0});
    // Circular reference  point for first and last free nodes in the linked list
    trie_node_array_push(self->nodes, (trie_node_t){-1, -1});
    // Root node
    trie_node_array_push(self->nodes, (trie_node_t){TRIE_POOL_BEGIN, 0});

    uchar_array_push(self->tail, '\0');
    // Since data indexes are negative integers, index 0 is not valid, so pad it
    trie_data_array_push(self->data, (trie_data_node_t){0, 0});

    return self;
}

bool trie_node_is_free(trie_node_t node) {
    return node.check < 0;
}

trie_node_t trie_get_node(trie_t *self, uint32_t index) {
    if ((index >= self->nodes->n) || index < ROOT_ID) return self->null_node;
    return self->nodes->a[index];
}

void trie_set_base(trie_t *self, uint32_t index, int32_t base) {
    log_debug("Setting base at %d to %d\n", index, base);
    self->nodes->a[index].base = base;
}

void trie_set_check(trie_t *self, uint32_t index, int32_t check) {
    log_debug("Setting check at %d to %d\n", index, check);
    self->nodes->a[index].check = check;
}


trie_node_t trie_get_root(trie_t *self) {
    return self->nodes->a[ROOT_ID];
}

trie_node_t trie_get_free_list(trie_t *self) {
    return self->nodes->a[FREE_LIST_ID];
}


/* 
* Private implementation
*/



static bool trie_extend(trie_t *self, uint32_t to_index) {
    uint32_t new_begin, i, free_tail;

    if (to_index <= 0 || TRIE_MAX_INDEX <= to_index)
        return false;

    if (to_index < self->nodes->n)
        return true;

    new_begin = self->nodes->n;

    for (i = new_begin; i < to_index + 1; i++) {
        trie_node_array_push(self->nodes, (trie_node_t){-(i-1), -(i+1)});
    }

    trie_node_t free_list_node = trie_get_free_list(self);
    free_tail = -free_list_node.base;
    trie_set_check(self, free_tail, -new_begin);
    trie_set_base(self, new_begin, -free_tail);
    trie_set_check(self, to_index, -FREE_LIST_ID);
    trie_set_base(self, FREE_LIST_ID, -to_index);

    return true;
}

void trie_make_room_for(trie_t *self, uint32_t next_id) {
    if (next_id+self->alphabet_size >= self->nodes->n) {
        trie_extend(self, next_id+self->alphabet_size);
        log_debug("extended to %zu\n", self->nodes->n);
    }
}

static inline void trie_set_node(trie_t *self, uint32_t index, trie_node_t node) {
    log_debug("setting node, index=%d, node=(%d,%d)\n", index, node.base, node.check);
    self->nodes->a[index] = node;
}

static void trie_init_node(trie_t *self, uint32_t index) {
    int32_t prev, next;

    trie_node_t node = trie_get_node(self, index);
    prev = -node.base;
    next = -node.check;

    trie_set_check(self, prev, -next);
    trie_set_base(self, next, -prev);

}

static void trie_free_node(trie_t *self, uint32_t index) {
    int32_t i, prev;

    trie_node_t free_list_node = trie_get_free_list(self);
    trie_node_t node;
    i = -free_list_node.check;
    while (i != FREE_LIST_ID && i < index) {
        node = trie_get_node(self, i);
        i = -node.check;
    }

    node = trie_get_node(self, i);
    prev = -node.base;

    trie_set_node(self, index, (trie_node_t){-prev, -i});

    trie_set_check(self, prev, -index);
    trie_set_base(self, i, -index);
}


static bool trie_node_has_children(trie_t *self, uint32_t node_id) {
    uint32_t index;
    if (node_id > self->nodes->n)
        return false;
    trie_node_t node = trie_get_node(self, node_id);
    if (node.base < 0)
        return false;
    for (int i = 0; i < self->alphabet_size; i++) {
        unsigned char c = self->alphabet[i];
        index = trie_get_transition_index(self, node, c);
        if (index < self->nodes->n && trie_get_node(self, index).check == node_id)
            return true;
    }
    return false;
}

static void trie_prune_up_to(trie_t *self, uint32_t p, uint32_t s) {
    log_debug("Pruning from %d to %d\n", s, p);
    log_debug("%d has_children=%d\n", s, trie_node_has_children(self, s));
    while (p != s && !trie_node_has_children(self, s)) {
        uint32_t parent = trie_get_node(self, s).check;
        trie_free_node(self, s);
        s = parent;
    }
}

static void trie_prune(trie_t *self, uint32_t s) {
    trie_prune_up_to(self, ROOT_ID, s);
}

static void trie_get_transition_chars(trie_t *self, uint32_t node_id, unsigned char *transitions, uint32_t *num_transitions) {
    uint32_t index;
    uint32_t j = 0;
    trie_node_t node = trie_get_node(self, node_id);
    log_debug("In get_transition_chars with node_id=%d\n", node_id);
    for (int i = 0; i < self->alphabet_size; i++) {
        unsigned char c = self->alphabet[i];
        index = trie_get_transition_index(self, node, c);
        if (index < self->nodes->n && trie_get_node(self, index).check == node_id) {
            log_debug("adding transition char %c to index %d\n", c, j);
            transitions[j++] = c;
        }
    }

    *num_transitions = j;
}


static bool trie_can_fit_transitions(trie_t *self, uint32_t node_id, unsigned char *transitions, int num_transitions) {
    int i;
    uint32_t char_index, index;

    for (i = 0; i < num_transitions; i++) {
        unsigned char c = transitions[i];
        char_index = trie_get_char_index(self, c);
        index = node_id + char_index;
        trie_node_t node = trie_get_node(self, index);
        if (node_id > TRIE_MAX_INDEX - char_index || !trie_node_is_free(node)) {
            return false;
        }

    }
    return true;

}

static uint32_t trie_find_new_base(trie_t *self, unsigned char *transitions, int num_transitions) {
    uint32_t first_char_index = trie_get_char_index(self, transitions[0]);

    trie_node_t node = trie_get_free_list(self);
    uint32_t index = -node.check;

    while (index != FREE_LIST_ID && index < first_char_index + TRIE_POOL_BEGIN) {
        node = trie_get_node(self, index);
        index = -node.check;
    }  


    if (index == FREE_LIST_ID) {
        for (index = first_char_index + TRIE_POOL_BEGIN; ; index++) {
            if (!trie_extend(self, index)) {
                log_error("Trie index error extending to %d\n", index);
                return TRIE_INDEX_ERROR;
            }
            node = trie_get_node(self, index);
            if (node.check < 0) 
                break;
        }
    }

    // search for next free cell that fits the transitions
    while (!trie_can_fit_transitions(self, index - first_char_index, transitions, num_transitions)) {
        trie_node_t node = trie_get_node(self, index);
        if (-node.check == FREE_LIST_ID) {
            if (!trie_extend(self, self->nodes->n+self->alphabet_size)) {
                log_error("Trie index error extending to %d\n", index);
                return TRIE_INDEX_ERROR;
            }
            node = trie_get_node(self, index);
        }

        index = -node.check;

    }

    return index - first_char_index;

}

static size_t trie_required_size(trie_t *self, uint32_t index) {
    size_t array_size = (size_t)self->nodes->m;
    // Make sure we have enough space in the array
    while (array_size < (TRIE_POOL_BEGIN+index)) {
        array_size *= 2;
    }
    return array_size;
}

static void trie_relocate_base(trie_t *self, uint32_t current_index, int32_t new_base) {
    log_debug("Relocating base at %d\n", current_index);
    int i;

    trie_make_room_for(self, new_base);

    trie_node_t old_node = trie_get_node(self, current_index);

    uint32_t num_transitions = 0;
    unsigned char transitions[self->alphabet_size];
    trie_get_transition_chars(self, current_index, transitions, &num_transitions);

    for (i = 0; i < num_transitions; i++) {
        unsigned char c = transitions[i];

        uint32_t char_index = trie_get_char_index(self, c);

        uint32_t old_index = old_node.base + char_index;
        uint32_t new_index = new_base + char_index;

        log_debug("old_index=%d\n", old_index);
        trie_node_t old_transition = trie_get_node(self, old_index);

        trie_init_node(self, new_index);
        trie_set_node(self, new_index, (trie_node_t){old_transition.base, current_index});

        /*
        *  All transitions out of old_index are now owned by new_index
        *  set check values appropriately
        */
        if (old_transition.base > 0) {  // do nothing in the case of a tail pointer
            for (int i = 0; i < self->alphabet_size; i++) {
                unsigned char c = self->alphabet[i];
                uint32_t index = trie_get_transition_index(self, old_transition, c);
                if (index < self->nodes->n && trie_get_node(self, index).check == old_index) {
                    trie_set_check(self, index, new_index);
                }
            }
        }

        // Free the node at old_index
        log_debug("freeing node at %d\n", old_index);
        trie_free_node(self, old_index);

    }

    trie_set_base(self, current_index, new_base);
}



/*
* Public methods
*/

inline uint32_t trie_get_char_index(trie_t *self, unsigned char c) {
    return self->alpha_map[(uint8_t)c] + 1;
}

inline uint32_t trie_get_transition_index(trie_t *self, trie_node_t node, unsigned char c) {
    uint32_t char_index = trie_get_char_index(self, c);
    //log_debug("char=%c, char_index=%d\n", c, char_index);
    return node.base + char_index;
}

inline trie_node_t trie_get_transition(trie_t *self, trie_node_t node, unsigned char c) {
   uint32_t index = trie_get_transition_index(self, node, c);

    if (index >= self->nodes->n) {
        return self->null_node;
    } else {
        return self->nodes->a[index];
    }

}

void trie_add_tail(trie_t *self, unsigned char *tail) {
    log_debug("Adding tail: %s\n", tail);
    for (; *tail; tail++) {
        uchar_array_push(self->tail, *tail);
    }

    uchar_array_push(self->tail, '\0');
}

void trie_set_tail(trie_t *self, unsigned char *tail, int32_t tail_pos) {
    log_debug("Setting tail: %s at pos %d\n", tail, tail_pos);
    int tail_len = strlen((char *)tail);
    int num_appends = (tail_pos + tail_len) - self->tail->n;
    int i = 0;

    // Pad with 0s if we're short
    if (num_appends > 0) {
        for (i = 0; i < num_appends; i++) {
            uchar_array_push(self->tail, '\0');
        }
    }

    for (i = tail_pos; *tail && i < self->tail->n; i++, tail++) {
        self->tail->a[i] = *tail;
    }
    self->tail->a[i] = '\0';
}


uint32_t trie_add_transition(trie_t *self, uint32_t node_id, unsigned char c) {
    uint32_t next_id;
    trie_node_t node, next;
    uint32_t new_base;


    node = trie_get_node(self, node_id);
    uint32_t char_index = trie_get_char_index(self, c);

    log_debug("adding transition %c to node_id %d + char_index %d\n", c, node_id, char_index);


    if (node.base > 0) {
        next_id = node.base + char_index;
        trie_make_room_for(self, next_id);

        next = trie_get_node(self, next_id);

        if (next.check == node_id) {
            return next_id;
        }

        if (node.base > TRIE_MAX_INDEX - char_index || !trie_node_is_free(next)) {
            uint32_t num_transitions;
            unsigned char transitions[self->alphabet_size];
            trie_get_transition_chars(self, node_id, transitions, &num_transitions);

            transitions[num_transitions++] = c;
            new_base = trie_find_new_base(self, transitions, num_transitions);

            trie_relocate_base(self, node_id, new_base);
            next_id = new_base + char_index;
        }

    } else {
        unsigned char transitions[] = {c};
        new_base = trie_find_new_base(self, transitions, 1);
        log_debug("Found base for transition char %c, base=%d\n", c, new_base);

        trie_set_base(self, node_id, new_base);
        next_id = new_base + char_index;
    }
    trie_init_node(self, next_id);
    trie_set_check(self, next_id, node_id);

    return next_id;
}

int32_t trie_separate_tail(trie_t *self, uint32_t from_index, unsigned char *tail, uint32_t data) {
    unsigned char c = *tail;
    int32_t index = trie_add_transition(self, from_index, c);

    if (*tail != '\0') tail++;

    log_debug("Separating node at index %d into char %c with tail %s\n", from_index, c, tail);
    trie_set_base(self, index, -1 * self->data->n);

    trie_data_array_push(self->data, (trie_data_node_t){self->tail->n, data});
    trie_add_tail(self, tail);

    return index;
}

void trie_tail_merge(trie_t *self, uint32_t old_node_id, unsigned char *suffix, uint32_t data) {
    unsigned char c;
    uint32_t next_id;

    trie_node_t old_node = trie_get_node(self, old_node_id);
    int32_t old_data_index = -1*old_node.base;
    trie_data_node_t old_data_node = self->data->a[old_data_index];
    uint32_t old_tail_pos = old_data_node.tail;

    unsigned char *original_tail = self->tail->a + old_tail_pos;
    unsigned char *old_tail = original_tail;
    log_debug("Merging existing tail %s with new tail %s, node_id=%d\n", original_tail, suffix, old_node_id);

    int common_prefix = string_common_prefix((char *)old_tail, (char *)suffix);
    int old_tail_len = strlen((char *)old_tail);
    int suffix_len = strlen((char *)suffix);
    if (common_prefix == old_tail_len && old_tail_len == suffix_len) {
        log_debug("Key already exists, exiting early\n");
        return;
    }

    uint32_t node_id = old_node_id;
    log_debug("common_prefix=%d\n", common_prefix);

    for (int i=0; i < common_prefix; i++) {
        c = old_tail[i];
        log_debug("merge tail, c=%c, node_id=%d\n", c, node_id);
        next_id = trie_add_transition(self, node_id, c);
        if (next_id == TRIE_INDEX_ERROR) {
            goto exit_prune;
        }
        node_id = next_id;
    }

    uint32_t old_tail_index = trie_add_transition(self, node_id, *(old_tail+common_prefix));
    log_debug("old_tail_index=%d\n", old_tail_index);
    if (old_tail_index == TRIE_INDEX_ERROR) {
        goto exit_prune;
    }

    old_tail += common_prefix;
    if (*old_tail != '\0') {
        old_tail++;
    }

    trie_set_base(self, old_tail_index, -1 * old_data_index);
    trie_set_tail(self, old_tail, old_tail_pos);

    trie_separate_tail(self, node_id, suffix+common_prefix, data);
    return;

exit_prune:
    trie_prune_up_to(self, old_node_id, node_id);
    trie_set_tail(self, original_tail, old_tail_pos);
    return;
}



void trie_print(trie_t *self) {
    printf("Trie\n");
    printf("num_nodes=%zu, alphabet_size=%d\n\n", self->nodes->n, self->alphabet_size);
    for (int i = 0; i < self->nodes->n; i++) {
        int32_t base = self->nodes->a[i].base;
        int32_t check = self->nodes->a[i].check;

        int check_width = abs(check) > 9 ? (int) log10(abs(check))+1 : 1;
        int base_width = abs(base) > 9 ? (int) log10(abs(base))+1 : 1;
        if (base < 0) base_width++;
        if (check < 0) check_width++;
        int width = base_width > check_width ? base_width : check_width;
        printf("%*d ", width, base);
    }
    printf("\n");

    for (int i = 0; i < self->nodes->n; i++) {
        int32_t base = self->nodes->a[i].base;
        int32_t check = self->nodes->a[i].check;

        int check_width = abs(check) > 9 ? (int) log10(abs(check))+1 : 1;
        int base_width = abs(base) > 9 ? (int) log10(abs(base))+1 : 1;
        if (base < 0) base_width++;
        if (check < 0) check_width++;
        int width = base_width > check_width ? base_width : check_width;
        printf("%*d ", width, check);
    }
    printf("\n");
    for (int i = 0; i < self->tail->n; i++) {
        printf("%c ", self->tail->a[i]);
    }
    printf("\n");
    for (int i = 0; i < self->data->n; i++) {
        uint32_t tail = self->data->a[i].tail;
        uint32_t data = self->data->a[i].data;

        int tail_width = tail > 9 ? (int) log10(tail)+1 : 1;
        int data_width = data > 9 ? (int) log10(data)+1 : 1;

        int width = tail_width > data_width ? tail_width : data_width;
        printf("%*d ", width, tail);

    }
    printf("\n");
    for (int i = 0; i < self->data->n; i++) {
        uint32_t tail = self->data->a[i].tail;
        uint32_t data = self->data->a[i].data;

        int tail_width = tail > 9 ? (int) log10(tail)+1 : 1;
        int data_width = data > 9 ? (int) log10(data)+1 : 1;

        int width = tail_width > data_width ? tail_width : data_width;
        printf("%*d ", width, data);

    }
    printf("\n");

}

void trie_add_to_node(trie_t *self, uint32_t node_id, char *key, uint32_t data) {
    int num_chars = strlen(key);
    unsigned char *ptr = (unsigned char *)key; 
    trie_node_t node = trie_get_node(self, node_id);

    trie_node_t next;
    uint32_t next_id;

    // Walks node until prefix reached, including the trailing \0

    for (int i = 0; i < num_chars + 1; ptr++, i++, node_id = next_id, node = next) {

        log_debug("--- char=%c\n", *ptr);
        next_id = trie_get_transition_index(self, node, *ptr);
        log_debug("next_id=%d\n", next_id);
        if (next_id != NULL_ID)
            trie_make_room_for(self, next_id);

        next = trie_get_node(self, next_id);
        log_debug("next.check=%d, node_id=%d, next.base=%d\n", next.check, node_id, next.base);

        if (next.check < 0 || (next.check != node_id)) {
            log_debug("node_id=%d, ptr=%s, tail_pos=%zu\n", node_id,  ptr, self->tail->n);
            trie_separate_tail(self, node_id, ptr, data);
            return;
        } else if (next.base < 0 && next.check == node_id) {
            log_debug("Case 3 insertion\n");
            trie_tail_merge(self, next_id, ptr + 1, data);
            return;
        }
    }

    return;
}

void trie_add(trie_t *self, char *key, uint32_t data) {
    if (strlen(key) == 0) return;
    trie_add_to_node(self, ROOT_ID, key, data);
}

void trie_add_suffix(trie_t *self, char *key, uint32_t data) {
    if (strlen(key) == 0) return;
    trie_node_t root = trie_get_root(self);

    uint32_t node_id = trie_get_transition_index(self, root, '\0');
    trie_node_t node = trie_get_node(self, node_id);
    if (node.check != ROOT_ID) {
        node_id = trie_add_transition(self, ROOT_ID, '\0');
    }

    char *suffix = utf8_reversed_string(key);    
    trie_add_to_node(self, node_id, suffix, data);
    free(suffix);

}

uint32_t trie_get(trie_t *self, char *word, bool whole_word) {
    if (word == NULL) return 0;

    unsigned char *ptr = (unsigned char *)word;

    trie_node_t node = trie_get_root(self);
    uint32_t node_id = ROOT_ID;
    uint32_t next_id;

    size_t word_len = strlen(word);
    /* Include NUL-byte if we're looking for whole phrases.
    *  It may be stored if this phrase is a prefix of a longer one */
    int chars = whole_word ? word_len + 1 : word_len;

    for (int i = 0; i < chars; i++, ptr++, node_id = next_id) {
        next_id = trie_get_transition_index(self, node, *ptr);
        node = trie_get_node(self, next_id);

        if (node.check != node_id) {
            return 0;
        }

        if (node.check == node_id && node.base < 0) {
            int32_t data_index = -1*node.base;
            trie_data_node_t data_node = self->data->a[data_index];
            unsigned char *current_tail = self->tail->a + data_node.tail;

            size_t tail_len = strlen((char *)current_tail);
            char *query_tail = (char *)(*ptr ? ptr + 1 : ptr);
            size_t query_tail_len = strlen((char *)query_tail);

            int tail_match;

            if (whole_word && query_tail_len == tail_len) {
                tail_match = strncmp((char *)current_tail, query_tail, tail_len);
            } else {
                tail_match = strncmp((char *)current_tail, query_tail, query_tail_len);
            }

            if (tail_match == 0) {
                return next_id;
            } else {
                return 0;
            }

        }

    }

    return next_id;

}

/*
Destructor
*/
void trie_destroy(trie_t *self) {
    if (!self)
        return;

    if (self->alphabet)
        free(self->alphabet);
    if (self->nodes)
        trie_node_array_destroy(self->nodes);
    if (self->tail)
        uchar_array_destroy(self->tail);
    if (self->data)
        trie_data_array_destroy(self->data);
    
    free(self);
}


/*
I/O methods
*/

bool trie_write(trie_t *self, FILE *file) {
    if (!file_write_int32(file, (int32_t)TRIE_SIGNATURE)) 
        return false;
    if (!file_write_int32(file, (int32_t)self->alphabet_size))
        return false;
    if (!file_write_chars(file, (char *)self->alphabet, self->alphabet_size)) 
        return false;
    if (!file_write_int32(file, (int32_t)self->nodes->n))
        return false;

    int i;
    trie_node_t node;

    for (i = 0; i < self->nodes->n; i++) {
        node = self->nodes->a[i];
        if (!file_write_int32(file, node.base) ||
            !file_write_int32(file, node.check)) {
            return false;
        }
    }

    if (!file_write_int32(file, (int32_t)self->data->n))
        return false;

    trie_data_node_t data_node;
    for (i = 0; i < self->data->n; i++) {
        data_node = self->data->a[i];
        if (!file_write_int32(file, (int32_t)data_node.tail) ||
            !file_write_int32(file, (int32_t)data_node.data)) {
            return false;
        }
    }

    if (!file_write_int32(file, (int32_t)self->tail->n))
        return false;

    if (!file_write_chars(file, (char *)self->tail->a, self->tail->n))
        return false;

    return true;
}


bool trie_save(trie_t *self, char *path) {
    FILE *file;
    bool result = false;

    file = fopen(path, "w+");
    if (!file)
        return false;

    result = trie_write(self, file);
    fclose(file);

    return result;
}

trie_t *trie_read(FILE *file) {
    int i;

    long save_pos = ftell(file);

    uint8_t alphabet[NUM_CHARS];

    uint32_t signature;

    if (!file_read_int32(file, (int32_t *)&signature)) 
        goto exit_file_read;

    if (signature != TRIE_SIGNATURE)
        goto exit_file_read;

    uint32_t alphabet_size;

    if (!file_read_int32(file, (int32_t *)&alphabet_size))
        goto exit_file_read;

    log_debug("alphabet_size=%d\n", alphabet_size);

    if (!file_read_chars(file, (char *)alphabet, alphabet_size))
        goto exit_file_read;

    trie_t *trie = trie_new_empty(alphabet, alphabet_size);
    if (!trie)
        goto exit_file_read;

    uint32_t num_nodes;

    if (!file_read_int32(file, (int32_t *)&num_nodes))
        goto exit_trie_created;

    log_debug("num_nodes=%d\n", num_nodes);
    trie_node_array_resize(trie->nodes, num_nodes);

    int32_t base;
    int32_t check;
    trie_node_t node;
    for (i = 0; i < num_nodes; i++) {
        if (!file_read_int32(file, (int32_t *)&base) ||
            !file_read_int32(file, (int32_t *)&check))
            goto exit_trie_created;

        node.base = base;
        node.check = check;
        trie_node_array_push(trie->nodes, node);
    }

    uint32_t num_data_nodes;
    if (!file_read_int32(file, (int32_t *)&num_data_nodes))
        goto exit_trie_created;

    trie_data_array_resize(trie->data, num_data_nodes);
    log_debug("num_data_nodes=%d\n", num_data_nodes);

    uint32_t tail_ptr;
    uint32_t data;
    trie_data_node_t data_node;

    for (i = 0; i < num_data_nodes; i++) {
        if (!file_read_int32(file, (int32_t *)&tail_ptr) ||
            !file_read_int32(file, (int32_t *)&data))
            goto exit_trie_created;
        data_node.tail = tail_ptr;
        data_node.data = data;
        trie_data_array_push(trie->data, data_node);
    }

    uint32_t tail_len;
    if (!file_read_int32(file, (int32_t *)&tail_len))
        goto exit_trie_created;

    uchar_array_resize(trie->tail, tail_len);
    trie->tail->n = tail_len;

    if (!file_read_chars(file, (char *)trie->tail->a, tail_len))
        goto exit_trie_created;

    return trie;

exit_trie_created:
    trie_destroy(trie);
exit_file_read:
    fseek(file, save_pos, SEEK_SET);
    return NULL;
}

trie_t *trie_load(char *path) {
    FILE *file;

    file = fopen(path, "r");
    if (!file)
        return NULL;

    trie_t *trie = trie_read(file);

    fclose(file);

    return trie;
}