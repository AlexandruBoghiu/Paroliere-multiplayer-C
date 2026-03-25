#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "macros.h"
#include <errno.h>
#include <pthread.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>
#include <sys/un.h>
#include <getopt.h>
#include <stdbool.h>
#include <select.h>



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


#define LOG_FILE "log.txt"

#define MAX 32
#define N 4
#define BUFFER_SIZE 1024
#define MAX_MESSAGES 8
volatile sig_atomic_t pause_flag = 0;
volatile sig_atomic_t shutdown_flag = 0;
pthread_mutex_t matrice_lock = PTHREAD_MUTEX_INITIALIZER;




typedef struct Messaggio{
    char type;
    int length;
    char *data;
}Messaggio;

typedef struct Bacheca{
    char *nome;
    char messaggio[];

}Bacheca;

typedef struct Partita {
    int matrice[N][N]; // Matrice 4x4
    int tempo_partita;  // Tempo di partita
    int seed; // Seed
    int rand;
    char *file;
} Partita;

typedef struct Cliente{
    int socket;
    char *nome;
    int punti;
    int online; // se è 1 il cliente è attivo se è 0 no
    pthread_t thread_id;  
    struct memCondivisa *memoria;
    
}Cliente;


typedef struct memCondivisa{
    Cliente Clienti[MAX];
    Partita partita;
    pthread_mutex_t mutexPers;
    pthread_mutex_t mutexFile;
    pthread_mutex_t mutex_bacheca;
    char *dizionario;
    int disconnessione;
    Bacheca bacheca[MAX_MESSAGES];
    int bacheca_count;
}memCondivisa;

void generaMatrice(int s, memCondivisa *mem);
void generaMf(FILE *fp, int matr[N][N]);
int cercoPosto(memCondivisa *mem);
int confronta_punti(const void *a, const void *b);
int registra(Cliente *cliente, char *data);
bool esisteParola(int matrice[N][N], const char* parola);
void loginUtente(Cliente *cliente);
int parolaNelDiz(const char *str);
void inizio(Cliente *cliente);
int gestionePausa(Cliente *cliente);

void post_bacheca(Cliente *cliente, char *mess);
void show_bacheca(Cliente *cliente);
const char* getTimestamp() {
    time_t now = time(NULL);
    return ctime(&now);
}
void signal_handler(int sig) {

    if (sig == SIGUSR1) {
        pause_flag = !pause_flag;  // Toggle the pause flag

        if (pause_flag) {
            printf("Programma in pausa.\n");
        } else {
            printf("Programma ripreso.\n");
        }
        
        
    } else if (sig == SIGTERM) {

        shutdown_flag = 1; 
    
    }

}



void *threadPart(void *arg){
    memCondivisa *mem = (memCondivisa*)arg;
    time_t start_time = time(NULL); // Tempo di inizio 
    int t_partita = mem->partita.tempo_partita * 60;  // Tempo durata partita

    pthread_mutex_lock(&matrice_lock);
    generaMatrice(mem->partita.seed, mem);
    thread_mutex_unlock(&matrice_lock);


    while(1){
        time_t c_time = time(NULL);
        double t_trasc = difftime(c_time, start_time); // Tempo trascorso nella partita

        mem->partita.t_finePartita = t_partita - t_trasc;

        if(mem->partita.t_finePartita <= 0){
            printf("\nPartita in pausa\n");

            for (int i = 0; i < MAX; i++) {
                if (mem->Clienti[i].online) {
                    pthread_kill(mem->Clienti[i].thread_id, SIGUSR1);
                }
            }

            // Mertto in pausa per 60 sec
            time_t ora = time(NULL);
            int t_pausa = 60;
            
        
            while(1){

                time_t f_ora = time(NULL);
                double t_rimasto = difftime(f_ora, ora); // Tempo trascorso nella pausa

                mem->partita.t_nextPartita = t_pausa - t_rimasto;

                if (mem->partita.t_nextPartita <= 0){
                    
                    // lock sulla matrice
                    pthread_mutex_lock(&matrice_lock);
                    generaMatrice(mem->partita.seed, mem);
                    pthread_mutex_unlock(&matrice_lock);

                    for (int i = 0; i < MAX; i++) {
                        if (mem->Clienti[i].online) {
                            pthread_kill(mem->Clienti[i].thread_id, SIGUSR1);
                        }
                    };

                    start_time = time(NULL);  // Reset del timer
                    printf("Pausa finita\n");
                    break;
                }
            }

        }

    }
    return NULL;
}

