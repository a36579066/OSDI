# ifndef USER_PROG_H
# define USER_PROG_H
#include <stdint.h>

void switch_to_user_mode(void *func, void* sp_addr);
void execute_user_program(char *filename);


extern int ready_run_user;

# endif