#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <ctype.h>

#define MAX_COMMAND_LENGTH 2048
#define MAX_ARGS   512

pid_t pids_of_process[150];
int bg_count = 0;
int foreground_only_mode = 0; 

struct command{
    char* args[MAX_ARGS + 1];
    int   argc;             // argument count
    char* inputf;
    char* outputf;
    bool  is_bg;            // Check if file is bg
};
// Prototype func

void print_prompt(void);
void read_input(char* input);
struct command *parse_command(char* input);
bool is_comment_or_blank(char* input);
void builtin_cd(struct command* cmd);
void builtin_exit(void);
void builtin_status(int last_status);
bool is_builtin(char* command);
void execute_command(struct command* cmd, int* status);
void setup_redirection(struct command* cmd);
void add_background_pid(pid_t pid);
void check_background_processes(void);
void handle_SIGTSTP(int signo);

int main(void) {
   

    char input[MAX_COMMAND_LENGTH];
    struct command *cmd;
    int last_status = 0;
     // Sigaction stuff

    struct sigaction SIGINT_action;
    SIGINT_action.sa_handler = SIG_IGN;
    SIGINT_action.sa_flags = 0;
    sigfillset(&SIGINT_action.sa_mask);
    sigaction(SIGINT, &SIGINT_action, NULL);

    struct sigaction SIGTSTP_action;
    SIGTSTP_action.sa_handler = handle_SIGTSTP;
    SIGTSTP_action.sa_flags = 0;
    sigfillset(&SIGTSTP_action.sa_mask);
    sigaction(SIGTSTP, &SIGTSTP_action, NULL);
    while(1){
        check_background_processes();
        print_prompt();
        read_input(input);
        if(is_comment_or_blank(input)){
            continue;
        }
        cmd = parse_command(input);
        if (cmd->argc == 0){                // Check for empty commands
            free(cmd);
            continue;
        }
        if (is_builtin(cmd->args[0])) {
            if (strcmp(cmd->args[0], "cd") == 0) {
                builtin_cd(cmd);
            }
            else if (strcmp(cmd->args[0], "exit") == 0) {
                builtin_exit();
            }
            else if (strcmp(cmd->args[0], "status") == 0) {
                builtin_status(last_status);
            }
        }
        else {
            execute_command(cmd, &last_status);
        
        }
    }
}

void print_prompt(void){
    printf(": ");
    fflush(stdout);
}
void read_input(char* input){
    fgets(input, MAX_COMMAND_LENGTH, stdin);
}
struct command *parse_command(char* input){
    struct command *curr_command = (struct command *) calloc(1,
        sizeof(struct command));
        // Tokenize the input
        char *token = strtok(input, " \n");
        while(token){
        if(!strcmp(token,"<")){
        curr_command->inputf = strdup(strtok(NULL," \n"));
        } else if(!strcmp(token,">")){
        curr_command->outputf = strdup(strtok(NULL," \n"));
        } else if(!strcmp(token,"&")){
        curr_command->is_bg = true;
        } else{
        curr_command->args[curr_command->argc++] = strdup(token);
        }
        token=strtok(NULL," \n");
        }
        return curr_command;
}

void builtin_cd(struct command* cmd){
    // Check for aguments if no argument go home
    char* path;
    if (cmd->argc == 1){
        path = getenv("HOME");
    }
    else{ 
        path = cmd->args[1];
    }

    if (chdir(path) != 0){
        perror("cd failed");
    }
}
void builtin_exit(void){
    int i = 0;
    while(i < bg_count){
        kill(pids_of_process[i], SIGKILL);
        i++;
    }
    exit(0);

}
void builtin_status(int last_status){
    if (WIFEXITED(last_status)){
        int exit_status = WEXITSTATUS(last_status);
        printf("exit value %d\n", exit_status);
        fflush(stdout);
    }
    else if(WIFSIGNALED(last_status)){
        int terminated_status = WTERMSIG(last_status);
        printf("terminated by signal %d\n", terminated_status);
        fflush(stdout);
    }
    
}
bool is_builtin(char* command){
    if (strcmp(command, "exit") == 0 || strcmp(command, "cd") == 0 || strcmp(command, "status") == 0){
        return true;
    }
    return false;
}


