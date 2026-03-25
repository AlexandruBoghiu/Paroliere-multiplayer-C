#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include "macros.h"
#include <time.h>
#include <sys/select.h>
#include <signal.h>
#include <termios.h>



#define MSG_OK 'K'
#define MSG_ERR 'E'
#define MSG_REGISTRA_UTENTE 'R'
#define MSG_MATRICE 'M'
#define MSG_TEMPO_PARTITA 'T'
#define MSG_TEMPO_ATTESA 'A'
#define MSG_PAROLA 'W'
#define MSG_PUNTI_FINALI 'F'
#define MSG_PUNTI_PAROLA 'P'
#define MSG_CANCELLA_UTENTE 'D'
#define MSG_LOGIN_UTENTE 'L'
#define MSG_POST_BACHECA 'H'
#define MSG_SHOW_BACHECA 'S'
#define MSG_SHUTDOWN 'I'
#define MSG_PAUSA 'G'

#define SIZE 1024
#define N 4
#define MAX_PAROLE 100
#define MAX_LUNGHEZZA 16


char parole_usate[MAX_PAROLE][MAX_LUNGHEZZA];
int num_parole_usate = 0;

pthread_mutex_t lock;
volatile sig_atomic_t pause_flag = 0;
volatile sig_atomic_t shutdown_flag = 0;
volatile sig_atomic_t response_received = 0;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

typedef struct mainInfo {
    pthread_t id;
    int socket;
} mainInfo;

void signal_handler(int sig) {
    if (sig == SIGUSR1) {
        pause_flag = !pause_flag;  // Toggle the pause flag
        printf("\r\033[K");
        
        tcflush(STDIN_FILENO, TCIFLUSH);  // Flush the input buffer
        
        
    } else if (sig == SIGTERM) {
        shutdown_flag = 1;
        tcflush(STDIN_FILENO, TCIFLUSH);  // Flush the input buffer

    }
}

void registrazione(int socket_fd, char *nome);
void loginUtente(int socket_fd, char *nome);
void stampaComandi();
int verificoParola(const char *str);
void stampaMatrice(int matrice[N][N]);

void *listen_thread(void *arg) {
    mainInfo *info = (mainInfo*)arg;
    char type;
    int length, punti;
    char *data;
    int matrice[N][N];
    int tempo_partita, tempo_attesa;

    while (1) {
        if (read(info->socket, &type, 1) <= 0) {
            perror("Errore nella ricezione dal server");
            exit(EXIT_FAILURE);
        }

        // read(info->socket, &length, sizeof(length));

        switch (type) {
            case MSG_OK:
                read(info->socket, &length, sizeof(length));
                if(length != 0){
                    data = (char *)calloc(length + 1, sizeof(char));
                    read(info->socket, data, length);
                    printf("%s\n", data);
                };

                pthread_mutex_lock(&mutex);
                response_received = 1; // Indica che è stata ricevuta una risposta
                pthread_cond_signal(&cond);
                pthread_mutex_unlock(&mutex);

                break;
            case MSG_ERR:
                read(info->socket, &length, sizeof(length));
                if(length != 0){
                    data = (char *)calloc(length + 1, sizeof(char));
                    read(info->socket, data, length);
                    printf("%s\n", data);
                };

                pthread_mutex_lock(&mutex);
                response_received = 1; // Indica che è stata ricevuta una risposta
                pthread_cond_signal(&cond);
                pthread_mutex_unlock(&mutex);

                break;
            case MSG_MATRICE:
                if (!pause_flag){
                    printf("\nMatrice\n");
                    read(info->socket, &length, sizeof(length));
                    read(info->socket, &matrice, length);
                    stampaMatrice(matrice);
                    printf("\nTempo fine partita: %d\n", tempo_partita);

                }
                pthread_mutex_lock(&mutex);
                response_received = 1; // Indica che è stata ricevuta una risposta
                pthread_cond_signal(&cond);
                pthread_mutex_unlock(&mutex);

                break;
            case MSG_PUNTI_PAROLA:
                read(info->socket, &length, sizeof(length));
                if(length != 0){
                    read(info->socket, &punti, length);
                    printf("Parola giusta: +%d punti\n", punti);
                };

                pthread_mutex_lock(&mutex);
                response_received = 1; // Indica che è stata ricevuta una risposta
                pthread_cond_signal(&cond);
                pthread_mutex_unlock(&mutex);

                break;
            case MSG_PAUSA:
                if (pause_flag == 0) {
                    printf("\nPartita finita!!\nInizio Pausa di 1 minuto.\n");
                } else {
                    printf("\nPausa finita!!\nInizio nuova partita!\n");
                }
                
                pthread_kill(info->id, SIGUSR1);
                
                break;
            case MSG_SHUTDOWN:
                pthread_kill(info->id, SIGTERM);
                return NULL;  // Esce dal ciclo e termina il thread
            default:

                printf("\nErore ricevuto type sconosciuto: %c\n", type);
                break;
        }
    }

}



