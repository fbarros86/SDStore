#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/stat.h>


//verificar se a string começa por concluded
int isConcluded(char *s){
  s[9]='\0';
  return !strcmp(s,"concluded");
}

int error(char *s){
  s[5]='\0';
  return !strcmp(s,"Erro:");
}


int main(int argc, char const *argv[]) {
  int fdPedido = open("pedido",O_WRONLY);
  char *pedido=NULL;
  //o nome do pipe (servidor-> cliente) vai corresponder ao pid do processo do cliente
  char *resposta = malloc(20);
  sprintf(resposta,"%d",getpid());
  //se for status põe um s no inicio, seguido do nome do pipe (servidor-> cliente)
  if (argc == 2 && !strcmp(argv[1],"status")) {
    pedido=malloc(25);
    strcpy(pedido,"s");
    strcat(pedido,resposta);
    strcat(pedido,"\n");
  }
  else if (argc < 5 || strcmp(argv[1],"proc-file")){
    perror("Falta de argumentos");
    return 1;
  }
  //se for proc-file
  // põe a prioridade no inicio, seguido do nome do pipe (servidor-> cliente)
  // seguido dos restantes argumentos (ficheiros de entrada, saida e operações)
  else {
    int i=2;
    pedido = malloc(2048);
    // verficar a prioridade (se não tiver põe 0)
    if (!strcmp(argv[2],"-p")) {
      if ((strlen(argv[3]))==1 && argv[3][0]>='0'&&argv[3][0]<='5')
        strcpy(pedido,argv[3]);
      else {
        perror("Prioridade inválida");
        return 1;
      }
      i=4;
    }
    else strcpy(pedido,"0");
    strcat(pedido,resposta);
    strcat(pedido," ");
    for (;i<argc; i++) {
      strcat(pedido,argv[i]);
      strcat(pedido," ");
    }
    strcat(pedido,"\n");
  }
  //enviar o pedido e esperar pela resposta do servidor
  if (pedido){
    // criar pipe(servidor->cliente)
    if ((mkfifo(resposta, 0666))== -1){
      perror("mkfifo");
      return 1;
    }
  //  printf("PID:%s\n", resposta);
    //escrever pedido
    write(fdPedido,pedido,strlen(pedido));
    int bytes_read,fdAux,fdOutput=open(resposta,O_RDONLY);
    if (argc>=5 ) fdAux = open(resposta,O_WRONLY);
    char * buffer = malloc(2048);
    //ler resposta do servidor
    while ((bytes_read=read(fdOutput,buffer,2048))>0) {
      write(1,buffer,bytes_read);
      //se o pedido estiver concluído enviar ao servidor
      //esta informação para ele libertar as operações usadas
      //um c no inicio, seguido do nome do identificador do cliente
      if (argc>=5  && (isConcluded(buffer) || error(buffer))) close(fdAux);
    }
    close(fdPedido);
    close(fdOutput);
    unlink(resposta);
  }
  else close(fdPedido);
  return 0;
}
