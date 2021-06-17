# cacti

Actor model implementation in C using pthreads for Concurrent Programming class.
https://en.wikipedia.org/wiki/Actor_model

Uses a thread pool using pthread.
Upon receiving *SIGINT* program disables the ability to add new actors and makes all existing actors to stop receiving new messages, after handling every message in actor queues, destroys the actor system.

There can only be one actor system at the same time.
An actor can be given a role - an array of function pointers to handle incoming messages based on message type.
Every actor has its own message queue.

To send *message* to an *actor* one shall invoke *send_message(actor, message)*.

There are three predefined messages:
1. MSG_SPAWN - uses the third argument of message as a *role* and creates a new actor with such *role*. Then sends MSG_HELLO to it with its id.
2. MSG_GODIE - actor shall stop receiving messages, still parses messages in his queue.
3. MSG_HELLO - doesn't have a predefined implementation, but is sent to an actor after spawn.
    