#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <stdbool.h>
#include <ctype.h>
#include <time.h>
#include <errno.h> 
#include <sys/types.h>
#include <dirent.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <signal.h>
#include <sys/stat.h>

extern int errno;       /* C library macro to indicate error */

#define MAX_LINE_LENGTH 81  /*assuming each input line from the user will have no more than 80 characters */

/*
 * tokenlist contains the parsed tokens
*/
typedef struct {
    int size;
    char **items;
} tokenlist;

/*
 * global varaiables to handle process
*/
pid_t process_list[100];
int process_list_index = -1;

/*
 * Function declaration of functons used in parsing input string into tokens
*/
char *get_input(void);
tokenlist *get_tokens(char *input);
tokenlist *new_tokenlist(void);
void add_token(tokenlist *tokens, char *item);
void free_tokens(tokenlist *tokens);

/*
 * Function to remove white spaces
*/
void remove_space(char *line)
{
    int i;
    int start = 0;
    int end = strlen(line) - 1;

    while (isspace((char) line[start]))
        start++;

    while ((end >= start) && isspace((char) line[end]))
        end--;

    // Bringing characters back to the start 
    for (i = start; i <= end; i++) {
        line[i - start] = line[i];
    }
    line[i - start] = '\0'; 
}

/*
 * Function to split pipe commands
*/
void split_pipe_commands(char** temp_command, int *num_pipes, char *buffer)
{
    char *token;
	token=strtok(buffer, "|");
	int i = -1;
	while (token) {
		temp_command[++i] = malloc(sizeof(token)+1);
		strcpy(temp_command[i], token);
        remove_space(temp_command[i]);
		token = strtok(NULL, "|");
	}
    *num_pipes = i;
	temp_command[++i] = NULL;
}

/*
 * Function for Piping
 * e.g ls | sort -r
 * ls | sort -r | more
 * ls -la | sort -r | more
*/
void execute_piping(char* buffer)
{
    char *temp_command[100];
    char *command[100];
    int num_pipes = 0;
    split_pipe_commands(temp_command, &num_pipes, buffer);
    int fd[num_pipes][2];
    int i = 0;
    
    for (i = 0; i < num_pipes+1; i++) {
        char *token;
	    token=strtok(temp_command[i], " ");
	    int j = -1;
        while (token) {
            command[++j] = malloc(sizeof(token)+1);
            strcpy(command[j], token);
            remove_space(command[j]);
            token = strtok(NULL, " ");
        }
        command[++j] = NULL;
        if(i != num_pipes) {
            pipe(fd[i]);
		}
        pid_t pid = fork();
        if (pid == -1){
            printf("mytoolkit: error while creating child process\n");
            exit(1);
        }
		if (pid == 0) {
			if(i != num_pipes) {
				dup2(fd[i][1], STDOUT_FILENO);
				close(fd[i][0]);
				close(fd[i][1]);
			}
			if (i != 0) {
				dup2(fd[i-1][0], STDIN_FILENO);
				close(fd[i-1][1]);
				close(fd[i-1][0]);
			}
			if (execvp(command[0], command) == -1) {
                printf("mytoolkit: command execution failed\n");
                exit(1);
            }
		}
		if (i != 0){
			close(fd[i-1][0]);
			close(fd[i-1][1]);
		}
		waitpid(pid, NULL, 0);
    }
}

/*
 * Function for I/O redirection
 * parameter redirect_type is to decide type of redirection
 * redirect_type 0 : command1 > file1
 * redirect_type 1 : command1 < file1
 * redirect_type 2 : command1 < file1 > file2
*/ 
void input_output_redirection(char** commands, char* inputfile, char* outputfile, int redirect_type)
{
    int fd;   /* file_descriptor */
    pid_t pid = fork();
    if (pid == -1) {
        printf("mytoolkit: error while creating child process\n");
        exit(1);
    }

    if(pid == 0) {
        switch (redirect_type)
        {
        case 0: /* output redirection */
            fd = open(outputfile, O_CREAT | O_TRUNC | O_WRONLY, 0666); 
			close(STDOUT_FILENO);
			dup(fd);
			close(fd);
            break;

        case 1:  /* input redirection */
            fd = open(inputfile, O_RDONLY);  
			if (fd == -1) {   /* Error if file does not exist or is not a file */
                fprintf(stderr, "mytoolkit: ");
				exit(1);
			}
            close(STDIN_FILENO);
			dup(fd);
			close(fd);
            break;

        case 2: /* input-output redirection */
            fd = open(inputfile, O_RDONLY);  
			if (fd == -1){
				fprintf(stderr, "mytoolkit: ");
				fprintf(stderr, "%s\n", strerror(errno));
				exit(1);
			}
            close(STDIN_FILENO);
			dup(fd);
			close(fd);
			fd = open(outputfile, O_CREAT | O_TRUNC | O_WRONLY, 0666);
            close(STDOUT_FILENO);
			dup(fd);
			close(fd);	
            break;
        }
        execvp(commands[0], commands);	
        exit(1); 
	}
	waitpid(pid, NULL, 0);
}


