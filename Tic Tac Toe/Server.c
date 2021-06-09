#include <stdio.h>
#include <sys/socket.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/in.h>

#include "utilityfunction.c"
#include "constantsInCommon.h"

/**********************************************************************/

/* Struttura per rappresentare le informazioni 
   necessarie per un client attualmente connesso */
typedef struct 
{
   int sd;
   int opponentSd; //sd dell'avversario
   int status;     //0: pronto, 1: in gioco/in menu
   int victories;  //Numero di partite vinte
   int draws;      //Numero di partite pareggiate
   char * nickname;
} clientTable;


/**********************************************************************/

#define CLIENT_LIMIT 10    /* Limite numero di client contemporaneamente
                              connessi, dev'essere almeno 2 */
#define RANKBUFFSIZE 2048 // Dimensione del buffer dedicato alla classifica


/***************************Synchronization***************************/

//Mutex per l'accesso alla variabile globale "client_conn[]"
pthread_mutex_t clientConnMutex = PTHREAD_MUTEX_INITIALIZER;        

//Mutex per l'accesso alla variabile globale "clientCounter"
pthread_mutex_t clientCounterMutex = PTHREAD_MUTEX_INITIALIZER;


/******************************Prototypes******************************/


void* abortExecution(void* arg);
int checkConnectionLimit(int connect_sd);
void* nicknameManager(void* arg);
void* clientManager(void* arg);
int opponentNicknameRequest(int sd);
int rankingRequest(int sd);
void gameIsOverRequest(int sd, char buffer[]);
int communicateNextMove(int sd, char buffer[]);
int tryToPlayRequest(int sd, char buffer[], int reverse);
int isPossibleToStartNewMatch();
void rankingInsertionSort(clientTable * client[], int size);
int checkConditionStartNewMatch(int reverse, int i);
void disconnectClient(int sd);


/**********************************************************************/


//Array dei client connessi
clientTable * client_conn[CLIENT_LIMIT] = { NULL };

/* Conta il numero di client attualmente connessi,
   utilizzata per verificare se un client 
   può connettersi senza sforare CLIENT_LIMIT
   Nota: non assicura la presenza di numero 
         "clientCounter" record in client_conn[] */
int clientCounter = 0;

/**********************************************************************/

int main(int argc, char** argv)
{
   if(argc != 2) {
      printf("\nERRORE - l'utilizzo prevede la seguente semantica:\n");
      printf("./Server <porta_del_server>\n\n");
      exit(EXIT_FAILURE);
   }
   
	int listen_sd, connect_sd, err, portNum;
	pthread_t tidNicknameManager, tidAbortExecution;
	struct sockaddr_in my_addr;

	manageSignal();
	
	portNum = atoi(argv[1]);
	if(portNum < 1024) {
	   printf("ERRORE - la porta inserita non è valida.\n");
      exit(1);
	}
	
	my_addr.sin_family = AF_INET;
	my_addr.sin_port= htons(portNum);
	my_addr.sin_addr.s_addr = htonl(INADDR_ANY);

	if((listen_sd = socket(PF_INET, SOCK_STREAM, 0)) < 0)
	{
		perror("socket");
		exit(1);
	}
	
	int option=1;
   if (setsockopt(listen_sd,SOL_SOCKET,SO_REUSEADDR,&option,sizeof(int)) == -1) 
   {
       perror("setsockopt");
       exit(1);
   }
	
	if(bind(listen_sd,(struct sockaddr*) &my_addr,sizeof(my_addr))<0)
	{
		perror("bind");
		exit(1);
	}
	
	if(listen(listen_sd, CLIENT_LIMIT)<0)
	{
		perror("listen");
		exit(1);
	}
	
	printf("\n	        	-----ESECUZIONE AVVIATA-----\n\n");
	
	//Lancio del thread che si occupa di interrompere l'esecuzione del server quando richiesto
	if((err=pthread_create(&tidAbortExecution,NULL,&abortExecution,NULL)) != 0)
	{
		 printf("ERRORE: %s\n",strerror(err));
		 exit(1);
   }
   if(pthread_detach(tidAbortExecution) > 0)
   {
	   perror("detach");
	   exit(1);
   }
	
	while(1)
	{
		if((connect_sd=accept(listen_sd,NULL,NULL))<0)
		{
			perror("accept");
			exit(1);
		}
		
		if(!checkConnectionLimit(connect_sd))
		   continue; //Limite raggiunto
		   
		int * thread_sd = (int*) malloc(sizeof(int));
		*thread_sd = connect_sd;
		
		//Lancio del thread che si occupa della verifica dei nickname
		if((err=pthread_create(&tidNicknameManager,NULL,&nicknameManager,(void*)thread_sd)) != 0)
	   {
		   printf("ERRORE: %s\n",strerror(err));
		   exit(1);
	   }
	   if(pthread_detach(tidNicknameManager) > 0)
	   {
		   perror("detach");
		   exit(1);
	   }
	}
	close(connect_sd);
	close(listen_sd);
	
	return 0;
}

