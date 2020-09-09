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
void run_op(const char *op, char **params, int input_redirection, int output_redirection, int input_pipe_fd, int output_pipe_fd);
char **has_pipe(char **tokens);




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
		process(tokens);


		// Freeing the allocated memory	
		for(i=0;tokens[i]!=NULL;i++){
			free(tokens[i]);
		}
		free(tokens);

	}
	return 0;
}

/**
  * 명령어를 처리하는 함수
  * @param tokens 명령어 토큰
  */
void process(char **tokens) {
	struct stat sb;
	int param_size;
	char **p = tokens;
	// 파이프 파일 디스크립터
	int pipe_fd[2];

	// 이 전 명령어에 파이프가 사용되었는지 여부
	int prev_pipe = 0;

	// 현재 명령어에 파이프가 사용되어야 하는지 여부
	int cur_pipe = 0;

	int input_pipe_fd = -1, output_pipe_fd = -1;

	while (1) {
		p = has_pipe(tokens);

		// 파이프가 있음
		if (p != NULL) {
			if (pipe(pipe_fd) < 0) {
				fprintf(stderr, "pipe error\n");
				exit(1);
			}

			output_pipe_fd = pipe_fd[1];
			*p = NULL;
			cur_pipe = 1;
		}

		//char buffer[MAX_INPUT_SIZE];
		//sprintf(buffer, "/bin/%s", tokens[0]);
	
		// /bin에 명령어가 존재하는 경우
		// Linux 내장 명령어
		//if (stat(buffer, &sb) == 0) {
		//	run_op(tokens[0], tokens, prev_pipe, cur_pipe, input_pipe_fd, output_pipe_fd);
		//} else {
		run_op(tokens[0], tokens, prev_pipe, cur_pipe, input_pipe_fd, output_pipe_fd);
		//}

		prev_pipe = cur_pipe;
		cur_pipe = 0;

		if (input_pipe_fd != -1) {
			close(input_pipe_fd);
			input_pipe_fd = -1;
		}
		
		if (output_pipe_fd != -1) {
			close(output_pipe_fd);
			output_pipe_fd = -1;
		}
		
		if (p == NULL)
			break;

		input_pipe_fd = pipe_fd[0];
		tokens = ++p;
	}
	
	// ttop 명령어
	if (!strcmp(tokens[0], "ttop")) {

		return;
	}

	// pps 명령어
	if (!strcmp(tokens[0], "pps")) {

		return;
	}

	return;
}

char **has_pipe(char **tokens) {
	while (*tokens != NULL) {
		if (!strcmp(*tokens, "|"))
			return tokens;
		tokens++;
	}
	return NULL;
}


void ttop() {

}

void pps() {

}

void run_op(const char *op, char **params, int input_redirection, int output_redirection, int input_pipe_fd, int output_pipe_fd) {
	int background_flag;
	char buffer[MAX_INPUT_SIZE];
	int status;
	
	pid_t pid = fork();

	// fork 에러
	if (pid == -1) {
		fprintf(stderr, "fork 에러 발생\n");
		exit(1);
	}

	// 자식 프로세스
	if (pid == 0) {
		// 이전 명령어에 파이프가 있었음
		if (input_redirection) {
			// 입력에 파이프 등록
			dup2(input_pipe_fd, 0);
		}

		if (output_redirection) {
			dup2(output_pipe_fd, 1);
		}
		status = execvp(op, params);
		if (status < 0) {
			fprintf(stderr, "SSUShell : Incorrect command\n");
		}
		exit(1);
	}

	// 부모 프로세스
	wait(&status);

	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
		printf("SSUShell : Incorrect command\n");
}