/*
 * Function for executing existing external Unix commands
 * e.g ls, ps, ls -la, ps -ef
*/ 
void execute_external_command(char **commands)
{
    pid_t pid = fork();

    if (pid == -1){
        printf("mytoolkit: error while creating child process\n");
        exit(1);
    }

	if(pid == 0) {
        execvp(commands[0], commands);
        printf("mytoolkit: %s: command not found\n", commands[0]);
        exit(1);
	}
	waitpid(pid, NULL, 0);	
}

/*
 * General function for handling the entered commands
 * Calls seperate function for Piping, I/O redirection and external Unix commands
*/ 
int execute_commands(char *buffer, tokenlist *tokens)
{
    int i = 0;
	int j = 0;
	char *commands[100]; 
	
	// Looking for the special characters '>' and '< ' in commands and breaking the commands for I/O redirection
	while ( tokens->items[j] != NULL) {
		if ( (strcmp(tokens->items[j], ">") == 0) || (strcmp(tokens->items[j], "<") == 0)){
			break;
		}
		commands[j] = tokens->items[j];
		j++;
	}
    commands[j] = NULL;

    while (tokens->items[i] != NULL) {
        // Sub-routine for piping if character'|' is detected in input token
        if (strcmp(tokens->items[i], "|") == 0){
            if (tokens->items[i+1] != NULL) {
                execute_piping(buffer);
            }
            else
                printf("mytoolkit: insufficient arguments in command\n");

            return 0;
        }
        
        // Sub-routine for I/O Redirection
        else if (strcmp(tokens->items[i], "<") == 0){
            if (tokens->items[i+1] == NULL) {
                printf("mytoolkit: no inputfile provided\n");
                return 0;
            }
            else if (tokens->items[i+1] != NULL && tokens->items[i+2] == NULL) {
                input_output_redirection(commands,tokens->items[i+1], NULL, 1);
                return 0;
            }
            else if (tokens->items[i+1] == NULL || tokens->items[i+2] == NULL || tokens->items[i+3] == NULL ) {
                printf("mytoolkit: insufficient arguments in command\n");
                return 0;
            }
            else {
                if (strcmp(tokens->items[i+2], ">") != 0){
                printf("mytoolkit: expected character '>' not found %s\n", tokens->items[i+2]);
                return 0;
                }
            }
            input_output_redirection(commands, tokens->items[i+1], tokens->items[i+3], 2);
            return 0;
        }
        else if (strcmp(tokens->items[i], ">") == 0){
            if (tokens->items[i+1] == NULL){
                printf("mytoolkit: no outputile provided\n");
                return 0;
            }
            input_output_redirection(commands, NULL, tokens->items[i+1], 0);
            return 0;
        }
        i++;
    }
    // executing external command
    execute_external_command(commands);
    return 0;
}

/*
 * To align the printout properly in mytree uisng space
*/ 
void print_indent(int indent)
{
    printf("%*c", indent, ' ');
}

/*
 * To print the directories (and files) in a tree-like format
*/
void implement_mytree(char *dirpath, const int indent)
{
    char path[1000];
    struct dirent *files;
    DIR *dir = opendir(dirpath);

    if (!dir) {
        return;
    }

    while ((files = readdir(dir)) != NULL) {
        if (strcmp(files->d_name, ".") != 0 && strcmp(files->d_name, "..") != 0) {
            for (int i = 0; i < indent; i++) {
                print_indent(1);
            }
            print_indent(3);
            printf("%s\n", files->d_name);
            strcpy(path, dirpath);
            strcat(path, "/");
            strcat(path, files->d_name);
            implement_mytree(path, indent + 3);
        }
    }
    closedir(dir);
}