/**********************************************************************/

void* abortExecution(void* arg) {
   char buf[100];
   
   while(1)
   {
      fgets(buf,99,stdin);
      int len = strlen(buf);
	   if(buf[len-1] == '\n')
			buf[len-1] = '\0';
		
		if(strcmp(buf, "abort") == 0) {
		   printf("\n	        	-----ESECUZIONE ABORTITA-----\n\n");
		   exit(1);
		}
   }
   return NULL;
}

/**********************************************************************/

int checkConnectionLimit(int connect_sd) 
{
   int ret = 0;
   
   pthread_mutex_lock(&clientCounterMutex);
	clientCounter++; /* Si incrementa prima delle write per evitare 
	                    che nel frattempo un altro client provi a connettersi */
		
	if(clientCounter > CLIENT_LIMIT) 
	{
		if(write(connect_sd, CONNECTION_REFUSED, 7) != 7)
		   perror("write");
		   
		clientCounter--;
		//ret = 0; connection refused
	}
	else
	{
		if(write(connect_sd, CONNECTION_ACCEPTED, 8) != 8) {
		   perror("write");
		   clientCounter--;
		   //ret = 0; connection refused
		}
		else
		   ret = 1; //connection accepted
	}
	
	pthread_mutex_unlock(&clientCounterMutex);
	
	return ret;
}

/**********************************************************************/

