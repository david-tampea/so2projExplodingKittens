#ifndef CONTRACT_H
#define CONTRACT_H

// Tipuri de carti
typedef enum {
    CT_EXPLODING_KITTEN = 0,
    CT_DEFUSE,
    CT_ATTACK,
    CT_SKIP,
    CT_FAVOR,
    CT_SHUFFLE,
    CT_SEE_THE_FUTURE,
    CT_NOPE,
    CT_CAT
} card_type_t;

// Tipuri de pisici (pentru combinatii fara efect)
typedef enum {
    CAT_TACOCAT = 0,
    CAT_CATTERMELLON,
    CAT_POTATO,
    CAT_BEARD,
    CAT_RAINBOW,
    CAT_NONE = -1
} cat_name_t;

// Structura unei carti
typedef struct {
    card_type_t type;
    int subtype; // doar daca e CT_CAT
} ek_card_t;

// Structura unui jucator
#define MAX_HAND 20
#define NAME_LEN 32
#define MAX_DECK 100
#define NUM_PLAYERS 2

typedef struct {
    int id; // socket fd
    char name[NAME_LEN];
    ek_card_t hand[MAX_HAND];
    int hand_size;
    int is_alive;
} player_t;

// Starea jocului
typedef struct {
    player_t players[NUM_PLAYERS];
    ek_card_t deck[MAX_DECK];
    int deck_size;
    int current_player_idx;
    int turns_to_take;
    int game_over;
} game_state_t;

#endif