/*
 * To print last modification time of all the regular files in the directories
*/
void implement_mymtimes(char *dirpath)
{
    struct stat path_stat;
    char fullpath[1000];
    struct dirent *files;
    DIR *dir = opendir(dirpath);

    if (!dir) {
        return;
    }

    while ((files = readdir(dir)) != NULL) {
        if (strcmp(files->d_name, ".") != 0 && strcmp(files->d_name, "..") != 0) {
            strcpy (fullpath, dirpath);
            strcat (fullpath, "/");
            strcat (fullpath, files->d_name);
            if (stat(fullpath, &path_stat) == -1) {
                printf("File %s not there.\n", files->d_name);
            }
            if (S_ISREG(path_stat.st_mode)) {
                time_t last_change = path_stat.st_mtime;
                time_t now = time(0);
                double diff_seconds = difftime(now, last_change);
                if (diff_seconds <= 86400)
                {   
                    printf("%s", ctime(&path_stat.st_mtime));
                }
            }
            implement_mymtimes(fullpath);
        }
    }
    closedir(dir);
}

/*
 * To kill process of cmd by sending a TERM signal in mytimeout if it has not terminated 
 * mytimeout snds cmd 
*/
void alarm_handler(int signum)
{
    int temp_pid = process_list[process_list_index];
    kill(temp_pid, SIGTERM);
}

int main()
{
    bool terminate = false;
    while (!terminate){
        char line[MAX_LINE_LENGTH];
        char *buffer = NULL;
        printf("$ ");
        // signal handler for SIGALRM
        signal(SIGALRM, alarm_handler);
        memset(line, '\0', MAX_LINE_LENGTH );
        // Terminating when CTRL-D is encountered
        if(NULL == fgets(line, MAX_LINE_LENGTH, stdin)) {
            printf("\n");
            terminate = 1;
            break;
        }

        // Terminating when end-of-file is encountered.
		if(feof(stdin)) {
            printf("\n");
            terminate = 1;
            break;
		}

        buffer = (char *) malloc (strlen(line) + 1);
        strcpy(buffer, line);
        remove_space(buffer);

        tokenlist *tokens = get_tokens(buffer);

        // Handling condition so that enter key doesn't terminate the toolkit
        if(tokens->items[0] == NULL) {  
            continue;                                     
        }
		
        /*
         * Command: myexit
        */
		if(!strcmp(tokens->items[0], "myexit")){    
			printf("Exiting shell...\n");
            terminate = 1;
            break;
		}
		
        /*
         * Command: mycd
        */
		else if(!strcmp(tokens->items[0], "mycd")){
            if (tokens->items[1] == NULL || tokens->items[2] == NULL)
            {
                if (tokens->items[1] == NULL)
                {
                    chdir(getenv("HOME"));
                    char *cwd = getcwd(NULL, 0);
                    setenv("PWD", cwd, 1);
                    free(cwd);
                }
                else
                {
                    if (strstr(tokens->items[1], "/") != NULL) {
                        if (chdir(tokens->items[1]) != 0) {
                            if (errno == ENOTDIR)
                                printf("%s: Not a directory.\n", tokens->items[1]);
                            else if(errno == ENOENT)
                                printf("%s: No such file or directory.\n", tokens->items[1]);
                        }
                        else { 
                            char *cwd = getcwd(NULL, 0);
                            setenv("PWD", cwd, 1);
                            free(cwd);
                        }
                    }
                    else if (chdir(tokens->items[1]) != 0) {
                        if (errno == ENOTDIR)
                            printf("%s: Not a directory.\n", tokens->items[1]);
                        else if(errno == ENOENT)
                            printf("%s: No such file or directory.\n", tokens->items[1]);
                    }
                    else { 
                        char *cwd = getcwd(NULL, 0);
                        setenv("PWD", cwd, 1);
                        free(cwd);
                    }
                }
            }
            else
                printf("mycd: too few arguments\n");
        }
        
        /*
         * Command: mypwd
        */
        else if (!strcmp(tokens->items[0], "mypwd")) {
            char * pwd = getenv("PWD");
            printf("%s\n", pwd);
        }
        
        /*
         * Command: mytree
        */
        else if (!strcmp(tokens->items[0], "mytree")) {
            if (tokens->items[1] == NULL) {
                char * cop = (char *) malloc(strlen(getenv("PWD")) + 1) ;
		        strcpy(cop, getenv("PWD"));
                implement_mytree(cop, 0);
            }
            else {
                char * cop = (char *) malloc(strlen(getenv("PWD")) + strlen(tokens->items[1] + 1));
                strcpy(cop, getenv("PWD"));
                strcat(cop, "/"); 
                strcat(cop, tokens->items[1]);
                implement_mytree(cop, 0);   
            }
        }
        
        /*
         * Command: mytime
        */
        else if (!strcmp(tokens->items[0], "mytime")) {
            struct timeval start, end;
			struct rusage usage_start;
			struct rusage usage_end;
			gettimeofday(&start, NULL);
			getrusage(RUSAGE_CHILDREN, &usage_start);
            int i = 0;
            char *commands[100]; 
            while ( tokens->items[i] != NULL) {
                commands[i] = tokens->items[i+1];
                i++;
            }
            commands[i] = NULL;
            if (tokens->items[1] != NULL) { 
                execute_external_command(commands);
                gettimeofday(&end, NULL);
			    getrusage(RUSAGE_CHILDREN, &usage_end);
			    double wall_clock_time = ((end.tv_usec/1000) - (start.tv_usec/1000)) + ((end.tv_sec*1000) - (start.tv_sec*1000));
                double user_cpu_time = ((usage_end.ru_utime.tv_usec/1000) + (usage_end.ru_utime.tv_sec*1000)) - ((usage_start.ru_utime.tv_usec/1000) + (usage_start.ru_utime.tv_sec*1000));
                double system_cpu_time = ((usage_end.ru_stime.tv_usec/1000) + (usage_end.ru_stime.tv_sec*1000)) - ((usage_start.ru_stime.tv_usec/1000) + (usage_start.ru_stime.tv_sec*1000));
                printf("real\t%.4fms\n", wall_clock_time);
                printf("user\t%.4fms\n", user_cpu_time);
                printf("sys \t%.4fms\n", system_cpu_time);
            }
        }
        
        /*
         * Command: mymtimes
        */
        else if (!strcmp(tokens->items[0], "mymtimes")) {
            if (tokens->items[1] == NULL) {
                char * cop = (char *) malloc(strlen(getenv("PWD")) + 1) ;
		        strcpy(cop, getenv("PWD"));
                implement_mymtimes(cop);
            }
            else {
                char * cop = (char *) malloc(strlen(getenv("PWD")) + strlen(tokens->items[1] + 1));
                strcpy(cop, getenv("PWD"));
                strcat(cop, "/"); 
                strcat(cop, tokens->items[1]);
                implement_mymtimes(cop);     
            }
        }
        
        /*
         * Command: mytimeout
        */
        else if (!strcmp(tokens->items[0], "mytimeout")) {
            int i = 0;
            char *commands[100]; 
            // Seperating out cmd part from input tokens for execution
            while ( tokens->items[i] != NULL) {
                commands[i] = tokens->items[i+2];   /* mytimeout snds cmd: cmd to execute is cmd */
                i++;
            }
            commands[i] = NULL;
            process_list_index++;
            if (tokens->items[1] != NULL) { 
                pid_t pid = fork();
                if (pid == -1){
                    printf("mytoolkit: error while creating child process\n");
                    exit(1);
                }
                if (pid == 0) {
				    process_list[process_list_index] = getpid(); 
                    execvp(commands[0], commands);
                    printf("mytoolkit: %s: command not found\n", commands[0]);
                    exit(1);
                }
                process_list[process_list_index] = pid;
                // convert string to integer
	            int snds = (int)strtol(tokens->items[1], (char **)NULL, 10);
                alarm(snds);
                waitpid(pid, NULL, 0);
                process_list_index--;
                alarm(0);
            }
        }
    
        else {
            terminate = execute_commands(buffer, tokens);
        }

        free(buffer);
        free_tokens(tokens);
    }

    return 0;
} 

