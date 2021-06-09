#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

#include "utilityfunction.c"
#include "constantsInCommon.h"

/**********************************************************************/

char trisMatrix[3][3]; //'X', 'O'

/**********************************************************************/

#define clear() printf("\033[H\033[J")

/**********************************************************************/

// Prototipi funzioni
void* stopNewGameSearch(void* arg);
int addMoveToMatrix(char movePos, char symbol);
void printMatrix();
int selectAndSendYourMove(char mySymbol, int sd);
void play(int sd, char buf[], char opponentSymbol, char mySymbol);
int checkResult(int movesCnt, char symbol);

/**********************************************************************/

int main(int argc, char** argv)
{
   if(argc != 3) {
      printf("\nERRORE - l'utilizzo prevede la seguente semantica:\n");
      printf("./Client <indirizzo_ip_del_server> <porta_del_server>\n\n");
      exit(EXIT_FAILURE);
   }
   
	struct sockaddr_in srv_addr;
	char nickname[256];
	int sd, portNum;
	
	portNum = atoi(argv[2]);
	if(portNum < 1024) {
	   printf("ERRORE - la porta inserita non è valida.\n");
      exit(1);
	}
	
	srv_addr.sin_family = AF_INET;
	srv_addr.sin_port = htons(portNum);
	inet_aton(argv[1],&srv_addr.sin_addr);
	
	if((sd=socket(PF_INET, SOCK_STREAM, 0))<0)
	{
		perror("socket");
		exit(1);
	}
	
	if(connect(sd,(struct sockaddr *) &srv_addr, sizeof(srv_addr)) < 0)
	{
		perror("connect");
		exit(1);
	}
	
	char buf[100];
	if(read(sd, buf, 100) < 0) 
	{
	   perror("read");
	   exit(1);
	}
	
	if(strcmp(buf, CONNECTION_REFUSED) == 0) 
	{
	   printf("\nIl server è attualmente pieno, riprovare più tardi.\n");
	   exit(1);
	}
	
	memset(buf, 0, 100);
	
	printf("			+-----------------------+");
	printf("\n			|         LOGIN         |\n");
	printf("			+-----------------------+\n\n");
	printf("Inserire un nickname: ");
	
	while(1)
	{
	   fgets(nickname,255,stdin);
	   
	   int len = strlen(nickname);
	   if(nickname[len-1] == '\n')
			nickname[len-1] = '\0';
		
		char * trimmedNick = trimwhitespace(nickname);
		len = strlen(trimmedNick);
		
		if(len < 3 || len > 20)
		   printf("ERRORE - Inserire un nickname di almeno 3 caratteri e al più 20 caratteri: ");
		else 
		{
		   int flag = 0;
		   //Verifica dei caratteri speciali riservati
		   if(trimmedNick[0] == RANKING_REQUEST || 
		      trimmedNick[0] == MATCH_IS_OVER_REQUEST || 
		      trimmedNick[0] == CLIENT_DISCONNECTED_REQUEST ||
		      trimmedNick[0] == TRY_TO_PLAY_REQUEST || 
		      trimmedNick[0] == OPPONENT_NICKNAME_REQUEST || 
		      trimmedNick[0] == ABORT_FIND_MATCH_REQUEST || 
   	      trimmedNick[0] == '1' || trimmedNick[0] == '2' || trimmedNick[0] == '3' ||
            trimmedNick[0] == '4' || trimmedNick[0] == '5' || trimmedNick[0] == '6' || 
		      trimmedNick[0] == '7' || trimmedNick[0] == '8' || trimmedNick[0] == '9') 
		   {
		      
		      printf("\nERRORE - Il tuo nickname non può cominciare\n");
		      printf("per uno di questi caratteri: '%c', '%c','%c','%c','%c', '%c','1-9'\n\nInserisci un Nickname diverso: ", 
		             RANKING_REQUEST, MATCH_IS_OVER_REQUEST, CLIENT_DISCONNECTED_REQUEST, 
		             TRY_TO_PLAY_REQUEST, OPPONENT_NICKNAME_REQUEST, ABORT_FIND_MATCH_REQUEST);
		      flag = 1;
		   }
		   
		   if(flag == 0) //Nickname potenzialmente valido
		   {
		      if(writen(sd, trimmedNick, len) < 0) 
		      {
		         perror("writen");
		         exit(1);
		      }
		      
		      char * serverResponse = (char*)malloc(sizeof(char)*4);
		      
		      if(read(sd, serverResponse, 3) <= 0) 
		      {
		         perror("read");
		         exit(1);
		      }
		      
		      if(strcmp(serverResponse, NICKNAME_REFUSED) == 0)
		         printf("Il nickname inserito non è disponibile, riprova con un nuovo nickname:\n");
       		else //Nickname valido
	      	   break;
	   	}
	   }
	}
	
	printf("\n		+-----------------------------------------+");
	printf("\n		|         BENVENUTO SU TIC TAC TOE        |\n");
	printf("		+-----------------------------------------+\n\n");
	
	int choice, c;
	char rankRequest[2];
	rankRequest[0] = RANKING_REQUEST;

	while(1)
	{
		printf("1) NUOVA PARTITA\n2) CLASSIFICA\n3) LOGOUT\n\n");
		printf("Scegli un'opzione: ");
		
      while(1) 
      {
         if (scanf("%d",&choice) != 1) 
         {
             while ((c = getchar()) != '\n' && c != EOF) {} //clean stdin
             printf("\nSi prega di inserire un numero: ");
         }
         else
             break;
      }
		
		switch(choice) 
		{
		   case 1: 
		   {
		      clear(); //clear stdout
		      
		      for(int i=0; i<3; i++) 
		      {
		          for(int j=0; j<3; j++)
		              trisMatrix[i][j] = ' ';
		      }
		      
		      buf[0] = TRY_TO_PLAY_REQUEST;
		      
		      if(write(sd, buf, 1)<1) 
		      {
		         perror("write");
		         return 1;
		      }
		      
		      memset(buf, 0, 100);
		      
		      if(read(sd, buf, 100) <= 0) 
		      {
		         perror("read");
		         return 1;
		      }
		      
		      int stdinNeedsToBeCleaned = 0;
		      
		      if(strcmp(buf, WAIT_FOR_MATCH_TO_BEGIN) == 0) 
		      {
		         printf("\nIn attesa di nuovi giocatori...\nPremi \"1\" per annullare la ricerca.\n");
		         
		         pthread_t tidStopSearch;
		         int err;
		         int * sdPtr = (int*)malloc(sizeof(int));
		         *sdPtr = sd;
		         
		         //Lancio del thread che si occupa dell'annullamento della ricerca di una partita
		         if((err=pthread_create(&tidStopSearch,NULL,&stopNewGameSearch,(void*)sdPtr)) != 0)
	            {
		            printf("ERRORE: %s\n",strerror(err));
		            exit(1);
	            }
	            if(pthread_detach(tidStopSearch) > 0)
	            {
		            perror("detach");
		            exit(1);
	            }
		        
		         memset(buf, 0, 100);
		         int n;
		         while((n=read(sd, buf, 100)) > 0 && strcmp(buf, WAIT_FOR_MATCH_TO_BEGIN) == 0) {}
               
		         pthread_cancel(tidStopSearch);
		         
		         if(n <= 0) 
		         {
		            perror("read");
		            exit(1);
		         }
		         else if(strcmp(buf, SEARCH_ABORTED_MSG) == 0) {
		            while ((c = getchar()) != '\n' && c != EOF) {} //clean stdin
		            memset(buf, 0, 100);
		            continue; //Passa alla prossima iterazione
		         }
		      }
		      else
		         stdinNeedsToBeCleaned = 1; //Non se ne occuperà il thread che legge se si vuole annullare la ricerca
		      
		      char * opponentNick = (char*)malloc(sizeof(char)*21);
		      memset(opponentNick, 0, 21);
		      
		      //Richiesta nickname avversario trovato
		      char request[2];
		      request[0] = OPPONENT_NICKNAME_REQUEST;
		      
		      if(write(sd, request, 1) < 1)
		      {
		         perror("write");
		         exit(1);
		      }
		      
		      if(read(sd, opponentNick, 20) <= 0)
		      {
		         perror("read");
		         exit(1);
		      }
		      
		      if(stdinNeedsToBeCleaned)
   		      while ((c = getchar()) != '\n' && c != EOF) {} //clean stdin
		         
	         printf("\nLa partita è iniziata, stai giocando contro: %s\n\n", opponentNick);
	         
	         free(opponentNick);
	         
	         if(strcmp(buf, FIRST_CLIENT_TO_PLAY_MSG) == 0) //Comincio io il turno
	         {		            
	            //Stampa matrice prima della mia mossa
	            printMatrix();
	            
	            printf("\n\nInserire un numero da 1 a 9 per fare una mossa: ");
	            if(!selectAndSendYourMove('O', sd))
	            {
	               printf("\nIl tuo avversario si è disconnesso, partita terminata.\n\n");
                  printf("Premere un tasto per tornare indietro...");
		            getchar();
		            getchar();
		            printf("\n\n");
	            }
	            else
	            {
	               //Stampa matrice dopo la mia mossa
	               printf("\n");
	               printMatrix();
	               
	               printf("\n\nIn attesa della mossa dell'avversario...\n");
	               memset(buf, 0, 100);
	               
	               play(sd, buf, 'X', 'O');
	            }
	         }
	         else
	         {
	            printf("In attesa della mossa dell'avversario...\n\n");
	            
	            memset(buf, 0, 100);
	            play(sd, buf, 'O', 'X');
	         }
		      break;
		   }
		   
		   case 2: 
		   {
		      printf("\n			+---------------------------+");
				printf("\n		  	|        CLASSIFICA         |\n");
				printf("			+---------------------------+\n\n");
		      
		      if(write(sd, rankRequest, 1)<1) 
		      {
		         perror("write");
		         exit(1);
		      }
		      
		      //Chiediamo quanti byte occorre leggere per il buffer della classifica
		      int bytesToRead = 0;
		      if(read(sd, &bytesToRead, sizeof(bytesToRead)) < sizeof(bytesToRead)) {
		         perror("write");
		         exit(1);
		      }
		      
		      bytesToRead = ntohl(bytesToRead);
		      
		      //Comunichiamo che è possibile proseguire con la richiesta della classifica
		      if(write(sd, rankRequest, 1)<1) //Usiamo di nuovo rankRequest che contiene il carattere costante RANKING_REQUEST
		      {
		         perror("write");
		         exit(1);
		      }
		      
		      char * rankingBuffer = (char*)malloc(sizeof(char)*bytesToRead);
		      memset(rankingBuffer, 0, bytesToRead);
		      
		      if(readn(sd, rankingBuffer, bytesToRead) <= 0) 
		      {
		         perror("readn");
		         exit(1);
		      }
		      
		      printf("%s", rankingBuffer);
		      printf("\n\n");
		      
		      free(rankingBuffer);
		      
		      printf("		  	+---------------------------+\n\n");
		      printf("Premere un tasto qualsiasi per tornare al Menù...");
		      while ((c = getchar()) != '\n' && c != EOF) {}
		      getchar();
		      
		      printf("\n");
		      break;
		   }
		   
		   case 3: 
		   {
		      printf("\n		   +----------------------------------+");
				printf("\n		   |     DISCONNESSIONE EFFETTUATA.   |\n");
				printf("		   |           ALLA PROSSIMA!         |\n");
				printf("		   +----------------------------------+\n\n");
		      
		      //Comunicazione al server della disconnessione 
		      char * disconnect = (char*)malloc(sizeof(char)*2);
		      disconnect[0] = CLIENT_DISCONNECTED_REQUEST;

		      if(write(sd, disconnect, 1) != 1) 
		      {
		         perror("write");
		         exit(1);
		      }
		      
		      close(sd);
         	return 0;
		   }
		   
		   default: 
		   {
		      printf("\nERRORE - Inserire un'opzione valida tra quelle elencate:\n\n");
		      break;
		   }
		}
	}
	
	close(sd);
	return 0;
}

