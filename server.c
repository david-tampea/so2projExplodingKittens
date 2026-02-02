#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <time.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "contract.h"

#define PORT 12346
#define BUF_SIZE 1024

typedef struct {
    game_state_t game;
    int client_socks[NUM_PLAYERS];
} game_instance_t;

const char *card_name(ek_card_t card) {
    switch(card.type) {
        case CT_EXPLODING_KITTEN: return "exploding_kitten";
        case CT_DEFUSE: return "defuse";
        case CT_ATTACK: return "attack";
        case CT_SKIP: return "skip";
        case CT_FAVOR: return "favor";
        case CT_SHUFFLE: return "shuffle";
        case CT_SEE_THE_FUTURE: return "see_the_future";
        case CT_NOPE: return "nope";
        case CT_CAT:
            switch (card.subtype) {
                case CAT_TACOCAT: return "tacocat";
                case CAT_CATTERMELLON: return "cattermellon";
                case CAT_POTATO: return "potato";
                case CAT_BEARD: return "beard";
                case CAT_RAINBOW: return "rainbow";
                default: return "cat";
            }
        default: return "unknown";
    }
}

void send_to_player(int player_idx, const char *msg, game_instance_t *instance) {
    send(instance->client_socks[player_idx], msg, strlen(msg), 0);
}

void broadcast(const char *msg, game_instance_t *instance) {
    for (int i = 0; i < NUM_PLAYERS; ++i)
        send_to_player(i, msg, instance);
}

void add_to_hand(player_t *p, ek_card_t card) {
    if (p->hand_size < MAX_HAND)
        p->hand[p->hand_size++] = card;
}

void remove_from_hand(player_t *p, int idx) {
    for (int i = idx; i < p->hand_size - 1; i++)
        p->hand[i] = p->hand[i+1];
    p->hand_size--;
}

void insert_card_at(ek_card_t *deck, int *deck_size, ek_card_t card, int position) {
    for (int i = *deck_size; i > position; i--)
        deck[i] = deck[i - 1];
    deck[position] = card;
    (*deck_size)++;
}

void shuffle_deck(ek_card_t *deck, int size) {
    for (int i = size - 1; i > 0; --i) {
        int j = rand() % (i + 1);
        ek_card_t temp = deck[i];
        deck[i] = deck[j];
        deck[j] = temp;
    }
}

ek_card_t create_card(card_type_t type, int subtype) {
    ek_card_t c;
    c.type = type;
    c.subtype = subtype;
    return c;
}

void build_deck(ek_card_t *deck, int *size) {
    int idx = 0;
    for (int i = 0; i < 5; i++)
        for (int j = 0; j < 4; j++)
            deck[idx++] = create_card(CT_CAT, i);

    for (int i = 0; i < 4; i++) deck[idx++] = create_card(CT_ATTACK, -1);
    for (int i = 0; i < 4; i++) deck[idx++] = create_card(CT_SKIP, -1);
    for (int i = 0; i < 4; i++) deck[idx++] = create_card(CT_FAVOR, -1);
    for (int i = 0; i < 5; i++) deck[idx++] = create_card(CT_SHUFFLE, -1);
    for (int i = 0; i < 5; i++) deck[idx++] = create_card(CT_SEE_THE_FUTURE, -1);
    for (int i = 0; i < 5; i++) deck[idx++] = create_card(CT_NOPE, -1);
    for (int i = 0; i < 6; i++) deck[idx++] = create_card(CT_DEFUSE, -1);
    for (int i = 0; i < NUM_PLAYERS - 1; i++)
        deck[idx++] = create_card(CT_EXPLODING_KITTEN, -1);

    *size = idx;
    shuffle_deck(deck, *size);
}
int ask_for_card_position(game_instance_t *instance, int player_idx, int max_pos) {
    char buffer[BUF_SIZE];
    char prompt[128];
    sprintf(prompt, "Alege pozitia (0-%d) unde sa reintroduci exploding kitten:\n", max_pos);
    send_to_player(player_idx, prompt, instance);
    while (1) {
        memset(buffer, 0, BUF_SIZE);
        recv(instance->client_socks[player_idx], buffer, BUF_SIZE - 1, 0);
        int pos = atoi(buffer);
        if (pos >= 0 && pos <= max_pos) {
            return pos;
        }
        send_to_player(player_idx, "Pozitie invalida. Incearca din nou:\n", instance);
    }
}