int main(int argc, char *argv[]){
    if (argc < 3) {
        fprintf(stderr, "Parametri insufficienti, minimo 2 parametri\n");
        exit(EXIT_FAILURE);
    };

    char *indirizzo = argv[1];
    int PORT = atoi(argv[2]);
    char *buffer = (char*)calloc(SIZE, sizeof(char));
    char comando[50];
    char data[50];
    char type;
    int length, contatore;
    int matrice [N][N];
    int tempo_partita, tempo_attesa;

    int client_fd, retvalue;
    struct sockaddr_in server_addr;
    struct addrinfo hints, *res;
    mainInfo info;
    pthread_t ascolta_thread;
    
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET; // Forza l'uso di IPv4
    hints.ai_socktype = SOCK_STREAM; // Forza l'uso di TCP

    // Risolve l'indirizzo IP e assegna alla struttura server_addr
    if ((retvalue = getaddrinfo(indirizzo, NULL, &hints, &res)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(retvalue));
        exit(EXIT_FAILURE);
    }
    
    // Preparazione della struttura hints
    struct sockaddr_in *addr_in = (struct sockaddr_in *)res->ai_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr = addr_in->sin_addr;

    freeaddrinfo(res);


    client_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (client_fd == -1) {
        perror("Errore nella creazione del socket");
        exit(EXIT_FAILURE);
    }

    if (connect(client_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("Errore nella connessione al server");
        close(client_fd);
        exit(EXIT_FAILURE);
    }

    info.id = pthread_self();
    info.socket = client_fd;


    while(1){
        // Invio dei commandi diponibili
        write(STDOUT_FILENO, "Comandi disponibili:\n", 21);
        write(STDOUT_FILENO, "1. -> registra_utente nome_utente\n", 35);
        write(STDOUT_FILENO, "2. -> login_utente nome_utente\n", 32);
        write(STDOUT_FILENO, "3. -> fine\n", 12);
        

        while (1) {
            read(STDIN_FILENO, buffer, SIZE);

            // Rimuovi il carattere newline
            buffer[strcspn(buffer, "\n")] = '\0';

            char *token = strtok(buffer, " ");

            if (token != NULL) {
                strncpy(comando, token, sizeof(comando) - 1); // Copia il token nella variabile comando
                comando[sizeof(comando) - 1] = '\0';

                if (strcmp(comando, "fine") != 0) {
                    token = strtok(NULL, " ");

                    if (token != NULL) {
                        strncpy(data, token, sizeof(data) - 1);
                        data[sizeof(data) - 1] = '\0';
                        break;
                    } else {
                        printf("Dato non trovato, riprova.\n");
                    }
                } else {
                    printf("Arrivederci!!\n");
                    type = MSG_SHUTDOWN;
                    write(client_fd, &type, sizeof(type));
                    close(client_fd);
                    exit(EXIT_SUCCESS);
                }
            } else {
                printf("Comando non trovato, riprova.\n");
            }
        }

        if (strcmp(comando, "registra_utente") == 0) {
            registrazione(client_fd, data);
            break;
        } else if (strcmp(comando, "login_utente") == 0) {
            loginUtente(client_fd, data);
            break;
        } else if (strcmp(comando, "fine") == 0) {
            printf("Arrivederci!!\n");
            type = MSG_SHUTDOWN;
            write(client_fd, &type, sizeof(type));
            close(client_fd);
            return 0;
        }
        
    }
    
    pthread_create(&ascolta_thread, NULL, listen_thread, &info);

    
    if (!pause_flag){
        stampaComandi();

        type = MSG_MATRICE;
        length = 0;
        write(client_fd, &type, sizeof(type));
        write(client_fd, &length, sizeof(length));

        pthread_mutex_lock(&mutex);  // Aspetto la risposta
        while (!response_received) {
            pthread_cond_wait(&cond, &mutex);
        }
        response_received = 0;
        pthread_mutex_unlock(&mutex);

        type = MSG_TEMPO_PARTITA;
        write(client_fd, &type, sizeof(type));
        write(client_fd, &length, sizeof(length));

        pthread_mutex_lock(&mutex);  // Aspetto la risposta
        while (!response_received) {
            pthread_cond_wait(&cond, &mutex);
        }
        response_received = 0;
        pthread_mutex_unlock(&mutex);

    }else{
        printf("\nPausa in corso!\n");
        stampaComandi();

        type = MSG_TEMPO_ATTESA;
        length = 0;
        write(client_fd,&type, sizeof(type));
        write(client_fd,&length, sizeof(length));

        pthread_mutex_lock(&mutex);  // Aspetto la risposta
        while (!response_received) {
            pthread_cond_wait(&cond, &mutex);
        }
        response_received = 0;
        pthread_mutex_unlock(&mutex);

    }



    while (1) {

        if(shutdown_flag == 1){
            printf("Server Chiuso\n Arrivederci!\n");

            if(data != NULL){
                free(data);
            };

            if(comando != NULL){
                free(comando);
            };

            close(client_fd);
            exit(EXIT_SUCCESS);
        }

        printf("Client: ");
        
        fflush(stdout);

        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);

        int ret = select(STDIN_FILENO + 1, &fds, NULL, NULL, NULL);

        if (ret > 0){

            if(read(STDIN_FILENO, buffer, SIZE - 1) > 0){

                buffer[strcspn(buffer, "\n")] = '\0';
                char *token = strtok(buffer, " ");

                if (token != NULL) {
                    strncpy(comando, token, sizeof(comando) - 1); // Copia il token nella variabile comando
                    comando[sizeof(comando) - 1] = '\0';

                    if (strcmp(comando, "aiuto") == 0) { // Aiuto
                        stampaComandi();
                        continue;

                    } else if (strcmp(comando, "matrice") == 0) { //Matrice
                    length = 0;
                        if(!pause_flag){    // in caso di Partita
                            type = MSG_MATRICE;

                            if (write(client_fd, &type, sizeof(type)) == -1) {
                                perror("Errore invio tipo messaggio");
                                continue;
                            }
                            write(client_fd, &length, sizeof(length));

                            //aspetto la risopsta
                            
                            type = MSG_TEMPO_PARTITA;
                            write(client_fd, &type, sizeof(type));
                            write(client_fd, &length, sizeof(length));
                            
                        }else{                // in caso di Pausa
                            type = MSG_TEMPO_ATTESA;
                            write(client_fd, &type, sizeof(type));
                            write(client_fd, &length, sizeof(length));
                            //aspetto la risopsta
                        }


                    } else if (strcmp(comando, "tempo_partita") == 0 && !pause_flag) { // Tempo Partita
                        type = MSG_TEMPO_PARTITA;
                        if (write(client_fd, &type, sizeof(type)) == -1) {
                            perror("Errore invio tipo messaggio");
                            continue;
                        }
                        length = 0;
                        write(client_fd, &length, sizeof(length));
                        

                    } else if (strcmp(comando, "p") == 0 && !pause_flag) {  // Parola
                        type = MSG_PAROLA;

                        token = strtok(NULL, " ");
                        if (token != NULL) {
                            strncpy(data, token, sizeof(data) - 1);
                            data[sizeof(data) - 1] = '\0';
                            if (strlen(data) >= 4 || verificoParola(data)){

                                if (write(client_fd, &type, sizeof(type)) == -1) {
                                perror("Errore invio tipo messaggio");
                                continue;
                                }

                                length = strlen(data);

                                if (write(client_fd, &length, sizeof(length)) == -1) {
                                    printf("Errore invio lunghezza");
                                    continue;
                                };

                                if (write(client_fd, data, length) == -1) {
                                    printf("Errore invio parola");
                                    continue;
                                };

                                }
                            }else{
                                printf("Parola non valida, deve avere almeno 4 letter.\n");
                            }
                        } else {
                            printf("Dato non trovato, Riprova.\n");
                        }

                    } else if (strcmp(comando, "punti_finali") == 0) { //Punti Finali
                        type = MSG_PUNTI_FINALI;
                        length = 0;
                        if (write(client_fd, &type, sizeof(type)) == -1) {
                            perror("Errore invio tipo messaggio");
                            continue;
                        };
                        write(client_fd, &length, sizeof(length));
                        

                    } else if (strcmp(comando, "cancella_registrazione") == 0) { // Cancella registrazione
                        type = MSG_CANCELLA_UTENTE;
                        length = 0;

                        printf("Arrivederci!!\n");

                        if (write(client_fd, &type, sizeof(type)) == -1) {
                            perror("Errore invio tipo messaggio");
                        }
                        if (write(client_fd, &length, sizeof(length)) == -1) {
                            perror("Errore invio tipo messaggio");
                        }
                        
                        if(buffer != NULL) {
                            free(buffer);
                        };
                        close(client_fd);
                        exit(EXIT_SUCCESS);

                    } else if (strcmp(comando, "post_bacheca") == 0) {
                        type = MSG_POST_BACHECA;
                        length = 0;
                        if (write(client_fd, &type, sizeof(type)) == -1) {
                            perror("Errore invio tipo messaggio");
                            continue;
                        }
                        write(client_fd, &length, sizeof(length));


                    } else if (strcmp(comando, "show_bacheca") == 0) { // Bacheca
                        type = MSG_SHOW_BACHECA;
                        length = 0;

                        if (write(client_fd, &type, sizeof(type)) == -1) {
                            perror("Errore invio tipo messaggio");
                            continue;
                        }
                        write(client_fd, &length, sizeof(length));


                    } else if (strcmp(comando, "fine") == 0) {
                        printf("Arrivederci!!\n");
                        type = MSG_SHUTDOWN;
                        length = 0;
                        write(client_fd, &type, sizeof(type));
                        write(client_fd, &length, sizeof(length));

                        if(buffer != NULL) {
                            free(buffer);
                        };
                        close(client_fd);
                        exit(EXIT_SUCCESS);

                    } else {
                        printf("Comando non riconosciuto, Riprova.\n");
                        stampaComandi();
                }
            }
        }


        // Attendo la risposta
        pthread_mutex_lock(&mutex);
        while (!response_received) {
            pthread_cond_wait(&cond, &mutex);
        }
        response_received = 0;
        pthread_mutex_unlock(&mutex);
    }

    // Chiusura del socket
    if(buffer != NULL) {
        free(buffer);
    };
    close(client_fd);
    return 0;
}


