#ifndef STDBOOL_H
#define STDBOOL_H

typedef enum {
    false = 0,
    true  = 1
} bool;

#define TO_BOOL(x) ((x) ? true : false)

#endif // STDBOOL_H