void* nicknameManager(void* arg)
{
   int sd = *((int*)arg);
   char * nicknameBuf = (char*)malloc(sizeof(char)*21);
   memset(nicknameBuf, 0, 21);
   
   while(1)
   {
      if(read(sd, nicknameBuf, 20) <= 0) {
         pthread_mutex_lock(&clientCounterMutex);
	      clientCounter--;
	      pthread_mutex_unlock(&clientCounterMutex);
	
         free(nicknameBuf);
         free(arg);
         return NULL;
      }
      
      int flag = 0;
      
      pthread_mutex_lock(&clientConnMutex);

      for(int i=0; i<CLIENT_LIMIT; i++) 
      {
         if(client_conn[i] != NULL && strcmp(client_conn[i]->nickname, nicknameBuf) == 0) {
            flag = 1; //Il nickname è già utilizzato
            
            if(write(sd, NICKNAME_REFUSED, 2) < 2) 
            {
               pthread_mutex_lock(&clientCounterMutex);
	            clientCounter--;
	            pthread_mutex_unlock(&clientCounterMutex);
	      
               free(nicknameBuf);
               free(arg);
               return NULL;
            }
            break;
         }
      }
      
      //Il nickname è corretto
      if(flag == 0) 
      {
         printf("\nCLIENT CONNESSO: %s\n", nicknameBuf);
         
         pthread_mutex_lock(&clientCounterMutex);
         printf("NUMERO DI CLIENT CONNESSI: %d\n", clientCounter);
         pthread_mutex_unlock(&clientCounterMutex);
         
         if(write(sd, NICKNAME_ACCEPTED, 3) < 3) {
             pthread_mutex_lock(&clientCounterMutex);
	          clientCounter--;
	          pthread_mutex_unlock(&clientCounterMutex);
	           
	          free(nicknameBuf);
             free(arg); 
             return NULL;
         }
       
         //Aggiorna l'array dei client, con stato 0 (disponibile)
		   for(int i = 0; i<CLIENT_LIMIT ; i++)
		   {
			   if(client_conn[i] == NULL)
			   {
				   client_conn[i] = (clientTable*)malloc(sizeof(clientTable));
				   client_conn[i]->sd = sd;
				   client_conn[i]->status = 1;
				   client_conn[i]->opponentSd = -1;
				   client_conn[i]->nickname = nicknameBuf;
				   client_conn[i]->victories = 0;
				   client_conn[i]->draws = 0;
				   break;
			   }
		   }
        
		   pthread_mutex_unlock(&clientConnMutex);
		   break;
      }
      else 
      {
         memset(nicknameBuf, 0, 21);
         pthread_mutex_unlock(&clientConnMutex);
      }
   }
   
   int err;
   pthread_t tidClientStatusManager;
   
   //Lancio del thread principale che si occupa delle richieste del client
	if((err=pthread_create(&tidClientStatusManager,NULL,&clientManager,(void*)arg)) != 0)
	{
	   printf("ERRORE: %s\n",strerror(err));
	   exit(1);
	}
	if(pthread_detach(tidClientStatusManager)>0)
	{
	   perror("detach");
	   exit(1);
   }
   
   return NULL;
}

/**********************************************************************/

void* clientManager(void* arg)
{
   int sd = *((int*)arg);
   free(arg);
   
   char* buffer = (char*)malloc(sizeof(char)*256);
   int clientIsAlive = 1;
   int reverse = 0; //Flag per bilanciare la priorità dell'inizio partita
   
   while(clientIsAlive == 1) 
   {
      if(read(sd, buffer, 255) <= 0)
         buffer[0] = CLIENT_DISCONNECTED_REQUEST; //Forza disconnessione
         
      switch(buffer[0]) {
         case ABORT_FIND_MATCH_REQUEST: { //Richiesta annullamento ricerca partita
            pthread_mutex_lock(&clientConnMutex);
         
            for(int i=0; i<CLIENT_LIMIT; i++) {
                if(client_conn[i] != NULL && client_conn[i]->sd == sd) {
                   client_conn[i]->status = 1;
                   break;
                }
            }
            
            pthread_mutex_unlock(&clientConnMutex);
            
            if(writen(sd, SEARCH_ABORTED_MSG, 14)<14) {
               perror("writen");
		         disconnectClient(sd);
		         clientIsAlive = 0;
            }
            break;
         }
         
         case OPPONENT_NICKNAME_REQUEST: { //Richiesta nickname avversario
            clientIsAlive = opponentNicknameRequest(sd);
            break;
         }
         
         case RANKING_REQUEST: { //Richiesta classifica
            clientIsAlive = rankingRequest(sd);
            break;
         }
         
         case MATCH_IS_OVER_REQUEST: { //La partita è terminata
            gameIsOverRequest(sd, buffer);
            break;
         }
         
         case CLIENT_DISCONNECTED_REQUEST: { //Il client si disconnette
            clientIsAlive = 0;
            disconnectClient(sd);
            break;
         }
         
         case TRY_TO_PLAY_REQUEST: { //Il client prova a giocare
            clientIsAlive = tryToPlayRequest(sd, buffer, reverse);
         
            if(!reverse)
               reverse = 1;
            else
               reverse = 0;
               
            break;
         }
         
         default: {
            if(isdigit(buffer[0])) { //La conversione ha avuto successo, richiesta mossa tris
               if(buffer[0] < 49 || buffer[0] > 57) { //Codice Ascii per i numeri < 1 e > 9
                  printf("\nIllegal move.\n");
                  disconnectClient(sd);
                  clientIsAlive = 0;
               }
               clientIsAlive = communicateNextMove(sd, buffer);
            }
            //else comando sconosciuto, ignora
         }
      }
      
      //Pulizia del buffer per la prossima read
      memset(buffer, 0, 256);
   }
   
   //Sezione dedicata al client che si sta disconnettendo
   free(buffer);
   
   return NULL;
}