void registrazione(int socket_fd, char *nome){
    char type = MSG_REGISTRA_UTENTE;
    int length;
    char response;
    char buffer[256];

    // invio solo una volta il tipo del messaggio
    if (write(socket_fd, &type, sizeof(type)) == -1) {
            perror("Errore invio tipo messaggio");
            exit(EXIT_FAILURE);
        }

    printf("Nella Registrazione\n");

    while (1) {
        length = strlen(nome);

        // Invio del nome

        if (write(socket_fd, &length, sizeof(length)) == -1) {
            perror("Errore invio lunghezza nome");
            exit(EXIT_FAILURE);
        }

        if (write(socket_fd, nome, length) == -1) {
            perror("Errore invio nome");
            exit(EXIT_FAILURE);
        }

        // Leggo la risposta dal server
        if (read(socket_fd, &response, sizeof(response)) == -1) {
            perror("Errore lettura risposta");
            exit(EXIT_FAILURE);
        }

        if (response == MSG_OK) {
            printf("Registrazione avvenuta con successo.\n");
            printf("\n");
            break;
        } else if (response == MSG_ERR) {
            printf("Nome non disponibile, Riprovare.\n");
            printf("Mettere solo nome:\n");
            fgets(buffer, sizeof(buffer), stdin);
            buffer[strcspn(buffer, "\n")] = '\0';  // Rimuove il carattere di nuova riga
            strcpy(nome, buffer);
        }
        
    }
    return;
}

