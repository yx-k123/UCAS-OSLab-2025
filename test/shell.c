/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *            Copyright (C) 2018 Institute of Computing Technology, CAS
 *               Author : Han Shukai (email : hanshukai@ict.ac.cn)
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *                  The shell acts as a task running in user mode.
 *       The main function is to make system calls through the user's output.
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this
 * software and associated documentation files (the "Software"), to deal in the Software
 * without restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit
 * persons to whom the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * */

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>

#define SHELL_BEGIN 20
#define BUFF_SIZE 128
#define MAX_ARG_NUM 16
#define MAX_ARG_LEN 32
char argv[MAX_ARG_NUM][MAX_ARG_LEN];

int isspace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

int atoi(const char *str) {
    int res = 0;
    int i = 0;
    while (str[i] >= '0' && str[i] <= '9') {
        res = res * 10 + (str[i] - '0');
        i++;
    }
    return res;
}

int parse_args(const char *buff) {
    int argc = 0;
    int i = 0;
    for(int j = 0; j < MAX_ARG_NUM; j++){
        argv[j][0] = '\0';
    }
    while (*buff) {
        while (isspace(*buff)) {
            buff++;
        } 

        if (*buff == '\0') break;

        if (argc >= MAX_ARG_NUM) break;

        i = 0;
        while (*buff && !isspace(*buff) && i < MAX_ARG_LEN - 1) {
            argv[argc][i++] = *buff++;
        }
        argv[argc][i] = '\0';
        argc++;
    }
    return argc;
}

int main(void)
{
    sys_move_cursor(0, SHELL_BEGIN);
    printf("------------------- COMMAND -------------------\n");
    printf("> root@UCAS_OS: ");

    char buff[BUFF_SIZE] = {0};
    int idx = 0;
    int temp = 0;
    int end = 0;
    while (1)
    {
        // TODO [P3-task1]: call syscall to read UART port
        while ((temp = sys_getchar()) == -1);
    
        // TODO [P3-task1]: parse input
        // note: backspace maybe 8('\b') or 127(delete)
        if (temp == '\b' || temp == 127) {
            if (idx > 0) {
                sys_write("\b");
                sys_reflush();
                buff[--idx] = '\0';
            }
        } else if (temp == '\n' || temp == '\r') {
            sys_write("\n");
            sys_reflush();
            end = 1;
            buff[idx] = '\0';
            idx = 0;
        } else {
            buff[idx++] = (char)temp;
            sys_write((char[]){(char)temp, '\0'});
            sys_reflush();
        }

        if (!end) {
            continue;
        }
        end = 0;

        // TODO [P3-task1]: ps, exec, kill, clear    
        int argc = parse_args(buff);
        if (argc == 0) {
            printf("> root@UCAS_OS: ");
            continue;
        } 

        if (strcmp(argv[0], "ps") == 0) {
            sys_ps();
        } else if (strcmp(argv[0], "clear") == 0) {
            sys_clear();
            sys_move_cursor(0, SHELL_BEGIN);
            printf("------------------- COMMAND -------------------\n");
        } else if (strcmp(argv[0], "exec") == 0) {
            if (argc < 2) {
                printf("Usage: exec [task_name] [args...] [&]\n");
            } else {
                int has_amp = 0;
                if (strcmp(argv[argc-1], "&") == 0) {
                    has_amp = 1;
                    argc--; 
                }
                char *args[MAX_ARG_NUM];
                for (int i = 0; i < argc - 1 && i < MAX_ARG_NUM; i++) {
                    args[i] = argv[i + 1];
                }
                pid_t pid = sys_exec(argv[1], argc - 1, args);
                if (pid > 0) {
                    if (!has_amp) {
                        sys_waitpid(pid);
                    }
                } else {
                    printf("Failed to exec %s\n", argv[1]);
                }
            }
        } else if (strcmp(argv[0], "kill") == 0) {
            if (argc < 2) {
                printf("Usage: kill [pid]\n");
            } else {
                int pid = atoi(argv[1]);
                sys_kill(pid);
            }
        } else {
            printf("Unknown command: %s\n", argv[0]);
        }

        printf("> root@UCAS_OS: ");

        /************************************************************/
        // TODO [P6-task1]: mkfs, statfs, cd, mkdir, rmdir, ls

        // TODO [P6-task2]: touch, cat, ln, ls -l, rm
        /************************************************************/
    }

    return 0;
}