/**********************************************************************/

//Restituisce 0 in caso di errore, 1 altrimenti
int opponentNicknameRequest(int sd) {
   int opponentSd;
         
   pthread_mutex_lock(&clientConnMutex);
   for(int i=0; i<CLIENT_LIMIT; i++) {
       if(client_conn[i] != NULL && client_conn[i]->sd == sd) {
          opponentSd = client_conn[i]->opponentSd;
          break;
       }
   }
   
   if(opponentSd == -1)
   {
      if(writen(sd, ILLEGAL_REQUEST, 15)<15)
      {
         perror("write");
         pthread_mutex_unlock(&clientConnMutex);
         disconnectClient(sd);
         return 0;
      }
   }
   
   for(int i=0; i<CLIENT_LIMIT; i++) {
       if(client_conn[i] != NULL && client_conn[i]->sd == opponentSd) {
          int len = strlen(client_conn[i]->nickname);
          if(writen(sd, client_conn[i]->nickname, len) < len)
          {
             perror("write");
             pthread_mutex_unlock(&clientConnMutex);
             disconnectClient(sd);
             return 0;
          }
          break;
       }
   }
   
   pthread_mutex_unlock(&clientConnMutex);
      
   return 1;
}

/**********************************************************************/

//Restituisce 0 in caso di errore, 1 altrimenti
int rankingRequest(int sd) {
   char * rankingBuffer = (char*)malloc(sizeof(char)*RANKBUFFSIZE);
         
   memset(rankingBuffer, 0, RANKBUFFSIZE);
   
   pthread_mutex_lock(&clientConnMutex);
   
   int clientsNum = 0;
   for(int i=0; i<CLIENT_LIMIT; i++){
       if(client_conn[i] != NULL)
          clientsNum++;
   }
   
   clientTable * client_conn_copy[clientsNum];
   
   /* Copia di client_conn[] in un array temporaneo che verrà 
      poi ordinato per numero di vittorie e pareggi */
   int j=0;
   for(int i=0; i<CLIENT_LIMIT; i++){
       if(client_conn[i] != NULL) 
       {
          client_conn_copy[j] = client_conn[i];
          j++;
       }
   }
   
   //Ordinamento di client_conn_copy[]
   rankingInsertionSort(client_conn_copy, clientsNum);
   
   char vict[10], draws[10], ranking[CLIENT_LIMIT];
   
   //client_conn_copy[] è ordinato per numero di vittorie e pareggi
   for(int i=0; i<clientsNum; i++) 
   { 
       sprintf(ranking, "%d", i+1);
       strcat(rankingBuffer, ranking);
       strcat(rankingBuffer, ". ");
       
       strcat(rankingBuffer, client_conn_copy[i]->nickname);
       
       strcat(rankingBuffer, "   Vittorie: ");
       sprintf(vict, "%d", client_conn_copy[i]->victories);
       strcat(rankingBuffer, vict);
       
       strcat(rankingBuffer, "   Pareggi: ");
       sprintf(draws, "%d", client_conn_copy[i]->draws);
       strcat(rankingBuffer, draws);
       
       strcat(rankingBuffer, "\n");
       
       memset(vict, 0, 10);
       memset(draws, 0, 10);
       memset(ranking, 0, CLIENT_LIMIT);
   }
   
   pthread_mutex_unlock(&clientConnMutex);
   
   int len = strlen(rankingBuffer);
   
   //Comunichiamo al client quanti byte deve leggere
   int converted_number = htonl(len);
   if(write(sd, &converted_number, sizeof(converted_number)) < sizeof(converted_number)) {
      perror("write");
      free(rankingBuffer);
      disconnectClient(sd);
      
      return 0;
   }
   
   //Ci mettiamo in attesa della risposta del client
   char continueRequest[1];
   if(read(sd, continueRequest, 1) < 1) { //1 : sizeof(char)*1
		perror("read");
		free(rankingBuffer);
      disconnectClient(sd);
      
      return 0;
	} else if(continueRequest[0] != RANKING_REQUEST) {
	   fprintf(stderr, "Error, wrong request.\n");
      free(rankingBuffer);
      disconnectClient(sd);
      
      return 0;
	}
   
   //Terminiamo la richiesta inviando la classifica al client
   if(writen(sd, rankingBuffer, len) < len)
   {
      perror("writen");
      free(rankingBuffer);
      disconnectClient(sd);
      
      return 0;
   }
   
   free(rankingBuffer);
   
   return 1;
}

