#include "cacti.h"
#include "err.h"
#include <pthread.h>
#include <semaphore.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>

// Symbolizes no actor.
#define EMPTY -1000

// State of an actor.
typedef enum STATE {
    DEAD,
    ALIVE
} ACTOR_STATE;

// Cache-friendly dynamic actor queue.
typedef struct actor_queue {
    size_t size;
    size_t capacity;
    size_t front;
    size_t back;
    actor_id_t *data;
} actor_queue_t;

// Cache-friendly static message queue.
typedef struct message_queue {
    size_t size;
    size_t front;
    size_t back;
    message_t *data;
} message_queue_t;

typedef struct actor_type {
    void *stateptr;
    role_t *role;
    ACTOR_STATE state;
    message_queue_t *messages;
    bool taking_msg;
    bool queued;
} actor_t;

// GLOBAL VARIABLES
static pthread_t *workers = NULL;
static actor_t *actors = NULL;
static actor_id_t actor_count = 0;
static actor_id_t alive_count = 0;
static actor_id_t actors_capacity = 0;
static actor_queue_t *actors_ready = NULL;
static actor_id_t workers_sleeping = 0;
static bool end = false;
static bool block_spawn = false;
static bool working = false;
static pthread_t signal_handler;

// SYNCHRONIZATION
static _Thread_local actor_id_t current_actor = EMPTY;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t sleep = PTHREAD_COND_INITIALIZER;

static bool is_any_actor_ready();
static actor_id_t get_actor();
static message_t get_message(actor_id_t actor);
static inline bool is_sleeping(actor_id_t actor);
static void enqueue_actor(actor_id_t actor);
static void enqueue_message(actor_id_t actor, message_t msg);
static void add_actor(role_t *role);
static actor_queue_t *create_actor_queue();
static message_queue_t *new_message_queue();
static void block_signals();
static void free_resources();

static void lock_mutex() {
    if (pthread_mutex_lock(&mutex)) {
        syserr("MUTEX LOCK FAIL");
    }
}

static void unlock_mutex() {
    if (pthread_mutex_unlock(&mutex)) {
        syserr("MUTEX UNLOCK FAIL");
    }
}

static void go_sleep() {
    pthread_cond_wait(&sleep, &mutex);
}

static void wake_everyone() {
    pthread_cond_broadcast(&sleep);
}

static void wake_somebody() {
    pthread_cond_signal(&sleep);
}

static void kill_actor(actor_id_t actor) {
    actors[actor].state = DEAD;
    --alive_count;

    if (alive_count == 0) {
        end = true;
        working = false;

        if (!block_spawn) {
            pthread_cancel(signal_handler);
            pthread_join(signal_handler, NULL);
        }
        wake_everyone();
    }
}

static actor_id_t get_actor() {
    if (actors_ready->size == 0)
        fatal("No actors to get");

    actor_id_t actor = actors_ready->data[actors_ready->front];
    actors_ready->size--;
    actors_ready->front = (actors_ready->front + 1) % actors_ready->capacity;

    actors[actor].queued = false;

    return actor;
}

static void resize_actor_queue() {
    size_t new_cap = actors_ready->capacity * 2;
    actor_id_t *new_data = realloc(actors_ready->data, new_cap * sizeof *new_data);
    if (new_data == NULL)
        syserr("Resize queue fail");
    actors_ready->capacity = new_cap;
    actors_ready->data = new_data;
    if (actors_ready->front > actors_ready->back) {
        for (size_t i = 0; i < actors_ready->size; ++i) {
            actors_ready->data[i + actors_ready->size] = actors_ready->data[i];
        }
        actors_ready->back = (actors_ready->back + actors_ready->size) % new_cap;
    }
}

static void enqueue_actor(actor_id_t actor) {
    actor_queue_t *q = actors_ready;

    if (q->size == q->capacity) {
        resize_actor_queue();
    }

    q->data[q->back] = actor;
    q->size++;
    q->back = (q->back + 1) % q->capacity;
    actors[actor].queued = true;

    if (workers_sleeping > 0) {
        wake_somebody();
    }
}

static message_t get_message(actor_id_t actor) {
    message_queue_t *q = actors[actor].messages;
    if (q->size == 0)
        fatal("No message to get");

    message_t msg = q->data[q->front];
    q->size--;
    q->front = (q->front + 1) % ACTOR_QUEUE_LIMIT;

    if (q->size == 0 && (msg.message_type == MSG_GODIE ||
        !actors[actor].taking_msg)) {
        kill_actor(actor);
    }

    return msg;
}

static void enqueue_message(actor_id_t actor, message_t msg) {
    message_queue_t *q = actors[actor].messages;

    if (q->size == ACTOR_QUEUE_LIMIT)
        fatal("message queue full");

    q->data[q->back] = msg;
    q->size++;
    q->back = (q->back + 1) % ACTOR_QUEUE_LIMIT;

}

static actor_queue_t *create_actor_queue() {
    actor_queue_t *q = NULL;
    q = malloc(sizeof *q);
    if (q == NULL)
        syserr("actor queue creation error");

    q->data = malloc(256 * sizeof *q->data);
    if (q->data == NULL)
        syserr("actor queue could not been initialized");

    q->capacity = 256;
    for (size_t i = 0; i < q->capacity; ++i)
        q->data[i] = EMPTY;
    q->size = 0;
    q->front = 0;
    q->back = 0;

    return q;
}

static message_queue_t *new_message_queue() {
    message_queue_t *q = NULL;
    q = malloc(sizeof *q);
    if (q == NULL)
        syserr("message queue creation error");

    q->data = malloc(ACTOR_QUEUE_LIMIT * sizeof *q->data);
    if (q->data == NULL)
        syserr("message queue could not be initialized");

    q->size = 0;
    q->front = 0;
    q->back = 0;

    return q;
}