void *Scorer(void *arg) {
    memCondivisa *mem = (memCondivisa *)arg;
    int i, length, j;
    char type;
    
    while (1) {
        if (mem->partita.t_finePartita <= 0) {
            pthread_mutex_lock(&mem->mutexPers);

            j = 0;
            // Conta il numero di client online
            for (i = 0; i < MAX; i++) {
                if (mem->Clienti[i].online == 1) {
                    j++;
                }
            }

            if (j > 0) {
                // Alloca memoria per la lista di client online
                Cliente *temp = (Cliente *)malloc(j * sizeof(Cliente));
                if (temp == NULL) {
                    perror("Errore di allocazione memoria");
                    pthread_mutex_unlock(&mem->mutexPers);
                    sleep(1);
                    continue;
                }

                j = 0;
                // Riempie la lista con client online
                for (i = 0; i < MAX; i++) {
                    if (mem->Clienti[i].online == 1) {
                        temp[j] = mem->Clienti[i];
                        j++;
                    }
                }

                // Ordina la lista in base ai punti
                qsort(temp, j, sizeof(Cliente), confronta_punti);

                // Alloca memoria per il buffer della lista finale
                char *lista = (char *)malloc(BUFFER_SIZE);
                if (lista == NULL) {
                    perror("Errore di allocazione memoria per lista");
                    free(temp);
                    pthread_mutex_unlock(&mem->mutexPers);
                    sleep(1);
                    continue;
                }
                lista[0] = '\0'; // Assicurati che il buffer sia inizializzato

                for (i = 0; i < j; i++) {
                    char buffer[BUFFER_SIZE];
                    snprintf(buffer, sizeof(buffer), "%d. %s: %d\n", i + 1, temp[i].nome, temp[i].punti);
                    strncat(lista, buffer, BUFFER_SIZE - strlen(lista) - 1); // Evita overflow
                }


                length = strlen(lista);
                type = MSG_PUNTI_FINALI;
                for (i = 0; i < j; i++) {
                    write(temp[i].socket, &type, sizeof(type));
                    write(temp[i].socket, &length, sizeof(length));
                    write(temp[i].socket, lista, length);
                }

                free(lista); // Libera la memoria allocata per la lista
                free(temp);  // Libera la memoria allocata per i client

                sleep(60);
            }

            pthread_mutex_unlock(&mem->mutexPers);
        }
        sleep(1);
    }
}