/**********************************************************************/

void* stopNewGameSearch(void* arg) {
   int sd = *((int*)arg);
   int choice, c;
   free(arg);
   
   while(1) 
   {
      if (scanf("%d",&choice) == 1) 
      {
          if(choice == 1)
          {        
             printf("\n");  
             char buffer[2];
             buffer[0] = ABORT_FIND_MATCH_REQUEST;  
             
             //Comunicazione al server dell'annullamento
             if(write(sd, buffer, 1) < 1)
             {
                perror("writen");
                exit(1);
             }
             
             break;
          }
      }
      else
          while ((c = getchar()) != '\n' && c != EOF) {} //clean stdin
   }
   
   return NULL;
}

/**********************************************************************/
	
//Restituisce 0 se la mossa non è stata inserita, 1 altrimenti
int addMoveToMatrix(char movePos, char symbol) 
{
   switch (movePos) 
   {
      case '1': 
      {
         if(trisMatrix[0][0] == 'X' || trisMatrix[0][0] == 'O')
            return 0;
            
         trisMatrix[0][0] = symbol;
         break;
      }
      
      case '2': 
      {
         if(trisMatrix[0][1] == 'X' || trisMatrix[0][1] == 'O')
            return 0;
            
         trisMatrix[0][1] = symbol;
         break;
      }
      
      case '3': 
      {
         if(trisMatrix[0][2] == 'X' || trisMatrix[0][2] == 'O')
            return 0;
            
         trisMatrix[0][2] = symbol;
         break;
      }
      
      case '4': 
      {
         if(trisMatrix[1][0] == 'X' || trisMatrix[1][0] == 'O')
            return 0;
            
         trisMatrix[1][0] = symbol;
         break;
      }
      
      case '5': 
      {
         if(trisMatrix[1][1] == 'X' || trisMatrix[1][1] == 'O')
            return 0;
            
         trisMatrix[1][1] = symbol;
         break;
      }
      
      case '6': 
      {
         if(trisMatrix[1][2] == 'X' || trisMatrix[1][2] == 'O')
            return 0;
         
         trisMatrix[1][2] = symbol;
         break;
      }
      
      case '7': 
      {
         if(trisMatrix[2][0] == 'X' || trisMatrix[2][0] == 'O')
            return 0;
            
         trisMatrix[2][0] = symbol;
         break;
      }
      
      case '8': 
      {
         if(trisMatrix[2][1] == 'X' || trisMatrix[2][1] == 'O')
            return 0;
            
         trisMatrix[2][1] = symbol;
         break;
      }
      
      case '9': 
      {
         if(trisMatrix[2][2] == 'X' || trisMatrix[2][2] == 'O')
            return 0;
            
         trisMatrix[2][2] = symbol;
         break;
      }
   }
   return 1;
}