void loginUtente(int socket_fd, char *nome){
    char type = MSG_LOGIN_UTENTE;
    int length = strlen(nome);

    if (write(socket_fd, &type, sizeof(type)) == -1) {
            perror("Errore invio tipo messaggio");
            return;
        };

    while(1){
        
        if (write(socket_fd, &length, sizeof(length)) == -1) {
            perror("Errore invio lunghezza nome");
            return;
        }
        if (write(socket_fd, nome, length) == -1) {
            perror("Errore invio nome");
            return;
        }

        if (read(socket_fd, &type, sizeof(type)) == -1) {
            perror("Errore lettura risposta");
            return;
        }
        
        if (type == MSG_OK ) {
            printf("Login effetuato con successo.\n");
            return;

        } else if (type == MSG_ERR) {
            read(socket_fd, &length, sizeof(length));
            char *errorMsg = (char *)calloc(length, sizeof(char));
            read(socket_fd, errorMsg, length);
            printf("%s\n", errorMsg);

            free(errorMsg);
            printf("Riprovare o registerati con quel nome.\nIserisci il nome o scrivi: registrati\n");
            fgets(nome, 100, stdin);

            nome[strcspn(nome, "\n")] = '\0';
            length = strlen(nome); 

            if (strcmp(nome, "registrati") == 0) {
                write(socket_fd, &length, sizeof(length));
                write(socket_fd, nome, length);
                printf("Va nella registrazione\n");
                registrazione(socket_fd, nome);
                return;
            }
            length = strlen(nome);
        }
    }
printf("Finito Login\n");
}

