/*****************************************************
 * Copyright Grégory Mounié 2008-2015                *
 *           Simon Nieuviarts 2002-2009              *
 * This code is distributed under the GLPv3 licence. *
 * Ce code est distribué sous la licence GPLv3+.     *
 *****************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <assert.h>
#include <sys/time.h>
//open & creat
#include <fcntl.h>
//close
#include <unistd.h>
#include "variante.h"
#include "readcmd.h"

#ifndef VARIANTE
#error "Variante non défini !!"
#endif

/* Guile (1.8 and 2.0) is auto-detected by cmake */
/* To disable Scheme interpreter (Guile support), comment the
 * following lines.  You may also have to comment related pkg-config
 * lines in CMakeLists.txt.
 */

#if USE_GUILE == 1
#include <libguile.h>

typedef struct list_pid list_pid;
struct list_pid{
  pid_t n_pid;
  char *com;
  int start;
  struct list_pid *suiv;
};

bool tache_de_fond = false;
bool input = false;
bool output = false;
char *input_file;
char *output_file;
list_pid *liste = NULL;

void ajouter_liste_queue(list_pid **liste, pid_t pid, char *command,int start){
  struct list_pid sent = {0,NULL, 0, *liste};
  struct list_pid *queue = &sent;
  while(queue -> suiv != NULL){
    queue = queue -> suiv;
  }
  queue -> suiv = malloc(sizeof(struct list_pid));
  assert(queue -> suiv != NULL);
  queue -> suiv -> n_pid = pid;
  queue -> suiv -> start = start;
  queue -> suiv -> com = calloc(strlen(command), sizeof(char));
  strcpy(queue -> suiv -> com, command);
  queue -> suiv -> suiv = NULL;
  *liste = sent.suiv;
}

void free_list(list_pid *list){
  struct list_pid *p, *suiv;
  p = list;
  while(p != NULL){
    suiv = p -> suiv;
    free(p -> com);
    free(p);
    p = suiv;
  }
}

void supprimer(struct list_pid **list, pid_t pid){
  struct timeval end;
  struct list_pid sent = { 0, NULL, 0, *list };
  struct list_pid *p = &sent;
  while(p->suiv != NULL && p->suiv->n_pid != pid){
    p = p->suiv;
  }
  
  if(p->suiv != NULL){
    struct list_pid *remove = p->suiv;
    p->suiv = remove->suiv;
    gettimeofday(&end, NULL);
    printf("Process '%s' of pid '%d' ended - Duration %d s\n",remove -> com, remove -> n_pid, (int)end.tv_sec - remove ->start);
    free(remove -> com);
    free(remove);
  }
  *list = sent.suiv;
}

void my_sigchld_handler(int sig){
  pid_t p;
  int status;

  if ((p=waitpid(-1, &status, WNOHANG)) != -1){
    supprimer(&liste,p);
  }
}

void jobs(){
  list_pid *p = liste;
  printf("Background Process\npid\tcommande\n");
  while(p != NULL){
    printf("%d\t%s\n",p -> n_pid, p -> com);
    p = p -> suiv;
  }
}

void  execute(char **argv)
{
  pid_t  pid;
  int fd0, fd1;
  int    status;
  struct timeval time;
  
  if ((pid = fork()) < 0) {     /* fork a child process           */
    printf("*** ERROR: forking child process failed\n");
    exit(1);
  }
  else if (pid == 0) {          /* for the child process:         */
    if (input) { //if '<' char was found in string inputted by user
      fd0 = open(input_file, O_RDONLY, 0);
      dup2(fd0, STDIN_FILENO);
      close(fd0);
      input = false;
    }

    if (output) { //if '>' was found in string inputted by user
      fd1 = creat(output_file,0666);
      dup2(fd1, STDOUT_FILENO);
      close(fd1);
      output = false;
    }

    if(strcmp(argv[0],"jobs") == 0){
      jobs();
      exit(1);
    }else{
      execvp(argv[0], argv);
    }
  }
  else {                                  /* for the parent:      */
    if(!tache_de_fond){
      while (wait(&status) != pid);      /* wait for completion  */
    }else{
      gettimeofday(&time,NULL);
      ajouter_liste_queue(&liste,pid,argv[0],(int)time.tv_sec);
    }
  }
}

void execpipe (char ** argv1, char ** argv2) {
  int fd0,fd1;
  int fds[2];
  int status;
  pid_t pid = fork();
  
  if (pid == -1) { //error
    printf("*** ERROR: forking child process failed\n");
    exit(1);
  } 
  if (pid == 0) { // child process  
    pipe(fds);
    pid = fork();
    if(pid == -1){
      printf("*** ERROR: forking another child process failed\n");
      exit(1);
    }
    if(pid == 0){
      if (input) { //if '<' char was found in string inputted by user
	fd0 = open(input_file, O_RDONLY, 0);
	dup2(fd0, STDIN_FILENO);
	close(fd0);
	input = false;
      }
      dup2(fds[1], 1);
      close(fds[0]);close(fds[1]);
      execvp(argv1[0], argv1); 
    }else{   
      if (output) { //if '>' was found in string inputted by user
	fd1 = creat(output_file,0666);
	dup2(fd1, STDOUT_FILENO);
	close(fd1);
	output = false;
      } 
      dup2(fds[0], 0);
      close(fds[1]); close(fds[0]);
      execvp(argv2[0], argv2);
    }
  }else { 
    while(wait(&status) != pid);
  }
}

