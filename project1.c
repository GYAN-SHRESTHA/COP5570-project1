#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <stdbool.h>
#include<ctype.h>
#include <time.h>
#include<errno.h> 

extern int errno;       /* C library macro to indicate error */

/*
 * 
*/
typedef struct {
    int size;
    char **items;
} tokenlist;

char *get_input(void);
tokenlist *get_tokens(char *input);
tokenlist *new_tokenlist(void);
void add_token(tokenlist *tokens, char *item);
void free_tokens(tokenlist *tokens);


/*
 * Function to remove white spaces
*/
void trim(char *line)
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

void calculate_num_pipes(char** temp_command, int *num_pipes, char *buffer)
{
    char *token;
	token=strtok(buffer, "|");
	int i = -1;
	while (token) {
		temp_command[++i] = malloc(sizeof(token)+1);
		strcpy(temp_command[i], token);
        trim(temp_command[i]);
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
    printf("input: %s\n", buffer);
    calculate_num_pipes(temp_command, &num_pipes, buffer);
    int fd[3][2];
    int i=0;
    printf("Number of pipes in command: %d\n", num_pipes);
    
    for (i = 0; i < num_pipes+1; i++) {
        char *token;
	    token=strtok(temp_command[i], " ");
	    int j = -1;
        while (token) {
            command[++j] = malloc(sizeof(token)+1);
            strcpy(command[j], token);
            trim(command[j]);
            token = strtok(NULL, " ");
        }
        command[++j] = NULL;
        if(i != num_pipes){
            pipe(fd[i]);
		}
        pid_t pd = fork();
		if (pd == 0) {
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
			execvp(command[0], command);
            printf("invalid command\n");
			exit(1);
		}
		if (i != 0){
			close(fd[i-1][0]);
			close(fd[i-1][1]);
		}
		waitpid(pd, NULL, 0);
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
				fprintf(stderr, "%s\n", strerror(errno));
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
void execute_single_command(char **commands)
{
    pid_t pid = fork();

	if(pid==0){
        execvp(commands[0],commands);
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
    // executing single external command
    execute_single_command(commands);
    return 0;
}

int main()
{
    bool terminate = false;
    
    while (!terminate){
        char line[81];
        char *buffer = NULL;
        int bufsize = 0;
        printf("$ ");

        // Terminate if CTRL-D is encountered
        if(NULL==fgets(line, 81, stdin)) {
            printf("\n");
            terminate = 1;
            break;
        }

        buffer = (char *) malloc (strlen(line) + 1);
        strcpy(buffer, line);
        printf("buffer before: %s\n", buffer);
        trim(buffer);
        printf("buffer after: %s\n", buffer);

        tokenlist *tokens = get_tokens(buffer);
        if(tokens->items[0] == NULL) {  
            continue;                                     
        }
        for (int i =0; i < tokens->size; i++) {
            printf("%s\n", tokens->items[i]);
        }
        
        printf("number of tokens: %d\n", tokens->size);
		
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
                    if (chdir(tokens->items[1]) != 0)
                    {
                        if (errno == ENOTDIR)
                            printf("%s: Not a directory.\n", tokens->items[1]);
                        else if(errno == ENOENT)
                            printf("%s: No such file or directory.\n", tokens->items[1]);
                    }
                    else{
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
        else if (!strcmp(tokens->items[0], "mypwd")){
            char * pwd = getenv("PWD");
            printf("%s\n", pwd);
        }

        else {
            terminate = execute_commands(buffer, tokens);
        }
        printf("one cycle of command is complete\n");

        free(buffer);
        free_tokens(tokens);
    }

    return 0;
}

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
