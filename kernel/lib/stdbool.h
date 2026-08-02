#ifndef KERNEL_STDBOOL_H
#define KERNEL_STDBOOL_H

typedef enum {
    false = 0,
    true  = 1
} bool;

#define TO_BOOL(x) ((x) ? true : false)

#endif // KERNEL_STDBOOL_H