void *threadCl(void *arg){
    Cliente *cliente = (Cliente*)arg;
    Messaggio msg;
    

    while(1){

        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(client->fd, &read_fds);

        struct timeval timeout;
        struct timeval *timeout_ptr = NULL;

        // Configurazione del timeout in base al valore di inactivity_timeout
        if (cliente->memoria->disconnessione != -1) {
            timeout.tv_sec = cliente->memoria->disconnessione;
            timeout.tv_usec = 0;
            timeout_ptr = &timeout;
        }

        // pthread_mutex_lock(&cliente->memoria->mutexPers);  // servirebbe mutex per chi scrive e non leggere

        // Se inactivity_timeout è -1 (NO_TIMEOUT), select attenderà indefinitamente.
        int activity = select(cliente->socket + 1, &read_fds, NULL, NULL, timeout_ptr);

        if (activity < 0) {
            // La connessione è chiusa o c'è un errore di lettura
            printf("Connessione chiusa per il client socket %d\n", cliente->socket);
            pthread_mutex_unlock(&cliente->memoria->mutexPers);
            close(cliente->socket);
            pthread_exit(NULL);
        }else if(activity == 0 && cliente->memoria->disconnessionet != -1){

        }
        
        // Controllo il tipo del messaggio
        
        switch (msg.type){
        
            case MSG_REGISTRA_UTENTE: //Registra
                read(cliente->socket, &msg.length, sizeof(msg.length));
                msg.data = (char *)calloc(msg.length + 1, sizeof(char));
                read(cliente->socket, msg.data, msg.length);
                
                    if(registra(cliente, msg.data)){

                        cliente->nome = msg.data;
                        cliente->punti = 0;
                        cliente->online = 1;

                        msg.type = MSG_OK;
                        write(cliente->socket, &msg.type, sizeof(msg.type));
                        inizio(cliente);

                        // Scrivo nel file log la registrazione
                        pthread_mutex_lock(&cliente->memoria->mutexFile);
                        FILE *logFile = fopen(LOG_FILE, "a");
                        fprintf(logFile, "[%s] Registrazione utente: %s\n", getTimestamp(), cliente->nome);
                        fclose(logFile);

                        pthread_mutex_unlock(&cliente->memoria->mutexFile);

                    }else{
                        free(msg.data);
                        close(cliente->socket);
                        pthread_exit(NULL);
                    }
                    

                break;
            case MSG_MATRICE:  //Matrice
                

                break;
            
            case MSG_PAROLA:  //Parola
                if (read(cliente->socket, &msg.length, sizeof(msg.length)) == -1) {
                    perror("Errore lettura lunghezza parola");
                    break;
                }

                msg.data = (char *)calloc(msg.length + 1, sizeof(char));
                if (read(cliente->socket, msg.data, msg.length) == -1) {
                    perror("Errore lettura parola");
                    free(msg.data);
                    break;
                }


                if(parolaNelDiz(msg.data)){

                    pthread_mutex_lock(&matrice_lock);
                    int n = esisteParola(cliente->memoria->partita.matrice, msg.data);
                    pthread_mutex_unlock(&matrice_lock);
                    
                    if (n) {

                        msg.type = MSG_OK;
                        
                        write(cliente->socket, &msg.type, sizeof(msg.type));
                        cliente->punti += msg.length;

                        pthread_mutex_lock(&cliente->memoria->mutexFile);
                        FILE *logFile = fopen(LOG_FILE, "a");
                        fprintf(logFile, "[%s] Parola inviata da %s: %s\n", getTimestamp(), cliente->nome, msg.data);
                        fclose(logFile);
                        pthread_mutex_unlock(&cliente->memoria->mutexFile);
                        break;
                         
                    } else {

                        char buf[35] = "Parola non trovata nella matrice!!";
                        msg.type = MSG_ERR;
                        write(cliente->socket, &msg.type, sizeof(msg.type));

                        msg.length = strlen(buf);
                        write(cliente->socket, &msg.length, sizeof(msg.length));
                        write(cliente->socket, buf, sizeof(buf));
                    
                    
                    break;
                    };

                }else{

                    char buf[22] = "Parola non esistente";
                    msg.type = MSG_ERR;
                    write(cliente->socket, &msg.type, sizeof(msg.type));
                    msg.length = 22;
                    write(cliente->socket, &msg.length, sizeof(msg.length));
                    write(cliente->socket, buf, sizeof(buf));
                }
                
                break;

            case MSG_CANCELLA_UTENTE:   //Cancalla Utente
                    cliente->nome = NULL;
                    cliente->punti = 0;
                    cliente->online = 0;
                    cliente->socket = 0;

                    pthread_mutex_lock(&cliente->memoria->mutexFile);
                    FILE *logFile = fopen(LOG_FILE, "a");
                    fprintf(logFile, "[%s] Cancellazione utente: %s\n", getTimestamp(), cliente->nome);
                    fclose(logFile);
                    pthread_mutex_unlock(&cliente->memoria->mutexFile);

                    
                    pthread_mutex_unlock(&cliente->memoria->mutexPers);
                    close(cliente->socket);
                    pthread_exit(NULL);

                break;

            case MSG_LOGIN_UTENTE:   //Login Utente
                loginUtente(cliente);
                inizio(cliente);
                break;

            case MSG_SHUTDOWN:   //Exit

                cliente->online = 0;
                pthread_mutex_unlock(&cliente->memoria->mutexPers);
                close(cliente->socket);
                pthread_exit(NULL);
            
            case MSG_TEMPO_PARTITA: // Tempo Partita
                if (cliente->memoria->partita.t_finePartita > 0){ //Sono ancora nella partita
                    msg.type = MSG_OK;
                    msg.length = sizeof(cliente->memoria->partita.t_finePartita);

                    write(cliente->socket, &msg.type, sizeof(msg.type));
                    write(cliente->socket, &msg.length, sizeof(msg.length));
                    write(cliente->socket, &cliente->memoria->partita.t_finePartita, msg.length);
                }else{ // Partita finita
                    msg.type = MSG_ERR;
                    char buf[16] = "Partita finita!";
                    msg.length = strlen(buf);

                    write(cliente->socket, &msg.type, sizeof(msg.type));
                    write(cliente->socket, &msg.length, sizeof(msg.length));
                    write(cliente->socket, buf, sizeof(buf));

                    pthread_mutex_unlock(&cliente->memoria->mutexPers);
                    sleep(1);
            
                    if(gestionePausa(cliente)){
                        pthread_mutex_unlock(&cliente->memoria->mutexPers);
                        close(cliente->socket);
                        pthread_exit(NULL);
                    };
                    cliente->punti = 0;
                    pthread_mutex_lock(&cliente->memoria->mutexPers);

                    }
                break;
            default:
                printf("Errore\n");
                printf("Ricevuto:  |%c|\n", msg.type);
                
                break;
        }

        cliente->ultimo_accesso = time(NULL);
        pthread_mutex_unlock(&cliente->memoria->mutexPers);


        
    };
    
    
    if (msg.data != NULL){
        free(msg.data);
    }
    close(cliente->socket);
    pthread_exit(NULL);
}