/**********************************************************************/

//Restituisce 0 in caso di errore, 1 altrimenti
void gameIsOverRequest(int sd, char buffer[]) {
   pthread_mutex_lock(&clientConnMutex);
         
   int indexSd;
   
   //Acquisisco la posizione di sd
   for(int i=0; i<CLIENT_LIMIT; i++) 
   {
       if(client_conn[i] != NULL && client_conn[i]->sd == sd)
       {
          indexSd = i;
          break;
       }
   }
   
   /* client_conn[] struct:
      int sd;
      int opponentSd; //sd dell'avversario
      int status;     //0: pronto, 1: in gioco/in menu
      int victories;  //Numero di partite vinte
      int draws;      //Numero di partite pareggiate
      char * nickname;
   */
            
   int opponentIndexSd;
   for(int i=0; i<CLIENT_LIMIT; i++) {
       if(client_conn[i] != NULL && client_conn[i]->sd == client_conn[indexSd]->opponentSd) {
          opponentIndexSd = i;
          break;
       }
   }
   
   //client_conn[opponentIndexSd]->opponentSd != -1 indica che le stampe di log sono già avvenute
   
   if(buffer[1] == CLIENT_WON_MATCH) //questo sd ha vinto
   {
      client_conn[indexSd]->victories++;
      
      if(client_conn[opponentIndexSd]->opponentSd != -1)
      {
         printf("\nPARTITA TERMINATA \"%s\" VS \"%s\"\n",
                client_conn[indexSd]->nickname, client_conn[opponentIndexSd]->nickname);
         printf("\"%s\" HA VINTO.\n", client_conn[indexSd]->nickname);
      }
   }   
   else if(buffer[1] == CLIENT_MATCH_TIE) //Partita terminata in pareggio
   {
      client_conn[indexSd]->draws++;
      
      if(client_conn[opponentIndexSd]->opponentSd != -1)
         printf("\nPARTITA TERMINATA \"%s\" VS \"%s\"\nPAREGGIO\n",
                client_conn[indexSd]->nickname, client_conn[opponentIndexSd]->nickname);
   }
   else if(client_conn[opponentIndexSd]->opponentSd != -1)
   {
      printf("\nPARTITA TERMINATA \"%s\" VS \"%s\"\n",
             client_conn[indexSd]->nickname, client_conn[opponentIndexSd]->nickname);
      printf("\"%s\" HA PERSO.\n", client_conn[indexSd]->nickname);
   }
   
   client_conn[indexSd]->opponentSd = -1; //Reset avversario
   
   pthread_mutex_unlock(&clientConnMutex);
}

/**********************************************************************/