void execmultipipe(struct cmdline *l){
  int fds[2];
  int fd_in = 0;
  int status;
  int i = 0;
  pid_t pid = fork();
  
  if(pid < 0){
    printf("Error\n");
    exit(1);
  }
  if(pid == 0){
    while(l->seq[i] != 0){
      char **cmd = l->seq[i];
      pipe(fds);
      pid = fork();
      if (pid == -1){
	exit(EXIT_FAILURE);
      }
      else if (pid == 0){
	dup2(fd_in, 0); //change the input according to the old one 
	if (l->seq[i+1] != 0){
	  dup2(fds[1], 1);
	}
	close(fds[0]);
	execvp(cmd[0], cmd);
	exit(EXIT_FAILURE);
      }else{
	while(wait(&status) != pid);
	close(fds[1]);
	fd_in = fds[0]; //save the input for the next command
	i++;
      }
    }
  }else{
    while(wait(&status) != pid);
  }
}

int question6_executer(char *line)
{
  /* Question 6: Insert your code to execute the command line
   * identically to the standard execution scheme:
   * parsecmd, then fork+execvp, for a single command.
   * pipe and i/o redirection are not required.
   */
  struct cmdline *l;

  //printf("Not implemented yet: can not execute %s\n", line);
  l = parsecmd( & line);

  /* Display each command of the pipe */
  for (int i=0; l->seq[i]!=0; i++) {
    char **cmd = l->seq[i];
    printf("seq[%d]: ", i);
    for (int j=0; cmd[j]!=0; j++) {
      printf("'%s' ", cmd[j]);
    }
    execute(l->seq[0]);
    printf("\n");
    printf("\n");
  }

  /* Remove this line when using parsecmd as it will free it */
  //free(line);

  return 0;
}

SCM executer_wrapper(SCM x)
{
  return scm_from_int(question6_executer(scm_to_locale_stringn(x, 0)));
}
#endif


void terminate(char *line) {
#if USE_GNU_READLINE == 1
  /* rl_clear_history() does not exist yet in centOS 6 */
  clear_history();
#endif
  if (line)
    free(line);

  free_list(liste);
  free(input_file);
  free(output_file);
  printf("exit\n");
  exit(0);
}

int main() {

  /* Establish handler. */
  struct sigaction sa;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sa.sa_handler = my_sigchld_handler;
  sigaction(SIGCHLD, &sa, NULL);

  printf("Variante %d: %s\n", VARIANTE, VARIANTE_STRING);

#if USE_GUILE == 1
  scm_init_guile();
  /* register "executer" function in scheme */
  scm_c_define_gsubr("executer", 1, 0, 0, executer_wrapper);
#endif

  while (1) {
    struct cmdline *l;
    char *line=0;
    int i, j;
    char *prompt = "ensishell>";
    /* Readline use some internal memory structure that
       can not be cleaned at the end of the program. Thus
       one memory leak per command seems unavoidable yet */
    line = readline(prompt);
    if (line == 0 || ! strncmp(line,"exit", 4)) {
      terminate(line);
    }
    /* implémentation des autres cmd */

#if USE_GNU_READLINE == 1
    add_history(line);
#endif


#if USE_GUILE == 1
    /* The line is a scheme command */
    if (line[0] == '(') {
      char catchligne[strlen(line) + 256];
      sprintf(catchligne, "(catch #t (lambda () %s) (lambda (key . parameters) (display \"mauvaise expression/bug en scheme\n\")))", line);
      scm_eval_string(scm_from_locale_string(catchligne));
      free(line);
      continue;
    }
#endif

    /* parsecmd free line and set it up to 0 */
    l = parsecmd( & line);

    /* If input stream closed, normal termination */
    if (!l) {
      terminate(0);
    }

    if (l->err) {
      /* Syntax error, read another command */
      printf("error: %s\n", l->err);
      continue;
    }

    if (l->in){
      // printf("in: %s\n", l->in);
      input_file = l->in;
      input = true;
    }else{
      input = false;
    }

    if (l->out){
      // printf("out: %s\n", l->out);      
      output_file = l->out;
      output = true;
    }else{
      output = false;
    }

    //TACHE DE FOND
    if (l->bg) {
      printf("background (&)\n");
      tache_de_fond = true;
    }else{
      tache_de_fond = false;
    }

    /* Display each command of the pipe */
    for (i=0; l->seq[i]!=0; i++) {
        char **cmd = l->seq[i];
      // printf("seq[%d]: ", i);
      for (j=0; cmd[j]!=0; j++) {
	//	printf("'%s' ", cmd[j]);
      }
      // printf("\n");
      //  printf("\n");
      }

    //Ca marche avec 1 seul pipe
    if(i == 2){
      execpipe(l->seq[0],l->seq[1]);
    }else if(i == 1){
      execute(l->seq[0]);
    }else{
      execmultipipe(l);
    }
  }
}