void handle_see_the_future(game_instance_t *instance, int player_idx) {
    char msg[256];
    int limit = instance->game.deck_size < 3 ? instance->game.deck_size : 3;
    sprintf(msg, "Urmatoarele %d carti sunt:\n", limit);
    for (int i = 0; i < limit; i++) {
        sprintf(msg + strlen(msg), "- %s\n", card_name(instance->game.deck[instance->game.deck_size - 1 - i]));
    }
    send_to_player(player_idx, msg, instance);
}

int check_nope_response(game_instance_t *instance, int acting_player) {
    int other = (acting_player + 1) % NUM_PLAYERS;
    char prompt[] = "[INPUT] Vrei sa joci NOPE? (da/nu):\n";
    send_to_player(other, prompt, instance);
    char buffer[BUF_SIZE];
    memset(buffer, 0, BUF_SIZE);
    recv(instance->client_socks[other], buffer, BUF_SIZE - 1, 0);
    if (strncmp(buffer, "da", 2) == 0) {
        player_t *p = &instance->game.players[other];
        for (int i = 0; i < p->hand_size; i++) {
            if (p->hand[i].type == CT_NOPE) {
                remove_from_hand(p, i);
                send_to_player(other, "Ai jucat NOPE.\n", instance);
                int counter = check_nope_response(instance, other);
                if (counter) {
                    send_to_player(acting_player, "NOPE-ul tau a fost anulat cu un alt NOPE!\n", instance);
                    return 0;
                }
                return 1;
            }
        }
        send_to_player(other, "Nu ai NOPE.\n", instance);
    }
    return 0;
}

void handle_favor(game_instance_t *instance, int player_idx) {
    int other = (player_idx + 1) % NUM_PLAYERS;
    player_t *opponent = &instance->game.players[other];
    player_t *me = &instance->game.players[player_idx];
    if (opponent->hand_size == 0) {
        send_to_player(player_idx, "Oponentul nu are carti.\n", instance);
        return;
    }
    int rand_idx = rand() % opponent->hand_size;
    add_to_hand(me, opponent->hand[rand_idx]);
    remove_from_hand(opponent, rand_idx);
    send_to_player(player_idx, "Ai primit o carte aleatorie de la oponent.\n", instance);
}

void handle_shuffle(game_instance_t *instance) {
    shuffle_deck(instance->game.deck, instance->game.deck_size);
}

void handle_two_of_a_kind(game_instance_t *instance, int player_idx, int card_subtype) {
    int other = (player_idx + 1) % NUM_PLAYERS;
    player_t *op = &instance->game.players[other];
    player_t *me = &instance->game.players[player_idx];

    if (op->hand_size == 0) {
        send_to_player(player_idx, "Oponentul nu are carti.\n", instance);
        return;
    }

    char buffer[BUF_SIZE];
    char prompt[128];
    sprintf(prompt, "[INPUT] Alege o pozitie intre 1 si %d din mana oponentului:\n", op->hand_size);
    send_to_player(player_idx, prompt, instance);
    while (1) {
        memset(buffer, 0, BUF_SIZE);
        recv(instance->client_socks[player_idx], buffer, BUF_SIZE - 1, 0);
        int pos = atoi(buffer);
        if (pos >= 1 && pos <= op->hand_size) {
            add_to_hand(me, op->hand[pos - 1]);
            remove_from_hand(op, pos - 1);
            send_to_player(player_idx, "Ai luat o carte de la oponent.\n", instance);
            return;
        }
        send_to_player(player_idx, "Pozitie invalida. Incearca din nou:\n", instance);
    }
}

int parse_card_type(const char *name) {
    if (strcmp(name, "defuse") == 0) return CT_DEFUSE;
    if (strcmp(name, "attack") == 0) return CT_ATTACK;
    if (strcmp(name, "skip") == 0) return CT_SKIP;
    if (strcmp(name, "favor") == 0) return CT_FAVOR;
    if (strcmp(name, "shuffle") == 0) return CT_SHUFFLE;
    if (strcmp(name, "see_the_future") == 0) return CT_SEE_THE_FUTURE;
    if (strcmp(name, "nope") == 0) return CT_NOPE;
    if (strcmp(name, "exploding_kitten") == 0) return CT_EXPLODING_KITTEN;
    if (strcmp(name, "tacocat") == 0) return CT_CAT;
    if (strcmp(name, "cattermellon") == 0) return CT_CAT;
    if (strcmp(name, "potato") == 0) return CT_CAT;
    if (strcmp(name, "beard") == 0) return CT_CAT;
    if (strcmp(name, "rainbow") == 0) return CT_CAT;
    return -1;
}

