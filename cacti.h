#ifndef CACTI_H
#define CACTI_H

#include <stddef.h>

typedef long message_type_t;

/* Predefined messages. */
/* SPAWN - creates new actor using data field as role struct. */
#define MSG_SPAWN (message_type_t)0x06057a6e
/* GODIE - upon parsing this an actor stops receiving messages,
 * but parses messages already in his queue. */
#define MSG_GODIE (message_type_t)0x60bedead
/* HELLO - actor's behaviour upon parsing this message is not predefined,
 * but an actor after spawning a new actor sends this message to him with
 * his own id. */
#define MSG_HELLO (message_type_t)0x0

#ifndef ACTOR_QUEUE_LIMIT
#define ACTOR_QUEUE_LIMIT 1024
#endif

/* Upper bound on the number of actors. */
#ifndef CAST_LIMIT
#define CAST_LIMIT 1048576
#endif

/* Size of a thread pool. */
#ifndef POOL_SIZE
#define POOL_SIZE 3
#endif

typedef struct message {
    message_type_t message_type;
    size_t nbytes;
    void *data;
} message_t;

typedef long actor_id_t;

/* Returns if of an actor invoking this function. */
actor_id_t actor_id_self();

/* First argument is a pointer to actors internal state,
 * second argument is the size of the third argument that is a mutable data of the message.*/
typedef void (*const act_t)(void **stateptr, size_t nbytes, void *data);

typedef struct role {
    size_t nprompts;
    act_t *prompts;
} role_t;

/* Creates an actor system and a thread pool.
 * Only one actor system can exist at the same time. */
int actor_system_create(actor_id_t *actor, role_t *const role);

/* Waits for an actor system of actor. */
void actor_system_join(actor_id_t actor);

/* Sends message to an actor.
 * Returns 0 on success and a negative value on failure. */
int send_message(actor_id_t actor, message_t message);

#endif