void printMatrix() 
{
   for(int t=0; t<3; t++) 
   {
       printf(" %c | %c | %c ", trisMatrix[t][0],
               trisMatrix[t][1], trisMatrix[t][2]);
               
       if(t!=2) 
          printf("\n-----------\n");
   }
}

/**********************************************************************/

//Restituisce 1 se la mossa è stata inviata al server, 0 altrimenti
int selectAndSendYourMove(char mySymbol, int sd) 
{
   int choice, c;         
   char buf[20];
   
   while(1) 
   {
      if (scanf("%d",&choice) != 1) 
      {
          while ((c = getchar()) != '\n' && c != EOF) {} //clean stdin
          printf("\nSi prega di inserire un numero: ");
      }
      else if(choice < 1 || choice > 9)
          printf("\nSi prega di inserire un numero compreso tra 1 e 9: ");
      else 
      {
          sprintf(buf, "%d", choice);
          if(addMoveToMatrix(buf[0], mySymbol)) 
          {
             //write della mia mossa..
             if(write(sd, buf, 1) < 1) 
             {
                perror("write");
                exit(1);
             }
             
             memset(buf, 0, 20);
             int n;
             
             if((n=read(sd, buf, 20))<=0 || strcmp(buf, MOVE_INSERTED_MSG) != 0) 
             {
                if(n <= 0) 
                {
                   perror("read");
                   exit(1);
                }
                else
                {
                   if(strcmp(buf, CONNECTION_LOST_MSG) == 0)
                      return 0;
                   else 
                   {
                      printf("\n\nUnknown command\n\n");
                      exit(1);
                   }
                }
             }
             break;   
          }
          else
             printf("\nERRORE - La posizione da te selezionata è già occupata, riprova: ");
      }
   }
   return 1;
}