void stampaComandi(){
    if(!pause_flag){
        printf("\nComandi disponibili:\n");
        printf("1. -> aiuto\n");
        printf("2. -> matrice\n");
        printf("3. -> tempo_partita\n");
        printf("4. -> p parola_indicata\n");
        printf("5. -> punti_finali\n");
        printf("6. -> cancella_registrazione\n");
        printf("7. -> fine\n\n");
    }else{
        printf("\nComandi disponibili:\n");
        printf("1. -> aiuto\n");
        printf("2. -> matrice\n");
        printf("3. -> tempo_attesa\n");
        printf("4. -> cancella_registrazione\n");
        printf("5. -> fine\n\n");
    }
}

void stampaMatrice(int matrice[N][N]){
    int i, j;
    for (i=0; i<N; i++){
        for(j = 0; j < N; j++){
            if (matrice[i][j] == 81){
                //printf("Qu");
                fprintf(stdout, "Qu ");
            }else{
                //printf("%c", matrice[i][j]);
                fprintf(stdout, "%c ", matrice[i][j]);
            };
            //printf(" ");
        }
        printf("\n");
    }
}

int verificoParola(const char *str) {

    if (str == NULL) {
        return 0; // Errore: stringa di input NULL
    }

    if (*str == '\0') {
        return 0; // Errore: stringa di input vuota
    }

    for (int i = 0; str[i]; i++) {
        if (!((str[i] >= 'A' && str[i] <= 'Z') || (str[i] >= 'a' && str[i] <= 'z'))) {
            return 0; // Falso: contiene caratteri non alfabetici
        }
    }

    // Controllo se la parola è già stata utilizzata

    for (int i = 0; i < num_parole_usate; i++) {
        if (strcmp(str, parole_usate[i]) == 0) {
            return 0; // Falso: parola già utilizzata
        }
    }

    // Aggiungo la parola all'elenco delle parole utilizzate

    strcpy(parole_usate[num_parole_usate], str);
    num_parole_usate++;
    return 1; // Vero: contiene solo lettere e non è stata utilizzata

}