/*void *threadD(void *arg) {
    
    memCondivisa *mem = (memCondivisa *)arg;
    int disconnetti_dopo = mem->disconnessione * 60;  // Tempo di inattività in secondi prima della disconnessione


    while (1) {
        time_t current_time = time(NULL);
        sleep(1);
        for (int i = 0; i < MAX; i++) {
            if (mem->Clienti[i].socket != 0 && difftime(current_time, mem->Clienti[i].ultimo_accesso) > disconnetti_dopo) {
                close(mem->Clienti[i].socket);
                printf("%d\n", mem->Clienti[i].socket);
                mem->Clienti[i].socket = 0;
                mem->Clienti[i].online = 0;
                printf("Cliente %s disconnesso per inattività\n", mem->Clienti[i].nome);
            }
        }

    }
    return NULL;
}
*/


int main(int argc, char *argv[]){
    int f = 0, PORT = 0;
    int opt;
    int option_index = 0;
    memCondivisa memoria;
    memoria.partita.tempo_partita = 3;
    memoria.partita.seed = time(0);
    memoria.disconnessione = -1;


    pthread_mutex_init(&memoria.mutexPers, NULL);
    pthread_mutex_init(&memoria.mutexFile, NULL);
    pthread_mutex_init(&memoria.mutex_bacheca, NULL);
    pthread_t client[MAX];

    int server_fd, retvalue;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    char *indirizzo = NULL;
    pthread_t tempPar, Scorer_id;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET; 
    hints.ai_socktype = SOCK_STREAM; 

    indirizzo = argv[1];
    PORT = atoi(argv[2]);
    
    

    // Controllo gli argomenti con getopt_long()
    struct option long_options[] = {
        {"matrici", required_argument, 0, 'm'},
        {"durata", required_argument, 0, 'd'},
        {"seed", required_argument, 0, 's'},
        {"diz", required_argument, 0, 'z'},
        {"disconnetti-dopo", required_argument, 0, 'x'},
        {0, 0, 0, 0}
    };


    while ((opt = getopt_long(argc, argv, "m:d::s::z:x:", long_options, &option_index)) != -1) {
        switch (opt) {
            
            case 'm':
                memoria.partita.file = optarg;
                break;

            case 'd':
                if(atoi(optarg) <= 0){
                    printf("\nLa dureata della Partita deve essere un numero positivo e maggiore di 0.\n");
                    exit(EXIT_FAILURE);
                }
                memoria.partita.tempo_partita = atoi(optarg);
                break;

            case 's':
                    memoria.partita.seed = atoi(optarg);
                    memoria.partita.rand = 0;
                break;

            case 'z':
                memoria.dizionario = optarg;
                break;

            case 'x':
                if(atoi(optarg) <= 0){
                    printf("\nIl timeout di inattività deve essere un numero positivo e maggiore di 0.\n");
                    exit(EXIT_FAILURE);
                }
                memoria.disconnessione = atoi(optarg);
                //pthread_t tempD;
                //pthread_create(&tempD, NULL, threadD, &memoria);
                break;

            case '?':
                write(STDOUT_FILENO,"Parametro non valido", 21);
                exit(EXIT_FAILURE);
                break;
        }
    }

    // Gestione segnale SIGINT (CTRL-C)
    signal(SIGINT, signal_handler);
    

    // Controllo la presenza dell'indirizzo e della PORT
    if (indirizzo == NULL || PORT == 0) {
        fprintf(stderr, "Indirizzo IP e porta devono essere specificati.\n");
        exit(EXIT_FAILURE);
    }
    
    // inizializzazione della str.typeuttura server
    if ((retvalue = getaddrinfo(indirizzo, NULL, &hints, &res)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(retvalue));
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in *addr_in = (struct sockaddr_in *)res->ai_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr = addr_in->sin_addr;

    freeaddrinfo(res);

    SYSC(server_fd, socket(AF_INET, SOCK_STREAM, 0), "nella socket");


    // Binding
    bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr));


    // Listen
    SYSC(retvalue, listen(server_fd, 10), "nella listen");

    for(int g = 0; g < MAX; g++){
        memoria.Clienti[g].nome = "";
        memoria.Clienti[g].socket = 0;
        memoria.Clienti[g].punti = 0;
        memoria.Clienti[g].online = 0;
    };

    // Creazione dei thread Partita e SCore
    pthread_create(&tempPar, NULL, threadPart, &memoria);
    pthread_create(&Scorer_id, NULL, Scorer, &memoria);

    printf("Server ON\n");
    //Accept
    while(1){

        if (shutdown_flag) {
            // Invia MSG_SHUTDOWN a tutti i client attivi e chiudi il server
            printf("Ricevuto CTRL-C. Chiusura del server...\n");

            pthread_mutex_lock(&memoria.mutexPers);  // Blocca l'accesso alla memoria condivisa

            for (int i = 0; i < MAX; i++) {
                if (memoria.Clienti[i].online || memoria.Clienti[i].socket != 0) {
                    char type = MSG_SHUTDOWN;
                    write(memoria.Clienti[i].socket, &type, sizeof(type));  // Invia il messaggio di shutdown
                    close(memoria.Clienti[i].socket);  // Chiudi il socket del client
                    memoria.Clienti[i].online = 0;
                }
            }
            pthread_mutex_unlock(&memoria.mutexPers);  // Sblocca la memoria

            break;  // Esci dal ciclo principale
        }

        f = cercoPosto(&memoria); // cerca posto libero
        if (f == -1) {
                printf("Nessun posto libero disponibile\n");
                sleep(1); // Attende prima di riprovare
                continue;
            }

        memoria.Clienti[f].socket = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
        memoria.Clienti[f].memoria = &memoria;
        memoria.Clienti[f].online = 0;
        pthread_create(&client[f], NULL, threadCl, &memoria.Clienti[f]);
        memoria.Clienti[f].thread_id = client[f]; 
        
    };


    // Chiusura del socket
    for (f = 0; f < MAX; f++){
        SYSC(retvalue, close(client[f]), "nella close");
    };

    SYSC(retvalue, close(server_fd), "nella close");

}