/**********************************************************************/

void play(int sd, char buf[], char opponentSymbol, char mySymbol) 
{
   int n;
   int movesCnt = 0;
   
   if(opponentSymbol == 'X')
      movesCnt++;
   
   while((n=read(sd, buf, 100)) > 0)
   {
      clean_stdin();
      if(strcmp(buf, CONNECTION_LOST_MSG) == 0) 
      {
         printf("\nIl tuo avversario si è disconnesso, partita terminata.\n\n");
         printf("Premere un tasto per tornare indietro...");
         
         getchar();
		   printf("\n\n");
         break;
      }
      
      addMoveToMatrix(buf[0], opponentSymbol);
      movesCnt++;
      
      if(movesCnt >= 5) //La partita potrebbe essere terminata
      {
         if(checkResult(movesCnt, opponentSymbol) == 1) //L'avversario ha vinto la partita 
         {
            printf("\n");
            printMatrix();
            
            memset(buf, 0, 100);
            buf[0] = MATCH_IS_OVER_REQUEST;
            buf[1] = '0';
            
            if(write(sd, buf, 2) < 2) 
            {
               perror("write");
               exit(1);
            }
            
            printf("\n\nPeccato - hai perso la partita, andrà meglio la prossima volta!\n\n");
            printf("Premere un tasto per tornare indietro...");
            int c;
		      while ((c = getchar()) != '\n' && c != EOF) {}
		      getchar();
		      
		      printf("\n");
            break;
         }
         else if(checkResult(movesCnt, opponentSymbol) == 2) //La partita è terminata in pareggio
         {
            printf("\n");
            printMatrix();
            
		      
            memset(buf, 0, 100);
            buf[0] = MATCH_IS_OVER_REQUEST;
            buf[1] = CLIENT_MATCH_TIE;
            
            if(write(sd, buf, 2) < 2) 
            {
               perror("write");
               exit(1);
            }
            
            printf("\n\nLa partita è terminata in pareggio, puoi fare di meglio!\n\n");
            printf("Premere un tasto per tornare indietro...");
            int c;
		      while ((c = getchar()) != '\n' && c != EOF) {}
		      getchar();
		      printf("\n");
		      
            break;
         }
      }
         
      //Stampa matrice prima della mia mossa
      printf("\n");
      printMatrix();
      
      printf("\n\nInserire un numero da 1 a 9 per fare una mossa: ");
      
      if(!selectAndSendYourMove(mySymbol, sd)) 
      {
         printf("\nIl tuo avversario si è disconnesso, partita terminata.\n\n");
         printf("Premere un tasto per tornare indietro...");
         int c;
		   while ((c = getchar()) != '\n' && c != EOF) {}
		   getchar();
		   printf("\n");
         break;
      }
      
      movesCnt++;
      
      if(movesCnt >= 5) //La partita potrebbe essere terminata
      {
         if(checkResult(movesCnt, mySymbol) == 1) //Ho vinto la partita 
         {
            printf("\n");
            printMatrix();
		      
            memset(buf, 0, 100);
            buf[0] = MATCH_IS_OVER_REQUEST;
            buf[1] = CLIENT_WON_MATCH;
            
            if(write(sd, buf, 2) < 2) 
            {
               perror("write");
               exit(1);
            }
            
            printf("\n\nCongratulazioni, hai vinto la partita!\n\n");
            printf("Premere un tasto per tornare indietro...");
            int c;
		      while ((c = getchar()) != '\n' && c != EOF) {}
		      getchar();
		      printf("\n");
            break;
         }
         else if(checkResult(movesCnt, opponentSymbol) == 2) //La partita è terminata in pareggio
         {
            printf("\n");
            printMatrix();
		      
            memset(buf, 0, 100);
            buf[0] = MATCH_IS_OVER_REQUEST;
            buf[1] = CLIENT_MATCH_TIE;
            
            if(write(sd, buf, 2) < 2) 
            {
               perror("write");
               exit(1);
            }
            
            printf("\n\nLa partita è terminata in pareggio, puoi fare di meglio!\n\n");
            printf("Premere un tasto per tornare indietro...");
            int c;
		      while ((c = getchar()) != '\n' && c != EOF) {}
		      getchar();
		      printf("\n");
		      
            break;
         }
      }
      
      //Stampa nuova matrice
      printf("\n");
      printMatrix();
      
      printf("\n\nIn attesa della mossa dell'avversario...\n");
      
      memset(buf, 0, 100); //Prima della prossima read
   }
   
   if(n <= 0) 
   {
      perror("read");
      exit(1);
   }
}

