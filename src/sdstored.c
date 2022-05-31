#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/stat.h>

int continuar;
int fezPedidos;

typedef struct pedido{
  char * pid;
  int priority;
  int cmds[7];
  char ** args;
  struct pedido * prox;
} *pedidos;


void printPedidos(pedidos p, int fd){
  while (p){
    char * pedido = malloc(200);
    sprintf(pedido,"Pedido:\nPid:%s\nPrioridade:%d\nArgs: ",p->pid,p->priority);
    for (int i=0; p->args[i];i++) {strcat(pedido,p->args[i]);strcat(pedido," ");}
    strcat(pedido,"\n\n");
    write(fd,pedido,strlen(pedido));
    p=p->prox;
  }
  write(fd,"\n\n",1);
}

// função correspondente ao sinal SIGTERM, usado para terminar o servidor
void terminaServidor (int signal){
  continuar=0;
  if (fezPedidos){
      int fd = open("pedido",O_WRONLY);
      write(fd,"kill\n",5);
      close(fd);
  }
}

// devolve a posição onde está especifico o número de vezes que a operação pode ser feita
int getPosOp (char * op){
  int r=-1;
  if (!strcmp(op,"bcompress")) r=0;
  else if (!strcmp(op,"bdecompress"))r=1;
  else if (!strcmp(op,"gcompress"))r=2;
  else if (!strcmp(op,"gdecompress"))r=3;
  else if (!strcmp(op,"encrypt"))r=4;
  else if (!strcmp(op,"decrypt"))r=5;
  else if (!strcmp(op,"nop"))r=6;
  return r;
}

// separa as transformações pedidas
int parseArgs (char *buffer, char **args, int maxInstrucoes, int *j,int cmds[7]){
  int posOpExec=0,flag=1,i;
  for (int k=0; k<7; k++) cmds[k]=0;
  for (i=0;i<maxInstrucoes && buffer && flag;i++)
      if (strlen(args[i] = strdup(strsep(&buffer," \n\0")))<2) i--;
      else {
        posOpExec = getPosOp(args[i]);
        if (posOpExec == -1) flag=0;
        cmds[posOpExec]++;
      }
  *j=i;
  return flag;
}

// verfica se têm instâncias suficientes para executar pedido
int enoughResources (int nmax[7], int nmaxfixo[7],int cmds[7]){
  int flag=1,i;
  for (i=0;i<7 && flag==1;i++){
    if (cmds[i]>nmaxfixo[i]) flag=-1;
    else if (cmds[i]>nmax[i]) flag=0;
    else nmax[i]-=cmds[i];
  }
  if (flag!=1)
    for (int j=0;j<i-1;j++)  nmax[j]+=cmds[j];
    return flag;
}




//cria e adiciona pedido de acordo com a sua prioridade
int addPedidoPriority (pedidos *pp, char*pid, int pr, int cmds[7],char **args){
  pedidos p = malloc(sizeof(struct pedido));
  p->pid = pid;
  p->priority = pr;
  p->args = args;
  for (int i=0; i<7; i++) p->cmds[i]=cmds[i];
  p->prox = (*pp);
  while (*pp &&  ((*pp)->priority) > pr) pp = &((*pp)->prox);
  p->prox = (*pp);
  (*pp) = p;
  return 0;
}

//cria e adiciona pedido no início
int addPedido (pedidos *pp, char *pid, int pr, int cmds[7],char **args){
  pedidos p = malloc(sizeof(struct pedido));
  p->pid = pid;
  p->priority = pr;
  p->args = args;
  for (int i=0; i<7; i++) p->cmds[i]=cmds[i];
  p->prox = (*pp);
  (*pp) = p;
  return 0;
}

//adiciona pedido no inicio
int addPedido2 (pedidos *pp, pedidos p){
  p->prox = (*pp);
  (*pp) = p;
  return 0;
}