void generaMatrice(int s, memCondivisa *mem) {
    int i, j;
    if (mem->partita.rand){
        s = time(0);
    }
    srand(s);

    if (mem->partita.file == NULL) {
        // Genero la matrice
        for (i = 0; i < N; i++) {
            for (j = 0; j < N; j++) {
                mem->partita.matrice[i][j] = (rand() % 26) + 'A';
            }
        }
    } else {
        FILE *file = fopen(mem->partita.file, "r");
        generaMf(file, mem->partita.matrice);
        fclose(file);
    }
}


void generaMf(FILE *fp, int matr[N][N]){
    int i = 0, j = 0;
    while( 1){
        int n = fgetc(fp);
        if (n == EOF || n == '\n') { 
            break;
        }

        if (n != ' ') { 
            matr[i][j] = n;
             j++;
            if (j == N) { 
                j = 0;
                i++;
                if (i == N) { 
                    break;
                }
            }
        }
    }
}

int cercoPosto(memCondivisa *mem){
    int i = 0;
    while(1){
        if(strcmp(mem->Clienti[i].nome, "") == 0 || mem->Clienti[i].socket == 0){
            return i;
        }
        i++;

        if (i >= MAX){
            i = 0;
        };
        sleep(1);
    };
}

int confronta_punti(const void *a, const void *b) {
    Cliente *giocatoreA = *(Cliente **)a;
    Cliente *giocatoreB = *(Cliente **)b;
    return (giocatoreB->punti - giocatoreA->punti);
}