/**********************************************************************/

/* Restituisce 0 se la partita non è terminata, 1 se la partita  
   è terminata con un vincitore, 2 se terminata con pareggio */
int checkResult(int movesCnt, char symbol) 
{
   if(trisMatrix[0][0]==symbol && trisMatrix[0][0]==trisMatrix[0][1] 
      && trisMatrix[0][1]==trisMatrix[0][2]) //tutta la 1a riga
      return 1;
      
   if(trisMatrix[1][0]==symbol && trisMatrix[1][0]==trisMatrix[1][1] 
      && trisMatrix[1][1]==trisMatrix[1][2]) //tutta la 2a riga
      return 1;
      
   if(trisMatrix[2][0]==symbol && trisMatrix[2][0]==trisMatrix[2][1] 
      && trisMatrix[2][1]==trisMatrix[2][2]) //tutta la 3a riga
      return 1;
      
   if(trisMatrix[0][0]==symbol && trisMatrix[0][0]==trisMatrix[1][0] 
      && trisMatrix[1][0]==trisMatrix[2][0]) //tutta la 1a colonna
      return 1;
      
   if(trisMatrix[0][1]==symbol && trisMatrix[0][1]==trisMatrix[1][1] 
      && trisMatrix[1][1]==trisMatrix[2][1]) //tutta la 2a colonna
      return 1;
      
   if(trisMatrix[0][2]==symbol && trisMatrix[0][2]==trisMatrix[1][2] 
      && trisMatrix[1][2]==trisMatrix[2][2]) //tutta la 3a colonna
      return 1;
      
   if(trisMatrix[0][0]==symbol && trisMatrix[0][0]==trisMatrix[1][1] 
      && trisMatrix[1][1]==trisMatrix[2][2]) //tutta la diagonale principale
      return 1;
      
   if(trisMatrix[0][2]==symbol && trisMatrix[0][2]==trisMatrix[1][1] 
       && trisMatrix[1][1]==trisMatrix[2][0]) //tutta la diagonale secondaria
      return 1;
      
   if(movesCnt == 9)
      return 2;
      
   return 0;
}
