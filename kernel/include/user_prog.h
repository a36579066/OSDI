# ifndef USER_PROG_H
# define USER_PROG_H
#include <stdint.h>
#include "thread.h"

void execute_user_program(char *filename);
void *load_user_program(const char *filename);
thread_t *start_user_process(const char *filename);


extern int ready_run_user;

# endif