int registra(Cliente *giocatore, char *name){
    int i;

    while(1){
        if (strcmp(name, "fine") == 0){
            return 0;
        }
        
        for (i = 0; i < MAX; i++){
            if (strcmp(giocatore->memoria->Clienti[i].nome, name) == 0){
                char c = MSG_ERR;
                write(giocatore->socket, &c, sizeof(c));

                int length;
                read(giocatore->socket, &length, sizeof(length));
                char *new_name = (char *)calloc(length + 1, sizeof(char));
                read(giocatore->socket, new_name, length);
                name = new_name;
                break;

            } else if(i == (MAX -1)){
                return 1;
            }
        }
    }

    return 0;
}

bool cercaParola(int matrice[N][N], const char* parola, int r, int c, int pos, bool visitato[N][N]) {
    if (r < 0 || r >= N || c < 0 || c >= N || visitato[r][c]) {
        return false;
    }

    char currentChar = matrice[r][c];
    if (parola[pos] == 'Q' && (pos + 1) < strlen(parola) && parola[pos + 1] == 'u') {
        if (currentChar != 'Q') {
            return false;
        }
        pos += 2;
    } else {
        if (currentChar != parola[pos]) {
            return false;
        }
        pos++;
    }

    if (pos == strlen(parola)) {
        return true;
    }

    visitato[r][c] = true;

    if (cercaParola(matrice, parola, r - 1, c, pos, visitato) ||
        cercaParola(matrice, parola, r + 1, c, pos, visitato) ||
        cercaParola(matrice, parola, r, c - 1, pos, visitato) ||
        cercaParola(matrice, parola, r, c + 1, pos, visitato)) {
        return true;
    }

    visitato[r][c] = false;

    return false;
}

void to_lowercase(char *str) {
    for (int i = 0; str[i]; i++) {
        if (str[i] >= 'A' && str[i] <= 'Z') {
            str[i] += 'a' - 'A';
        }
    }
}