//Restituisce 0 in caso di errore, 1 altrimenti
int communicateNextMove(int sd, char buffer[]) {
   pthread_mutex_lock(&clientConnMutex);
   
   int opponentSd;
   
   for(int i=0; i<CLIENT_LIMIT; i++) 
   {
       if(client_conn[i] != NULL && client_conn[i]->sd == sd) 
       {
          opponentSd = client_conn[i]->opponentSd;
          break;
       }
   }
   
   //Verifica se l'avversario è ancora connesso
   for(int i=0; i<CLIENT_LIMIT; i++) 
   {
       if(client_conn[i] != NULL && client_conn[i]->sd == opponentSd) 
       {
          //Comunicazione mossa avvenuta
          if(write(sd, MOVE_INSERTED_MSG, 2)<1) 
          {
             perror("write");
             pthread_mutex_unlock(&clientConnMutex);
             disconnectClient(sd);
             return 0;
          }
            
          if(write(opponentSd, buffer, 1)<1) 
          {
             perror("write");
             pthread_mutex_unlock(&clientConnMutex);
             disconnectClient(sd);
             return 0;
          }
          break;
       }
   }
   
   pthread_mutex_unlock(&clientConnMutex);
   
   return 1;
}

/**********************************************************************/

//Restituisce 0 in caso di errore, 1 altrimenti
int tryToPlayRequest(int sd, char buffer[], int reverse) {
   pthread_mutex_lock(&clientConnMutex);
         
   //Impostiamo lo stato di questo client come pronto a giocare
   for(int i=0; i<CLIENT_LIMIT; i++) 
   {
      if(client_conn[i] != NULL && client_conn[i]->sd == sd) {
         client_conn[i]->status = 0;
         break;
      }
   }
   
   pthread_mutex_unlock(&clientConnMutex);
   
   if(!isPossibleToStartNewMatch()) //Se falso, allora il client deve attendere
   {            
      if(write(sd, WAIT_FOR_MATCH_TO_BEGIN, 4) < 4)
      {
         perror("write");
         disconnectClient(sd);
         return 0;
      }
      return 1; //Rimette il thread in attesa di nuovi comandi tramite read
   }
      
   /* Comincia tutte le possibili nuove partite
      Nota: i thread dedicati ai client con stato 0 sono in attesa di read */
   
   int i, sd1 = -1, sd2 = -1;
   int sd1Index = -1, sd2Index = -1;
   
   if(!reverse)
      i=0;
   else
      i=CLIENT_LIMIT-1;
   
   pthread_mutex_lock(&clientConnMutex);
   
   //Si creano partite per coppie di client
   while(checkConditionStartNewMatch(reverse, i))
   {
       //Nuovo candidato a giocare individuato
       if(client_conn[i] != NULL && client_conn[i]->status == 0) 
       {
          if(sd1 == -1) 
          {
             sd1 = client_conn[i]->sd;
             sd1Index = i;
          }
          else if(sd2 == -1) 
          {
             sd2 = client_conn[i]->sd;
             sd2Index = i;
          }
       }
       
       //è possibile cominciare una nuova partita
       if(sd1 != -1 && sd2 != -1) 
       {
          client_conn[sd1Index]->status = 1; //1: In gioco/Nel menu
          client_conn[sd1Index]->opponentSd = sd2;
          client_conn[sd2Index]->status = 1;
          client_conn[sd2Index]->opponentSd = sd1;
          
          int random = rand() % 1;
          int x, y;
          
          if(random == 0) 
          {
             if((x=write(sd1, FIRST_CLIENT_TO_PLAY_MSG, 6))<6 || (y=write(sd2, SECOND_CLIENT_TO_PLAY_MSG, 6))<6) 
             {		                   
                perror("write");
                pthread_mutex_unlock(&clientConnMutex);
                
                if(x<6)
                   disconnectClient(sd1);
                if(y<6)
                   disconnectClient(sd2);   
                
                return 0;
             }
          }
          else 
          {
             if((x=write(sd1, SECOND_CLIENT_TO_PLAY_MSG, 6))<6 || (y=write(sd2, FIRST_CLIENT_TO_PLAY_MSG, 6))<6) 
             {		                   
                perror("write");
                pthread_mutex_unlock(&clientConnMutex);
                
                if(x<6)
                   disconnectClient(sd1);
                if(y<6)
                   disconnectClient(sd2);   
                
                return 0;
             }
          }
          
          printf("\nPARTITA INIZIATA \"%s\" VS \"%s\" \n", 
                    client_conn[sd1Index]->nickname, client_conn[sd2Index]->nickname);
          
          sd1 = -1; sd2 = -1;
          sd1Index = -1; sd2Index = -1;                
       }
       
       if(!reverse)
          i++;
       else
          i--;  
   }   
   
   /* Al termine del ciclo while precedente,
      rimane al più un client in attesa di giocare */
      
   pthread_mutex_unlock(&clientConnMutex);
   
   return 1;
}

