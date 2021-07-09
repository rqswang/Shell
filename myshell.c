#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h> 
#include <sys/wait.h> 
#include <assert.h>
#include <sys/stat.h> 
#include <fcntl.h>

#define MAX_LINE_LEN 514 // maximum line length including the '\n' and the '\0'

/* print string to standard output */
void myPrint(char *msg)
{
    write(STDOUT_FILENO, msg, strlen(msg));
}

/* print error message to standard output */
void error_msg()
{
    char error_message[30] = "An error has occurred\n";
    write(STDOUT_FILENO, error_message, strlen(error_message));
}

/* count the number of arguments in a job
* job: the job whose arguments are to be counted
* return: the number of arguments in the job */
int count_args(char job[])
{
    char* rest = job + sizeof(char);
   
    /* empty job */
    if (!job[0]) {
        return 0;
    }
   
    if (job[0] != ' ' && job[0] != '\t') {
        if ((job[1] == ' ' || job[1] == '\t' || job[1] == '>') || (!job[1])) {
            return 1 + count_args(rest);
        }
    }
   
    return count_args(rest);
}

/* helper function to measure_args
* given a string, measure the length of the first substring in it that does not
* contain whitespaces
* string: the string to be examined
* return: the length of the first substring that contains no whitespace */
int measure_arg(char* string)
{
    int length = 0;
    for (int i = 0; i < strlen(string); i++) {
        if (string[i] != ' ' && string[i] != '\t') {
            length++;
        }
        else {
            break;
        }
    }
   
    return length;
}

/* measure the string lengths of each of the arguments contained in a job
* job: the job whose arguments are to be measured
* return: an array each entry of whose contains the string lengths of each of
* the arguments in the job */
int* measure_args(char job[])
{
    int num_args = count_args(job);
    int* len_args = (int*)malloc(sizeof(int) * num_args);
    for (int j = 0; j < num_args; j++) {
        len_args[j] = 0;
    }
   
    int j = 0;
    for (int i = 0; i < strlen(job); i++) {
        if (job[i] != ' ' && job[i] != '\t') {
            len_args[j] = measure_arg(job + i * sizeof(char));

            i = i + measure_arg(job + i * sizeof(char)) - 1;
            if (j < num_args - 1) {
                j++;
            }
        }
    }
   
    return len_args;
}

/* store the arguments in a job into an array of strings
* job: the job to be parsed
* return: an array of strings containing the arguments in the job */
char** parse_job(char* job)
{
    int num_args = count_args(job);
    int* len_args = measure_args(job);
   
    char* start = job;
    char* rest;
   
    char s[3];
    s[0] = ' ';
    s[1] = '\t';
    s[2] = '\0';
   
    char** args = (char**)malloc(sizeof(char*) * (num_args + 1));
    for (int i = 0; i < num_args; i++) {
        args[i] = strtok_r(start, s, &rest);
        start = rest;
    }
    args[num_args] = NULL;
   
    /* free len_args array */
    free(len_args);
   
    return args;
}

/* search in a job for redirection
* job: the job to be searched
* return: 1 if there is one '>' sign, 0 if there is none, and -1 if there are
* multiple */
int is_redirection(char* job)
{
    int count = 0;
    for (int i = 0; i < strlen(job); i++) {
        if (job[i] == '>') {
            count++;
        }
    }
   
    if (count == 0) {
        return 0;
    }
    if (count == 1) {
        return 1;
    }
    if (count > 1) {
        return -1;
    }

    return -2;
}

void parse_redirection(char* job, char** instr_ptr, char** dest_ptr)
{
    assert(is_redirection(job) > 0);
   
    char r[2];
    r[0] = '>';
    r[1] = '\0';
   
    *instr_ptr = strtok_r(job, r, dest_ptr);
}