// ler uma linha
ssize_t readln(int fd, char *line, size_t size){
  size_t i;
  for (i=0; i<size && (read(fd, line, 1))>0; i++) {
    if (*line=='\n') {
      i++;
      break;
    }
    line++;
  }
  return i;
}

//recebe um descritor de ficheiro e devolve o número de bytes desse
// ficheiro
int getNBytes (char *f){
  int fd = open(f,O_RDONLY);
  int r = (int) lseek(fd,0,SEEK_END);
  close(fd);
  return r;
}

// free do pedido
void freePedido(pedidos p){
  free(p->pid);
  for (int i =0; p->args[i]; i++) free(p->args[i]);
  free(p->args);
  free(p);
}

int main(int argc, char const *argv[]) {
  int nmax[7],nmaxfixo[7],bytesr,i;
  char *pathTransf,*fe,*fs;int maxInstrucoes=0;
  continuar = 1;
  fezPedidos = 0;
  signal(SIGTERM,terminaServidor);
  if (argc != 3) {
    perror("Número de argumentos inválido\n");
    return 1;
  }

  for (i=0; i<7; i++) nmax[i] = -1;
  int fdl = open(argv[1],O_RDONLY);
  char *buffer1 = malloc(2048);
  char *buffer2 = buffer1;
  // ler o ficheiro que contêm a informação relativa ao número de operações que é
  // possível fazer concorrentemente
  while ((bytesr=read(fdl,buffer1,2048))>0){
    for (i=0; buffer1 && i<7;i++){
        char * op; int num,pos;
        op=strdup(strsep(&buffer1, " "));
        num = atoi(strsep(&buffer1, "\n"));
        maxInstrucoes +=num;
        if ((pos=getPosOp(op))==-1 && nmax[pos]!=-1) {
          perror("Operação inválida/repetida");
          return 1;
        }
        nmax[pos] = num;
        nmaxfixo[pos] = num;
        free(op);
    }
    if (i!=7) {
      perror("Falta definir valores para algumas operações");
      return 1;
    }
  }
  free(buffer2);
  close(fdl);
  pathTransf = malloc(strlen(argv[2] + 20)); // para acrescentar o comando
  strcpy(pathTransf,argv[2]);
  strcat(pathTransf,"/");
  pedidos pedidosPendentes = NULL;
  pedidos pedidosAtuais = NULL;
  int fdCliente;
  char *buffer= malloc(4096);
  // criar o pipe para receber pedidos dos clientes
  if ((mkfifo("pedido", 0666))== -1){
    perror("mkfifo");
  }
  int fdPedidos = open("pedido", O_RDONLY);
  fezPedidos = 1;
  int fdPedidosWR;
  if (continuar) fdPedidosWR = open("pedido", O_WRONLY); // para esperar por novos clientes
  //ir lendo os pedidos enviados por clientes
  while ((continuar || fezPedidos)&&(bytesr = readln(fdPedidos,buffer,4096))>0){
    buffer[bytesr-1] = '\0';
    char *buffercpy = strdup(buffer);
    char *iniciobuff = buffercpy;
    char * resposta;
    char**args = calloc(maxInstrucoes,sizeof(char *));
    //se a primeira letra é um s, a operação a efetuar é o status
    if (continuar && buffercpy[0]=='s'){
      if (fork()==0){
        buffercpy++;
        //abrir o pipe para comunicar com o cliente
        if ((fdCliente = open(buffercpy,O_WRONLY))==-1) perror ("fdCliente open error");
        char * numeros= malloc(50);
        if (pedidosPendentes) write(fdCliente,"Pedidos pendentes:\n",19);
        printPedidos(pedidosPendentes,fdCliente);
        if (pedidosAtuais) write(fdCliente,"Pedidos a executar:\n",20);
        printPedidos(pedidosAtuais,fdCliente);
        sprintf(numeros,"transf bcompress: %d/%d \n",nmaxfixo[0]-nmax[0],nmaxfixo[0]);
        write(fdCliente,numeros,strlen(numeros));
        sprintf(numeros,"transf bdecompress: %d/%d \n",nmaxfixo[1]-nmax[1],nmaxfixo[1]);
        write(fdCliente,numeros,strlen(numeros));
        sprintf(numeros,"transf gcompress: %d/%d \n",nmaxfixo[2]-nmax[2],nmaxfixo[2]);
        write(fdCliente,numeros,strlen(numeros));
        sprintf(numeros,"transf gdecompress: %d/%d \n",nmaxfixo[3]-nmax[3],nmaxfixo[3]);
        write(fdCliente,numeros,strlen(numeros));
        sprintf(numeros,"transf encrypt: %d/%d \n",nmaxfixo[4]-nmax[4],nmaxfixo[4]);
        write(fdCliente,numeros,strlen(numeros));
        sprintf(numeros,"transf decrypt: %d/%d \n",nmaxfixo[5]-nmax[5],nmaxfixo[5]);
        write(fdCliente,numeros,strlen(numeros));
        sprintf(numeros,"transf nop: %d/%d \n",nmaxfixo[6]-nmax[6],nmaxfixo[6]);
        write(fdCliente,numeros,strlen(numeros));
        free(numeros);
        close(fdCliente);
        _exit(0);
      }
      close(fdCliente);
    }
    //se a primeira letra é um c, o servidor é notificado que
    // um dado pedido acabou a sua execução
    else if (buffercpy[0]=='c'){
      buffercpy++;
      int pidCliente = atoi(buffercpy);
      int i;
      pedidos *aux = &pedidosAtuais;
      //procurar o pedido que acabou a execução
      while ((*aux) && (atoi((*aux)->pid)!=pidCliente)) aux=&((*aux)->prox);
      // voltar a colocar os recursos (operações) usados na execução do pedido
      if (!(*aux)) perror("Não existe pedido");
      else {
          for (i=0;i<7;i++) nmax[i]+=(*aux)->cmds[i];
          pedidos p = (*aux);
          *aux = (*aux)->prox;
          freePedido(p);
      }
      //verificar se após a conclusão do pedido é possível executar
      // algum dos pedidos que estão pendentes
      aux = &pedidosPendentes;
      while(*aux){
        if (enoughResources(nmax,nmaxfixo,(*aux)->cmds)){
          pedidos pedidoExecutar = (*aux);
          (*aux)=(*aux)->prox;
          pedidoExecutar->prox = NULL;
          resposta = (pedidoExecutar)->pid;
          fe = (pedidoExecutar)->args[0];
          fs = (pedidoExecutar)->args[1];
          args= (pedidoExecutar)->args;
          fdCliente = open(resposta,O_WRONLY);
          addPedido2(&pedidosAtuais,pedidoExecutar);
          if (fork()==0) goto executapedido;
          else close(fdCliente);
        }
        else aux=&((*aux)->prox);
      }
        if (!continuar && !pedidosAtuais && !pedidosPendentes) close(fdPedidosWR);
    }
    // se nem começar por s nem c, a operação a efetuar é o proc-file
    else if (continuar){
        char * pedido = strdup(buffercpy);
        int prioridade = pedido[0]-'0';
        buffercpy++;
        resposta = strdup(strsep(&buffercpy," "));
        fdCliente = open(resposta,O_WRONLY);
        int j;
        fe = strdup(strsep(&buffercpy," "));
        fs = strdup(strsep(&buffercpy," "));
        args[0] = fe;
        args[1]= fs;
        //verificar se pode efetuar o conjunto de operações pedido
        int cmds[7];
        int flag = parseArgs (buffercpy, args+2,maxInstrucoes-3, &i,cmds);
        args[i+2]=NULL;
        if (!flag) {
          write(fdCliente,"Erro: Pedido com argumentos invalidos\n", 38);
          close(fdCliente);
        }
        flag = enoughResources(nmax, nmaxfixo,cmds);
        if (flag==-1) {
          write(fdCliente,"Erro: Pedido inválido - instâncias excedem o limite\n", 54);
          close(fdCliente);
        }
        //se naquele momento não houver recursos suficientes, o pedido
        //é colocado na lista dos pedidos pendentes
        //(de acordo com a sua prioridade)
        else if (!flag){
          write(fdCliente,"pending\n", 8);
          addPedidoPriority(&pedidosPendentes,resposta,prioridade,cmds,args);
          close(fdCliente);
        }
        else {
          addPedido(&pedidosAtuais,resposta,prioridade,cmds,args);
          //executar os comandos pedidos
          if (fork()==0){
            executapedido:
            write(fdCliente,"processing\n", 11);
            int fdin = open(fe,O_RDONLY),
            fdout = open(fs,O_WRONLY | O_CREAT | O_TRUNC, 0640);
            if (fdin==-1) perror("Ficheiro de entrada inválido");
            if (fdout==-1) perror("Ficheiro de saída inválido");
            dup2(fdin,0);
            dup2(fdout,1);
            close(fdin);
            close(fdout);
            close(fdPedidos);
            if (fork()==0){
              close(fdPedidosWR);
              close(fdCliente);
              if (!args[3]){
                strcat(pathTransf,args[2]);
                execl(pathTransf,args[2],NULL);
                _exit(-1);
              }
              else{
                  int p[i-1][2];
                  for (j=2; args[j]; j++){
                    if (j==2){
                      pipe(p[j]);
                      if (fork()==0){
                        close(p[j][0]);
                        dup2(p[j][1],1);
                        close(p[j][1]);
                        strcat(pathTransf,args[j]);
                        execl(pathTransf,args[j],NULL);
                        _exit(-1);
                      }
                      else close(p[j][1]);
                    }
                    else if (!args[j+1]){
                      if (fork()==0){
                        dup2(p[j-1][0],0);
                        close(p[j-1][0]);
                        strcat(pathTransf,args[j]);
                        execl(pathTransf,args[j],NULL);
                        _exit(-1);
                      }
                      else close(p[j-1][0]);
                    }
                    else {
                      pipe(p[j]);
                      if (fork()==0){
                        close(p[j][0]);
                        dup2(p[j][1],1);
                        close(p[j][1]);
                        dup2(p[j-1][0],0);
                        close(p[j-1][0]);
                        strcat(pathTransf,args[j]);
                        execl(pathTransf,args[j],NULL);
                        _exit(-1);
                      }
                      else {
                        close(p[j][1]);
                        close(p[j-1][0]);
                      }

                    }
                  }
                  for (j=0; j<i; j++) wait(NULL);
                }
                _exit(0);
            }
            wait(NULL);
            char * concluded = malloc(100);
            sprintf(concluded,"concluded (bytes-input: %d, bytes-output: %d)\n", getNBytes(fe),getNBytes(fs));
            write(fdCliente,concluded, strlen(concluded));
            char *pedidoC = malloc(25);
            strcpy(pedidoC,"c");
            strcat(pedidoC,resposta);
            strcat(pedidoC,"\n");
            write(fdPedidosWR,pedidoC,strlen(pedidoC));
            close(fdPedidosWR);
            close(fdCliente);
            free(pedidoC);
            free(concluded);
            _exit(0);
        }
        close(fdCliente);
      }
    }
    else if (buffercpy[0]=='k') {if (!pedidosAtuais && !pedidosPendentes) close(fdPedidosWR);}
    else {
      buffercpy++;
      fdCliente = open(buffercpy,O_WRONLY);
      write(fdCliente,"Erro: servidor a fechar\n",24);
      close(fdCliente);
    }
    free(iniciobuff);
  }
  close(fdPedidos);
  if (bytesr == -1) perror("Erro a ler pedido");
  unlink("pedido");
  return 0;
}