/**********************************************************************/

/* Restituisce 1 se è possibile far cominciare
   almeno una partita, 0 altrimenti */
int isPossibleToStartNewMatch()
{
   int clientsNum = 0; //Numero di client disponibili
   
   pthread_mutex_lock(&clientConnMutex);
   
   for(int i=0; i<CLIENT_LIMIT; i++) 
   {
      if(client_conn[i] != NULL && client_conn[i]->status == 0)
         clientsNum++;
   
      //Se ci sono almeno 2 client disponibili, è possibile avviare una nuova partita   
      if(clientsNum == 2)
      {
         pthread_mutex_unlock(&clientConnMutex);
         return 1;
      }   
   }
   
   pthread_mutex_unlock(&clientConnMutex);
   return 0;
}

/**********************************************************************/

//Ordina una struttura clientTable per numero di vittorie e pareggi
void rankingInsertionSort(clientTable * client[], int size) 
{
   int i, j;
   clientTable * temp;
   
   for(i=1; i<size; i++)
   {
      temp = client[i];
      j = i-1; 
      
      while(j>=0 && ((client[j]->victories < temp->victories) || 
            (client[j]->victories == temp->victories && client[j]->draws < temp->draws)))
      {
		   client[j+1] = client[j];		
		   j--;
	   }
	   client[j+1] = temp;
	}
}

/**********************************************************************/

/* Restituisce 1 se occorre ancora ciclare nel while chiamante
   che si occupa di far partire nuove partite, 0 altrimenti */
int checkConditionStartNewMatch(int reverse, int i) 
{
   if(!reverse)
   {
      if(i == CLIENT_LIMIT)
         return 0;
      return 1;
   }
   else
   {
      if(i == -1)
         return 0;
      return 1;
   }   
}

/**********************************************************************/

void disconnectClient(int sd) {
   pthread_mutex_lock(&clientConnMutex);
         
   //Ricerca della cella del client che si è disconnesso
   for(int i=0; i<CLIENT_LIMIT; i++) 
   {
       //Allora questa è la cella da deallocare
       if(client_conn[i] != NULL && client_conn[i]->sd == sd) 
       {
          printf("\nCLIENT DISCONNESSO: %s\n", client_conn[i]->nickname);
          
          pthread_mutex_lock(&clientCounterMutex);
          clientCounter--;
          printf("NUMERO DI CLIENT CONNESSI: %d\n", clientCounter);
          pthread_mutex_unlock(&clientCounterMutex);
          
          //L'utente si è disconnesso durante una partita
          if(client_conn[i]->opponentSd != -1) 
          {
             char tmpBuff[] = CONNECTION_LOST_MSG;
             
             if(writen(client_conn[i]->opponentSd, tmpBuff, strlen(tmpBuff)) < strlen(tmpBuff))
                perror("writen");
          }
          free(client_conn[i]->nickname);
          free(client_conn[i]);
          client_conn[i] = NULL;
          break;
       }
   }
   
   pthread_mutex_unlock(&clientConnMutex);
}
