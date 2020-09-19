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
void ttop();


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
	    // 입력 파일이 있는 경우
		fp = fopen(argv[1], "r");

		// 파일이 없으면 에러
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
		if (strlen(line) == 0)
			continue;

		line[strlen(line)] = '\n'; //terminate with new line
		tokens = tokenize(line);

		// 명령어 처리
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

		// 현재 명령어 뒤에 파이프가 있음
		if (p != NULL) {
		    // 파이프 생성
			if (pipe(pipe_fd) < 0) {
				fprintf(stderr, "pipe error\n");
				exit(1);
			}

			// 현재 프로세스가 파이프 출력을 하도록 지정
			output_pipe_fd = pipe_fd[1];
			*p = NULL;
			cur_pipe = 1;
		}
		// ttop 명령어
		if (!strcmp(tokens[0], "ttop")) {
			sprintf(tokens[0], "./ttop");
		}
		
		// pps 명령어
		else if (!strcmp(tokens[0], "pps")) {
			sprintf(tokens[0], "./pps");
		}

		// 명령어 실행
		run_op(tokens[0], tokens, prev_pipe, cur_pipe, input_pipe_fd, output_pipe_fd);

		// ------------ 여기서부터 다음 명령어로 취급 ---------------- 즉. 윗줄에서의 "현재 명령어"는 "이전 명령어"로 명명됨
		// 이전 명령어가 파이프를 가졌음을 표시. 이전 명령어가 파이프를 가졌으면 현재 명령어의 입력이 파이프가 되어야 하기 때문
		prev_pipe = cur_pipe;
		cur_pipe = 0;

		// 다 사용한 파이프 파일은 닫아줌
		if (input_pipe_fd != -1) {
			close(input_pipe_fd);
			input_pipe_fd = -1;
		}
		
		if (output_pipe_fd != -1) {
			close(output_pipe_fd);
			output_pipe_fd = -1;
		}

		// 남은 명령어가 없으면 종료
		if (p == NULL)
			break;

		input_pipe_fd = pipe_fd[0];
		tokens = ++p;
	}
	
	return;
}

/**
 * 파이프를 가졌는지 확인하는 함수
 * @param tokens 명령어 토큰
 * @return 가졌다면 토큰의 주소를 리턴, 없다면 NULL을 리턴
 */
char **has_pipe(char **tokens) {
	while (*tokens != NULL) {
		if (!strcmp(*tokens, "|"))
			return tokens;
		tokens++;
	}
	return NULL;
}

/**
 * 명령어를 실행하는 함수
 * @param op 명령어
 * @param params 명령어에 전달될 아규먼트
 * @param input_redirection 파이프 입력 재지정 여부 (자식 프로세스 기준)
 * @param output_redirection 파이프 출력 재지정 여부 (자식 프로세스 기준)
 * @param input_pipe_fd 재지정할 입력 파이프 file descriptor
 * @param output_pipe_fd 재지정할 출력 파이프 file descriptor
 */
void run_op(const char *op, char **params, int input_redirection, int output_redirection, int input_pipe_fd, int output_pipe_fd) {
	int status;

	// 프로세스 생성
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
		    // 출력 파이프 지정
			dup2(output_pipe_fd, 1);
		}

		// 실행
		status = execvp(op, params);
		if (status < 0) {
			fprintf(stderr, "SSUShell : Incorrect command\n");
		}
		exit(1);
	}

	// 부모 프로세스
	wait(&status);

	if (!WIFEXITED(status))
		printf("SSUShell : Incorrect command\n");
}
