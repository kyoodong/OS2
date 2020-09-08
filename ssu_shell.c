#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wait.h>
#include <sys/types.h>
#include <sys/stat.h>

#define MAX_INPUT_SIZE 1024
#define MAX_TOKEN_SIZE 64
#define MAX_NUM_TOKENS 64

void process(char **tokens);
void run_op(const char *op, char **params);

/* Splits the string by space and returns the array of tokens
*
*/
char **tokenize(char *line)
{
  char **tokens = (char **)malloc(MAX_NUM_TOKENS * sizeof(char *));
  char *token = (char *)malloc(MAX_TOKEN_SIZE * sizeof(char));
  int i, tokenIndex = 0, tokenNo = 0;

  for(i =0; i < strlen(line); i++){

    char readChar = line[i];

    if (readChar == ' ' || readChar == '\n' || readChar == '\t'){
      token[tokenIndex] = '\0';
      if (tokenIndex != 0){
		  tokens[tokenNo] = (char*)malloc(MAX_TOKEN_SIZE*sizeof(char));
		  strcpy(tokens[tokenNo++], token);
		  tokenIndex = 0; 
      }
    } else {
      token[tokenIndex++] = readChar;
    }
  }
 
  free(token);
  tokens[tokenNo] = NULL ;
  return tokens;
}


int main(int argc, char* argv[]) {
	char  line[MAX_INPUT_SIZE];            
	char  **tokens;              
	int i;

	FILE* fp;
	if(argc == 2) {
		fp = fopen(argv[1],"r");
		if(fp < 0) {
			printf("File doesn't exists.");
			return -1;
		}
	}

	while(1) {
		/* BEGIN: TAKING INPUT */
		bzero(line, sizeof(line));
		if(argc == 2) { // batch mode
			if(fgets(line, sizeof(line), fp) == NULL) { // file reading finished
				break;	
			}
			line[strlen(line) - 1] = '\0';
		} else { // interactive mode
			printf("$ ");
			scanf("%[^\n]", line);
			getchar();
		}
		//printf("Command entered: %s (remove this debug output later)\n", line);
		/* END: TAKING INPUT */

		line[strlen(line)] = '\n'; //terminate with new line
		tokens = tokenize(line);
   
       //do whatever you want with the commands, here we just print them

		//for(i=0;tokens[i]!=NULL;i++){
		//	printf("found token %s (remove this debug output later)\n", tokens[i]);
		//}


		process(tokens);


		// Freeing the allocated memory	
		for(i=0;tokens[i]!=NULL;i++){
			free(tokens[i]);
		}
		free(tokens);

	}
	return 0;
}

void process(char **tokens) {
	struct stat sb;
	
	// ttop 명령어
	if (!strcmp(tokens[0], "ttop")) {

		return;
	}

	// pps 명령어
	if (!strcmp(tokens[0], "pps")) {

		return;
	}

	// 명령어가 존재하는 경우
	if (stat(tokens[0], &sb) == 0) {
		run_op(tokens[0], tokens);
		return;
	}
	char buffer[MAX_INPUT_SIZE];
	sprintf(buffer, "/bin/%s", tokens[0]);

	// /bin에 명령어가 존재하는 경우
	// Linux 내장 명령어
	if (stat(buffer, &sb) == 0) {
		run_op(buffer, tokens);
		return;
	}
	
}


void ttop() {

}

void pps() {

}

void run_op(const char *op, char **params) {
	char buffer[MAX_INPUT_SIZE];
	
	pid_t pid = fork();

	// fork 에러
	if (pid == -1) {
		fprintf(stderr, "fork 에러 발생\n");
		exit(1);
	}

	// 자식 프로세스
	if (pid == 0) {
		execv(op, params);
	}
	// 부모 프로세스
	int status;
	wait(&status);
       
}