int parolaNelDiz(const char *parola) {
    char *str = calloc(strlen(parola) + 1, sizeof(char));
    strcpy(str, parola);

    FILE *file = fopen("dictionary_ita.txt", "r");
    if (file == NULL) {
        perror("Errore nell'apertura del file");
        free(str);
        return 0;
    }

    to_lowercase(str);

    char line[256];
    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\r\n")] = '\0';
        to_lowercase(line);
        if (strcmp(line, str) == 0) {
            fclose(file);
            free(str);
            return 1;
        }
    }
    fclose(file);
    free(str);
    return 0;
}

bool esisteParola(int matrice[N][N], const char* parola) {
    if (strlen(parola) > N * N) {
        return false;
    }

    bool visitato[N][N];
    memset(visitato, 0, sizeof(visitato));

    for (int r = 0; r < N; r++) {
        for (int c = 0; c < N; c++) {
            if (matrice[r][c] == parola[0] && cercaParola(matrice, parola, r, c, 0, visitato)) {
                return true;
            }
        }
    }

    return false;
}

void loginUtente(Cliente *cliente) {
    int i, length;
    char *data;
    char type;

    while(1) {
        if (read(cliente->socket, &length, sizeof(length)) != sizeof(length)) {
            perror("Failed to read message length");
            return;
        }

        data = (char *)calloc(length + 1, sizeof(char));
        read(cliente->socket, data, length);
        if (data == NULL) {
            perror("Failed to allocate memory for message data");
            return;
        }

        if (strcmp(data, "registrati") == 0) {
            free(data);
            return;
        }

        for (i = 0; i < MAX; i++) {
            if (strcmp(cliente->memoria->Clienti[i].nome, data) == 0) {
                if (cliente->memoria->Clienti[i].online == 1) {
                    const char *errorMsg = "Giocatore già online";
                    int errorMsgLength = strlen(errorMsg);
                    type = MSG_ERR;
                    write(cliente->socket, &type, sizeof(type));
                    write(cliente->socket, &errorMsgLength, sizeof(errorMsgLength)); 
                    write(cliente->socket, errorMsg, errorMsgLength);
                } else {
                    type = MSG_OK;
                    write(cliente->socket, &type, sizeof(type));
                    cliente->memoria->Clienti[i].socket = cliente->socket;
                    cliente->memoria->Clienti[i].online = 1;
                    free(data);
                    return;
                }
            }
        }

        const char *errorMsg = "Utente non trovato";
        int errorMsgLength = strlen(errorMsg);

        type = MSG_ERR;
        write(cliente->socket, &type, sizeof(type));
        write(cliente->socket, &errorMsgLength, sizeof(errorMsgLength));
        write(cliente->socket, errorMsg, errorMsgLength);

        free(data);
    }
}

void inizio(Cliente *cliente) {
    int length = N * N * sizeof(int);
    char type;
    write(cliente->socket, &length, sizeof(length));
    write(cliente->socket, cliente->memoria->partita.matrice, length);

    if (cliente->memoria->partita.t_finePartita <= 0) {
        type = MSG_TEMPO_ATTESA;
        int tempoPartita = cliente->memoria->partita.t_nextPartita;
        length = sizeof(tempoPartita);
        write(cliente->socket, &type, sizeof(type));
        write(cliente->socket, &length, sizeof(length));
        write(cliente->socket, &tempoPartita, sizeof(cliente->memoria->partita.t_nextPartita));
    } else {
        type = MSG_TEMPO_PARTITA;
        write(cliente->socket, &type, sizeof(type));
        int tempoPartita = cliente->memoria->partita.t_finePartita;
        length = sizeof(tempoPartita);
        write(cliente->socket, &length, sizeof(length));
        write(cliente->socket, &tempoPartita, length);
    }
}