void execute_command(struct command* cmd, int* status){
    //Handles execution of non built in commands by forking and execing
    pid_t fork_pid = fork();
    if (fork_pid == -1){
        perror("Fork failed");
        return;
    }
    if (fork_pid == 0){
        if (!cmd->is_bg) {
            struct sigaction sa_default;
            sa_default.sa_handler = SIG_DFL;
            sa_default.sa_flags = 0;
            sigfillset(&sa_default.sa_mask);
            sigaction(SIGINT, &sa_default, NULL);
        }
        struct sigaction sa_ignore_tstp;
        sa_ignore_tstp.sa_handler = SIG_IGN;
        sa_ignore_tstp.sa_flags = 0;
        sigfillset(&sa_ignore_tstp.sa_mask);
        sigaction(SIGTSTP, &sa_ignore_tstp, NULL);
        setup_redirection(cmd);
        execvp(cmd->args[0], cmd->args);
        perror(cmd->args[0]);
        exit(1);
    }
    else{
        if (cmd->is_bg && !foreground_only_mode){
        printf("background pid is %d\n", fork_pid);
        fflush(stdout);
        add_background_pid(fork_pid);
        }
        else{ 
            waitpid(fork_pid, status, 0);
            if (WIFSIGNALED(*status)){
            int sig_num = WTERMSIG(*status);
            printf("terminated by signal %d\n", sig_num);
            fflush(stdout);        
    
    }
        }
    }
}
void setup_redirection(struct command* cmd){
    int filedescriptor;
    if (cmd->inputf != NULL){
        filedescriptor = open(cmd->inputf, O_RDONLY);
        if (filedescriptor == -1) {
            perror(cmd->inputf);
            exit(1);
        }
        dup2(filedescriptor, 0);
        close(filedescriptor);
    }
    else if (cmd->is_bg == true){ 
        filedescriptor = open("/dev/null", O_RDONLY);
        dup2(filedescriptor, 0);
        close(filedescriptor);
    }
    if (cmd->outputf != NULL){
        filedescriptor = open(cmd->outputf, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if(filedescriptor == -1){
            perror(cmd->outputf);
            exit(1);
        }
        dup2(filedescriptor, 1);
        close(filedescriptor);
    }
    else if (cmd->is_bg == true) {
        filedescriptor = open("/dev/null", O_WRONLY);
        dup2(filedescriptor, 1);
        close(filedescriptor);
    }
    return;
}



void add_background_pid(pid_t pid){
    pids_of_process[bg_count] = pid;
    bg_count++;
}
void check_background_processes(void){
    int status;
    pid_t result;
    for (int i = 0; i < bg_count; i++){
        result = waitpid(pids_of_process[i], &status, WNOHANG);
        
        if (result > 0){
            // Process finished
            printf("background pid %d is done: ", result);
            
            if (WIFEXITED(status)){
                printf("exit value %d\n", WEXITSTATUS(status));
            }
            else if (WIFSIGNALED(status)){
                printf("terminated by signal %d\n", WTERMSIG(status));
            }
            fflush(stdout);
            // Remove from array - shift everything left
            for (int j = i; j < bg_count - 1; j++){
                pids_of_process[j] = pids_of_process[j + 1];
            }
            bg_count--;
            i--;
        }
    }
}

void handle_SIGTSTP(int signo){
    if (foreground_only_mode == 0){
        // Enter foreground-only mode
        char* message = "Entering foreground-only mode (& is now ignored)\n";
        write(STDOUT_FILENO, message, 50);
        foreground_only_mode = 1;
    }
    else{
        // Exit foreground-only mode
        char* message = "Exiting foreground-only mode\n";
        write(STDOUT_FILENO, message, 30);
        foreground_only_mode = 0;
    }
}


bool is_comment_or_blank(char* input){
    char *ptr = input;
    while (*ptr != '\0' && isspace(*ptr)) {
        ptr++;
    }
    if (*ptr == '\0' || *ptr == '\n' || *ptr == '#'){
        return true;
    }
    return false;
}