/*
int gestionePausa(int matrice[N][N], int tempo_partita, int tempo_attesa, int client_fd){
    char type = 'J';
    int punti, length = sizeof(tempo_attesa);
    char *buffer = (char*)calloc(SIZE, sizeof(char));
    char comando[50];
    char data[50];

    // Leggo la Classifica
    read(client_fd, &type, sizeof(type));
    read(client_fd, &length, sizeof(length));
    char *lista = (char *)malloc(length);
    read(client_fd, lista, length);
    printf("Classifica giocatori:\n %s", lista);
    free(lista);
    
    printf("\n");

    printf("Comandi disponibili:\n");
    printf("1. -> aiuto -> per visualizzare \n");
    printf("2. -> matrice\n");
    printf("3. -> tempo_attesa\n");
    printf("4. -> cancella_registrazione\n");
    printf("5. -> msg testo_messaggio -> per scrivere sulla bacheca\n");
    printf("6. -> show-msg -> per visualizzare la bacheca\n");
    printf("7. -> fine -> per uscire dal gioco\n");

    printf("\n");
    
    


    while(1){

        
        read(client_fd, &length, sizeof(length));
        read(client_fd, &tempo_attesa, length);

        
        if (tempo_attesa < 0) {
            printf("Inizio nuova partita\n");
            printf("Matrice:\n");
            read(client_fd, &length, sizeof(length));
            read(client_fd, matrice, length);
            stampaMatrice(matrice);
            printf("\n");
            read(client_fd, &type, sizeof(type));
            if(type == MSG_TEMPO_PARTITA){
                read(client_fd, &length, sizeof(length));
                read(client_fd, &tempo_partita, length);
                printf("Tempo rimasto per la fine della partita: %d\n", tempo_partita);
            }
            return 1;
        }

        // gestione comandi solo durante la pausa

        read(STDIN_FILENO, buffer, SIZE);
        
        buffer[strcspn(buffer, "\n")] = '\0';
        char *token = strtok(buffer, " ");

        if(token != NULL){
            strncpy(comando, token, sizeof(comando) - 1); // Copia il token nella variabile comando
            comando[sizeof(comando) - 1] = '\0';

            if (strcmp(comando, "matrice") == 0){
                type = MSG_MATRICE;
                length = 0;

                printf("Matrice non disponible\n");
                write(client_fd, &type, sizeof(type));
                write(client_fd, &length, sizeof(length));

            }else if (strcmp(comando, "tempo_attesa") == 0){
                type = MSG_TEMPO_ATTESA;
                length = 0;
                write(client_fd, &type, sizeof(type));
                write(client_fd, &length, sizeof(length));


            }else if (strcmp(comando, "punti_finali") == 0){
                type = MSG_PUNTI_FINALI;
                length = 0;
                write(client_fd, &type, sizeof(type));
                write(client_fd, &length, length)

            }else if(strcmp(comando, "cancella_registrazione") == 0){
                type = MSG_CANCELLA_UTENTE;
                length = 0;
                printf("Arrivederci!!\n");
                write(client_fd, &type, sizeof(type));
                write(client_fd, &length, sizeof(length));
                return 0;

            }else if(strcmp(comando, "msg") == 0){
                type = MSG_POST_BACHECA;
                token = strtok(NULL, " ");
                if (token != NULL){
                    strncpy(data, token, sizeof(data) - 1);
                    data[sizeof(data) - 1] = '\0';

                    write(client_fd, &type, sizeof(type));

                    length = strlen(data);
                    write(client_fd, &length, sizeof(length));
                    write(client_fd, data, length);

                    //Aspetto la risposta del server

                    printf("Messaggio postato!\n");
                    
                }else{
                    printf("Manca il messaggio, Riprovare.\n");
                }
            }else if(strcmp(comando, "show-msg") == 0){
                // Vedere la bacheca
                type = MSG_SHOW_BACHECA;
                length = 0;
                write(client_fd, &type, sizeof(type));
                write(client_fd, &length, sizeof(length));
                
                

            }else if(strcmp(comando, "fine") == 0){
                printf("Arrivederci!!\n");
                type = MSG_SHUTDOWN;
                length = 0;
                write(client_fd, &type, sizeof(type));
                write(client_fd, &length, sizeof(length));
                return 0;

            }else if (strcmp(comando, "aiuto" ) == 0){
                printf("Comandi disponibili:\n");
                printf("1. -> aiuto -> per visualizzare \n");
                printf("2. -> matrice\n");
                printf("3. -> tempo_attesa\n");
                printf("4. -> punti_finali\n");
                printf("5. -> cancella_registrazione\n");
                printf("6. -> msg testo_messaggio -> per scrivere sulla bacheca\n");
                printf("7. -> show-msg -> per visualizzare la bacheca\n");
                printf("8. -> fine -> per uscire dal gioco\n");

            }else{
                printf("Comando non riconosciuto\n");
            }
        }else{
            printf("Riprova!\n");
        }
    }
}
*/