/* given a job, performs redirection if the job contains one redirection
* job: the job which contains the redirection
* return: the instruction part of the job (before the redirection), NULL if
* redirection failed */
char* redirect(char* job)
{
    int redir = 0;
    char* instr;
    char* path_temp; // used to store path with whitespaces
    char* path;
    int fd;
   
    redir = is_redirection(job);
        /* if the job contains more than one redirections */
        if (redir < 0) {
            error_msg();
            return NULL;
        }
       
        /* if the job does not contain redirection */
        else if (redir == 0) {
            instr = job;
            return instr;
        }
       
        /* if the job contains only one redirection */
        else if (redir > 0) {
            parse_redirection(job, &instr, &path_temp);

            if (job == NULL || path_temp[0] == '\0') {
                error_msg();
                return NULL;
            }
           
            /* if the job contains multiple paths */
            if (count_args(path_temp) != 1) {
                error_msg();
                return NULL;
            }
            else {
                char s[3];
                s[0] = ' ';
                s[1] = '\t';
                s[2] = '\0';
                path = strtok(path_temp, s);
               
                fd = open(path, O_WRONLY | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR
                          | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
                if (fd < 0) {
                    error_msg();
                    return NULL;
                }

                dup2(fd, STDOUT_FILENO);
               
                return instr;
            }
        }

    return NULL;
}

/* process a job that contains a built-in command
* args: the string array that contains the arguments of the job
* num_args: the number of arguments that the job contains
* return: 0 if the process does not contain a built-in command, 1 if it does */
int process_built_in(char** args, int num_args)
{
    /* non-built-in commands */
    if (strcmp(args[0], "exit") && strcmp(args[0], "pwd") && strcmp(args[0], "cd")) {
        return 0;
    }

    /* built-in with redirection */
    for (int i = 0; i < num_args; i++) {
        if (is_redirection(args[i]) != 0) {
            error_msg();
            return -1;
        }
    }
   
    /* variables used for pwd and cd */
    char pwd[1000];
    char* getcwd_status;
    char* path;
    int chdir_status;
   
    if (!strcmp(args[0], "exit")) {
        if (num_args != 1) {
            error_msg();
        }
        else {
            exit(0);
        }
    }

    if (!strcmp(args[0], "pwd")) {
        if (num_args != 1) {
            error_msg();
        }
        else {
            getcwd_status = getcwd(pwd, sizeof(pwd));
            if (getcwd_status == NULL) {
                error_msg();
            }
            else {
                myPrint(pwd);
                myPrint("\n");
            }
        }
    }

    if (!strcmp(args[0], "cd")) {
        if (num_args == 2) {
            path = args[1];
            chdir_status = chdir(path);
            if (chdir_status < 0) {
                error_msg();
            }
        }
        if (num_args == 1) {
            chdir_status = chdir("/");
            if (chdir_status < 0) {
                error_msg();
            }
        }
        else if (num_args > 2) {
            error_msg();
        }
    }
   
    return 1;
}

/* process a command line by executing the jobs in it in order
* cmd_buff: string containing the command line */
void process_line(char* cmd_buff)
{
    /* variables used for strtok */
    char* job;
    char* job_temp; // a copy of job to use for strtok
    char* instr;
    char* start = cmd_buff;
    char sm[2];
    sm[0] = ';';
    sm[1] = '\0';
    // sm[2] = '\0';
    char* rest;

    /* variables used for parsing a particular job */
    char** args;
    int num_args = 0;

    /* variables used for child processes */
    int fork_ret;
    int child_status;
    int execvp_ret;

    /* going through command line */
    while ((job = strtok_r(start, sm, &rest)) != NULL) {
        job_temp = (char*)malloc(sizeof(char) * (strlen(job) + 1));
        job_temp = strcpy(job_temp, job);

        num_args = count_args(job_temp);
        args = parse_job(job_temp);

        /* blank job */
        if (num_args) {
            /* non-built-in commands */
            if (!process_built_in(args, num_args)) {
                fork_ret = fork();
                if (fork_ret < 0) {
                    error_msg();
                }

                /* child process */
                if (fork_ret == 0) {
                    /* perform redirection if needed */
                    instr = redirect(job);
                    if (instr != NULL) {
                        num_args = count_args(instr);
                        args = parse_job(instr);

                        execvp_ret = execvp(args[0], args);
                        // check return value, and if fails, need to terminate
                        if (execvp_ret < 0) {
                            error_msg();
                            exit(0);
                        }
                    }
                    exit(0);
                }

                /* parent process */
                else {
                    waitpid(fork_ret, &child_status, 0);

                    if (WIFEXITED(child_status)) { // if child exited normally
                        /* free malloc'ed variables */
                        free(args);
                        free(job_temp);
                    }
                    else { // if the child did not exit normally
                        error_msg();
                        break;
                    }
                }
            }
        }
        start = rest;
    }
}

/* remove the '\n' character from the end of a string
* string: the string to be changed */
void remove_n(char* string)
{
    for (int i = 0; i < strlen(string); i++) {
        if (string[i] == '\n') {
            string[i] = '\0';
            break;
        }
    }
}

/* check to see if a string contains white spaces only
* string: the string to be examined
* length: the length of said string
* return: 1 if the string contains only white space, 0 if not */
int is_white_space(char* string, int length)
{
    for (int i = 0; i < length; i++) {
        if (string[i] != ' ' && string[i] != '\t') {
            return 0;
        }
    }

    return 1;
}

/* process a batch file line by line
* fd: the file descriptor of the batch file */
void process_batch_file(int fd)
{
    char cmd_buff[1000000];
    char one_char[1];
    int i = 0;

    /* reading the file line into the cmd_buff line by line */
    while (read(fd, one_char, 1)) {
        cmd_buff[i] = one_char[0];

        /* when reaching the end of the line */
        if (cmd_buff[i] == '\n') {
            cmd_buff[i] = '\0';

            /* if the line is not blank */
            if (!is_white_space(cmd_buff, strlen(cmd_buff)) && i != 0) {
                write(STDOUT_FILENO, cmd_buff, strlen(cmd_buff));
                write(STDOUT_FILENO, "\n", 1);

                /* if the line is no more than 512 characters */
                if (strlen(cmd_buff) < MAX_LINE_LEN - 1) { // < 513
                    process_line(cmd_buff);
                }
                /* if the line is too long */
                else {
                    error_msg();
                }
            }

            /* resetting cmd_buff */
            for (int j = 0; j < i; j++) {
                cmd_buff[j] = '\0';
            }
            i = 0;
        }
        else {
            i++;
        }
    }    
    exit(0);
}


// int main()
// {
//     char line[1000] = "[./batch-files/gooduser_basic]\n";
//     remove_n(line);
//     int fd;
//     int batch_status = is_and_open_batch(line, &fd);

//     if (batch_status > 0) {
//         process_batch_file(fd);
//     }
//     // printf("Fish is delicious\n");
//     // int num_args = count_args(job);
//     // printf("num_args: %d\n", num_args);

//     // char** args = parse_job(job);

//     // for (int i = 0; i < num_args + 1; i++) {
//     //     printf("args[%d]: %s\n", i, args[i]);
//     // }
//     // printf("is the last element NULL? %d\n", args[num_args] == NULL);
// }

int main(int argc, char *argv[])
{
    char cmd_buff[MAX_LINE_LEN + 1];
    char *pinput;
    int fd;

    /* interactive mode */
    if (argc == 1) {
        while (1) {
            myPrint("myshell> ");
            pinput = fgets(cmd_buff, MAX_LINE_LEN + 1, stdin);
            if (!pinput) {
                exit(0);
            }
            myPrint(cmd_buff);
           
            /* if the command line is not too long */
            if (strlen(cmd_buff) < MAX_LINE_LEN - 1) {
                remove_n(cmd_buff);
                process_line(cmd_buff);
            }

            /* if the command line is too long */
            else
            {
                error_msg();
            }
        }
    }

    /* batch mode */
    else if (argc == 2) {
        fd = open(argv[1], O_RDONLY);
        if (fd < 0) {
            error_msg();
            exit(0);
        }
        process_batch_file(fd);
    }

    else {
       
        error_msg();
        exit(0);
    }
}