static void spawn_actor(role_t *role) {
    add_actor(role);
}

static void godie() {
    actors[current_actor].taking_msg = false;
}

static void *work() {
    block_signals();
    while (true) {
        lock_mutex();
        while (!end && !is_any_actor_ready()) {
            ++workers_sleeping;
            go_sleep();
            --workers_sleeping;
        }
        if (end) {
            unlock_mutex();
            break;
        }

        current_actor = get_actor();
        message_t msg = get_message(current_actor);

        if (msg.message_type == MSG_SPAWN) {
            spawn_actor(msg.data);

        } else if (msg.message_type == MSG_GODIE) {
            godie();
        } else {
            role_t *role = actors[current_actor].role;
            void **stp = &actors[current_actor].stateptr;
            unlock_mutex();

            if (msg.message_type > (message_type_t) role->nprompts) {
                fatal("Bad message");
            }
            role->prompts[msg.message_type](stp, msg.nbytes, msg.data);
            lock_mutex();
        }

        if (!actors[current_actor].queued &&
            actors[current_actor].messages->size > 0)
            enqueue_actor(current_actor);

        unlock_mutex();
    }
    return NULL;
}

static bool initialize_threads() {
    workers = malloc(POOL_SIZE * sizeof *workers);
    if (workers == NULL)
        return false;
    for (size_t i = 0; i < POOL_SIZE; ++i) {
        if (pthread_create(&workers[i], NULL, work, NULL) != 0) {
            for (size_t j = 0; j < i; ++j) {
                pthread_cancel(workers[j]);
            }
            return false;
        }
    }
    return true;
}

static void *shutdown() {
    sigset_t sigset;
    sigfillset(&sigset);
    sigdelset(&sigset, SIGINT);
    pthread_sigmask(SIG_BLOCK, &sigset, NULL);
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGINT);
    int sig;
    do {
        sigwait(&sigset, &sig);
    } while (sig != SIGINT || end);

    if (!block_spawn) {
        block_spawn = true;
        message_t kill = {
            .message_type = MSG_GODIE
        };
        for (size_t i = 0; i < (size_t) actor_count; ++i) {
            send_message(i, kill);
        }
    }
    return NULL;
}

static void block_signals() {
    sigset_t sigset;
    sigfillset(&sigset);
}

static bool create_signal_handler() {
    if (pthread_create(&signal_handler, NULL, shutdown, NULL) != 0)
        syserr("error while creating a signal handler");
    return true;
}

int actor_system_create(actor_id_t *actor, role_t *const role) {
    lock_mutex();
    create_signal_handler();
    *actor = 0;
    if (working || role == NULL) {
        unlock_mutex();
        return -998;
    }

    if (!initialize_threads()) {
        unlock_mutex();
        return -999;
    }
    actors = calloc(1024, sizeof *actors);
    actors_capacity = 1024;
    if (actors == NULL) {
        unlock_mutex();
        return -997;
    }
    actors_ready = create_actor_queue();

    add_actor(role);
    working = true;
    unlock_mutex();

    return 0;
}

static void free_msg_queue(message_queue_t *q) {
    free(q->data);
    free(q);
}

static void free_resources() {
    end = false;
    for (size_t i = 0; i < (size_t) actor_count; ++i) {
        free_msg_queue(actors[i].messages);
    }
    free(actors);
    actors = NULL;
    actor_count = 0;
    actors_capacity = 0;
    free(workers);
    workers = NULL;
    free(actors_ready->data);
    free(actors_ready);
    alive_count = 0;
    block_spawn = false;
}

void actor_system_join(actor_id_t actor) {
    if (actor < 0 || actor >= (actor_id_t) actor_count)
        return;

    if (workers != NULL) {
        for (actor_id_t i = 0; i < POOL_SIZE; ++i) {
            pthread_join(workers[i], NULL);
        }
    }
    free_resources();
}

int send_message(actor_id_t actor, message_t msg) {
    if (actor < 0 || actor >= actor_count)
        return -2;

    lock_mutex();
    if (!actors[actor].taking_msg) {
        unlock_mutex();
        return -1;
    }
    enqueue_message(actor, msg);

    if (is_sleeping(actor)) {
        enqueue_actor(actor);
    }

    unlock_mutex();
    return 0;
}

actor_id_t actor_id_self() {
    return current_actor;
}

static inline bool is_sleeping(actor_id_t actor) {
    return !actors[actor].queued;
}

static inline bool is_any_actor_ready() {
    return actors_ready != NULL && actors_ready->size > 0;
}

static void add_actor(role_t *role) {
    if (block_spawn)
        return;

    actor_id_t id = actor_count;
    actor_count++;
    alive_count++;

    actor_t actor = {
        .stateptr = NULL,
        .role = role,
        .state = ALIVE,
        .messages = new_message_queue(),
        .taking_msg = true,
        .queued = false
    };

    if (actor_count >= actors_capacity) {
        size_t new_cap = actors_capacity * 2;
        actor_t *new_data = realloc(actors, new_cap * sizeof *new_data);
        if (new_data == NULL)
            syserr("actor vector error");
        actors_capacity = new_cap;
        actors = new_data;
    }

    actors[id] = actor;
    message_t hello = {
        .message_type = MSG_HELLO,
        .nbytes = sizeof(actor_id_t),
        .data = (void *) actor_id_self()
    };

    enqueue_message(id, hello);
    enqueue_actor(id);
}