/*
 * Following four functions are used in parsing input string into tokens
*/
tokenlist *get_tokens(char *input)
{
    char *buf = (char *) malloc (strlen(input) + 1);
    strcpy(buf, input);

    tokenlist *tokens = new_tokenlist();

    char *tok = strtok(buf, " ");
    while (tok != NULL){
        add_token(tokens, tok);
        tok=strtok(NULL, " ");
    }

    free(buf);
    return tokens;
}

tokenlist *new_tokenlist(void)
{
    tokenlist *tokens = (tokenlist *) malloc(sizeof(tokenlist));
    tokens->size = 0;
    tokens->items = (char **) malloc(sizeof(char *));
    tokens->items[0] = NULL;
    return tokens;
}

void add_token(tokenlist *tokens, char *item)
{
    int i = tokens->size;

    tokens->items = (char **) realloc(tokens->items, (i + 2) * sizeof(char *));
    tokens->items[i] = (char *) malloc(strlen(item) + 1);
    tokens->items[i + 1] = NULL;
    strcpy(tokens->items[i], item);

    tokens->size += 1;
}

void free_tokens(tokenlist *tokens)
{
    for(int i = 0; i < tokens->size; i++)
        free(tokens->items[i]);
    free(tokens->items);
    free(tokens);
}
