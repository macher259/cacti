#ifndef CACTI_ERR_H
#define CACTI_ERR_H

#ifndef _ERR_
#define _ERR_

/* Prints information about an error from a system function and exits. */
extern void syserr(const char *fmt, ...);

/* Prints information about an error and exits. */
extern void fatal(const char *fmt, ...);

#endif

#endif //CACTI_ERR_H