void handle_three_of_a_kind(game_instance_t *instance, int player_idx, int card_subtype) {
    int other = (player_idx + 1) % NUM_PLAYERS;
    player_t *op = &instance->game.players[other];
    player_t *me = &instance->game.players[player_idx];

    char buffer[BUF_SIZE];
    send_to_player(player_idx, "[INPUT] Alege ce carte vrei de la oponent (ex: defuse, skip, etc.):\n", instance);

    while (1) {
        memset(buffer, 0, BUF_SIZE);
        recv(instance->client_socks[player_idx], buffer, BUF_SIZE - 1, 0);
        buffer[strcspn(buffer, "\n")] = 0;

        int guessed_type = parse_card_type(buffer);
        if (guessed_type == -1) {
            send_to_player(player_idx, "Tip invalid. Incearca din nou:\n", instance);
            continue;
        }

        for (int i = 0; i < op->hand_size; i++) {
            if (op->hand[i].type == guessed_type) {
                add_to_hand(me, op->hand[i]);
                remove_from_hand(op, i);
                send_to_player(player_idx, "Ai ghicit si ai primit cartea!\n", instance);
                return;
            }
        }

        send_to_player(player_idx, "Oponentul nu are aceasta carte. Ai pierdut sansa.\n", instance);
        return;
    }
}
void handle_player_turn(int idx, game_instance_t *instance) {
    char buffer[BUF_SIZE], msg[BUF_SIZE];
    game_state_t *game = &instance->game;
    player_t *p = &game->players[idx];

    if (!p->is_alive) return;

    sprintf(msg, "[INPUT] Este randul tau. Mana ta:\n");
    send_to_player(idx, msg, instance);

    for (int i = 0; i < p->hand_size; i++) {
        sprintf(msg, "[%d] %s\n", i, card_name(p->hand[i]));
        send_to_player(idx, msg, instance);
    }

    send_to_player(idx, "[INPUT] Scrie indexul cartii (sau -1 pentru a trage):\n", instance);
    memset(buffer, 0, BUF_SIZE);
    recv(instance->client_socks[idx], buffer, BUF_SIZE - 1, 0);
    int choice = atoi(buffer);

    if (choice == -1) {
        ek_card_t card = game->deck[--game->deck_size];
        sprintf(msg, "Ai tras: %s\n", card_name(card));
        send_to_player(idx, msg, instance);

        if (card.type == CT_EXPLODING_KITTEN) {
            int defuse_idx = -1;
            for (int i = 0; i < p->hand_size; i++) {
                if (p->hand[i].type == CT_DEFUSE) {
                    defuse_idx = i;
                    break;
                }
            }

            if (defuse_idx != -1) {
                remove_from_hand(p, defuse_idx);
                send_to_player(idx, "Ai folosit DEFUSE!\n", instance);
                int pos = ask_for_card_position(instance, idx, game->deck_size);
                insert_card_at(game->deck, &game->deck_size, card, game->deck_size - pos);
            } else {
                send_to_player(idx, "Ai explodat! Ai pierdut jocul.\n", instance);
                p->is_alive = 0;
                game->game_over = 1;
                return;
            }
        } else {
            add_to_hand(p, card);
        }

    } else if (choice >= 0 && choice < p->hand_size) {
        ek_card_t card = p->hand[choice];
        int count = 1;
        int second = -1, third = -1;

        for (int i = 0; i < p->hand_size && (second == -1 || third == -1); i++) {
            if (i != choice && p->hand[i].type == CT_CAT && p->hand[i].subtype == card.subtype) {
                if (second == -1) second = i;
                else third = i;
                count++;
            }
        }

        if (card.type == CT_NOPE || card.type == CT_DEFUSE || card.type == CT_EXPLODING_KITTEN) {
            send_to_player(idx, "Aceasta carte nu poate fi jucata direct.\n", instance);
            return;
        }

        int canceled = check_nope_response(instance, idx);
        if (canceled) {
            send_to_player(idx, "Actiunea ta a fost anulata cu NOPE.\n", instance);
            remove_from_hand(p, choice);
            return;
        }

        if (card.type == CT_SEE_THE_FUTURE) {
            remove_from_hand(p, choice);
            handle_see_the_future(instance, idx);
        } else if (card.type == CT_SKIP) {
            remove_from_hand(p, choice);
            send_to_player(idx, "Ai sarit tura. Nu tragi carte.\n", instance);
            return;
        } else if (card.type == CT_SHUFFLE) {
            remove_from_hand(p, choice);
            handle_shuffle(instance);
            send_to_player(idx, "Pachetul a fost amestecat.\n", instance);
        } else if (card.type == CT_FAVOR) {
            remove_from_hand(p, choice);
            handle_favor(instance, idx);
        } else if (card.type == CT_ATTACK) {
            remove_from_hand(p, choice);
            send_to_player(idx, "Ai jucat ATTACK. Oponentul face 2 ture.\n", instance);
            game->turns_to_take = 2;
            game->current_player_idx = (idx + 1) % NUM_PLAYERS;
            return;
        } else if (card.type == CT_CAT && count == 2 && second != -1) {
            remove_from_hand(p, choice);
            remove_from_hand(p, second < choice ? second : second - 1);
            handle_two_of_a_kind(instance, idx, card.subtype);
        } else if (card.type == CT_CAT && count >= 3 && second != -1 && third != -1) {
            remove_from_hand(p, choice);
            if (second > choice) second--;
            remove_from_hand(p, second);
            if (third > choice && third > second) third -= 2;
            else if (third > choice || third > second) third--;
            remove_from_hand(p, third);
            handle_three_of_a_kind(instance, idx, card.subtype);
        } else {
            remove_from_hand(p, choice);
            send_to_player(idx, "Ai jucat o carte fara efect.\n", instance);
        }

    } else {
        send_to_player(idx, "Alegere invalida.\n", instance);
        return;
    }

    game->turns_to_take--;
    if (game->turns_to_take <= 0) {
        game->current_player_idx = (game->current_player_idx + 1) % NUM_PLAYERS;
        game->turns_to_take = 1;
    }
}
void *game_thread(void *arg) {
    game_instance_t *instance = (game_instance_t *)arg;
    game_state_t *game = &instance->game;
    int *client_socks = instance->client_socks;

    srand(time(NULL) ^ pthread_self());

    for (int i = 0; i < NUM_PLAYERS; i++) {
        char name_buf[NAME_LEN];
        memset(name_buf, 0, NAME_LEN);
        send(client_socks[i], "Introdu numele tau:\n", 21, 0);
        recv(client_socks[i], name_buf, NAME_LEN - 1, 0);
        name_buf[strcspn(name_buf, "\n")] = 0;
        strncpy(game->players[i].name, name_buf, NAME_LEN - 1);
        printf("Joc [%p] - Jucator %d: %s conectat.\n", (void*)pthread_self(), i + 1, game->players[i].name);
        send(client_socks[i], "Conectat la server. Asteptam ceilalti jucatori...\n", 50, 0);
    }

    game->deck_size = 0;
    game->current_player_idx = 0;
    game->turns_to_take = 1;
    game->game_over = 0;

    build_deck(game->deck, &game->deck_size);

    for (int i = 0; i < NUM_PLAYERS; ++i) {
        game->players[i].id = i;
        game->players[i].is_alive = 1;
        game->players[i].hand_size = 0;
        add_to_hand(&game->players[i], create_card(CT_DEFUSE, -1));
        for (int j = 0; j < 4; j++) {
            add_to_hand(&game->players[i], game->deck[--game->deck_size]);
        }
    }

    broadcast("=== Jocul a inceput! ===\n", instance);

    while (!game->game_over) {
        handle_player_turn(game->current_player_idx, instance);
    }

    int winner = -1;
    for (int i = 0; i < NUM_PLAYERS; i++) {
        if (game->players[i].is_alive) {
            winner = i;
            break;
        }
    }

    if (winner != -1) {
        char win_msg[64];
        sprintf(win_msg, "=== %s a castigat jocul! ===\n", game->players[winner].name);
        broadcast(win_msg, instance);
    } else {
        broadcast("=== Niciun castigator. ===\n", instance);
    }

    for (int i = 0; i < NUM_PLAYERS; i++)
        close(client_socks[i]);

    free(instance);
    return NULL;
}

int main() {
    int server_sock, addr_len;
    struct sockaddr_in server_addr, client_addr;

    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("socket");
        exit(1);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        exit(1);
    }

    if (listen(server_sock, 10) < 0) {
        perror("listen");
        exit(1);
    }

    printf("[SERVER] Server pornit. Asteptam jucatori...\n");

    while (1) {
        game_instance_t *instance = malloc(sizeof(game_instance_t));
        if (!instance) {
            perror("malloc");
            continue;
        }

        for (int i = 0; i < NUM_PLAYERS; i++) {
            addr_len = sizeof(client_addr);
            int client_sock = accept(server_sock, (struct sockaddr*)&client_addr, (socklen_t*)&addr_len);
            if (client_sock < 0) {
                perror("accept");
                i--;
                continue;
            }
            instance->client_socks[i] = client_sock;
        }

        pthread_t tid;
        pthread_create(&tid, NULL, game_thread, instance);
        pthread_detach(tid);
    }

    close(server_sock);
    return 0;
}