int gestionePausa(Cliente *cliente) {

    int length;
    char type;
    int tempo_attesa = cliente->memoria->partita.t_nextPartita;
    char *mess;
    

    while (1) {
        cliente->ultimo_accesso = time(NULL);
        



        length = sizeof(tempo_attesa);
        write(cliente->socket, &length, sizeof(length));
        write(cliente->socket, &tempo_attesa, length);
        
        tempo_attesa = cliente->memoria->partita.t_nextPartita;
        if (tempo_attesa < 0) {
            printf("Pausa finita\n");
            return 0;
        }

        read(cliente->socket, &type, sizeof(type));
        switch (type) {
            case MSG_CANCELLA_UTENTE:
                    cliente->nome = NULL;
                    cliente->punti = 0;
                    cliente->online = 0;
                    cliente->socket = 0;

                close(cliente->socket);
                return 1;

            case MSG_SHUTDOWN:
                cliente->online = 0;
                return 1;

            case MSG_REGISTRA_UTENTE:
                read(cliente->socket, &length, sizeof(length));
                char *data = (char*)calloc(length, sizeof(char));
                read(cliente->socket, data, length);
                registra(cliente, data);
                free(data);
                break;
            case MSG_MATRICE:
                length = sizeof(cliente->memoria->partita.t_nextPartita);
                write(cliente->socket, &length, sizeof(length));
                write(cliente->socket, &cliente->memoria->partita.t_nextPartita, sizeof(cliente->memoria->partita.t_nextPartita));
                break;
            case MSG_TEMPO_PARTITA:
               type = MSG_ERR;
                    char buf[16] = "Partita finita!";
                    length = strlen(buf);

                    write(cliente->socket, &type, sizeof(type));
                    write(cliente->socket, &length, sizeof(length));
                    write(cliente->socket, buf, sizeof(buf));
                break;
            case MSG_TEMPO_ATTESA:
                length = sizeof(tempo_attesa);
                write(cliente->socket, &length, sizeof(length));
                write(cliente->socket, &tempo_attesa, length);
                break;
            case MSG_POST_BACHECA:
                read(cliente->socket, &length, sizeof(length));
                mess = (char*)calloc(length, sizeof(char));
                read(cliente->socket, &mess, length);
                post_bacheca(cliente, mess);
                break;
            
            case MSG_SHOW_BACHECA:
                show_bacheca(cliente);
                break;
            default:
                printf("Comando non riconosciuto %c\n", type);
                
                break;
        }
    }
}

void post_bacheca(Cliente *cliente, char *mess){
    pthread_mutex_lock(&cliente->memoria->mutex_bacheca);

    if (cliente->memoria->bacheca_count == MAX_MESSAGES) {
        // Rimuove il messaggio più vecchio
        for (int i = 1; i < MAX_MESSAGES; i++) {
            cliente->memoria->bacheca[i - 1] = cliente->memoria->bacheca[i];
        }
        cliente->memoria->bacheca_count--;
    }

    // Aggiunge il nuovo messaggio
    Bacheca *new_message = &cliente->memoria->bacheca[cliente->memoria->bacheca_count];
    new_message->nome = cliente->nome;
    strncpy(new_message->messaggio, mess, 128);
    cliente->memoria->bacheca_count++;

    pthread_mutex_unlock(&cliente->memoria->mutex_bacheca);

    
    char type = MSG_OK;
    write(cliente->socket, &type, sizeof(type));

}

void show_bacheca(Cliente *cliente) {
    memCondivisa *memoria = cliente->memoria;
    pthread_mutex_lock(&cliente->memoria->mutex_bacheca);

    char buffer[BUFFER_SIZE];
    int offset = 0;

    for (int i = 0; i < memoria->bacheca_count; i++) {
        Bacheca *msg = &memoria->bacheca[i];
        int written = snprintf(buffer + offset, BUFFER_SIZE - offset, "%s,%s\n", msg->nome, msg->messaggio);
        if (written >= BUFFER_SIZE - offset) {
            break;
        }
        offset += written;
    }

    pthread_mutex_unlock(&memoria->mutex_bacheca);

    int length = strlen(buffer);
    write(cliente->socket, &length, sizeof(length));
    write(cliente->socket, buffer, length);
}   
