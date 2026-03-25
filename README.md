# Paroliere Multiplayer in C

Un gioco di parole multiplayer in stile Boggle/Scarabeo implementato in C con architettura client-server TCP/IP. Il server gestisce più giocatori contemporaneamente tramite thread, valida le parole su un dizionario italiano, calcola i punteggi e invia la classifica finale a tutti i client connessi.

## Funzionalità

- Connessione multipla di client tramite socket TCP/IP
- Matrice 4x4 di lettere generata casualmente (con supporto alla lettera "Qu")
- Validazione delle parole su dizionario italiano
- Gestione concorrente dei client tramite thread separati
- Classifica finale inviata a tutti i giocatori a fine partita
- Disconnessione automatica dei giocatori inattivi (opzionale)
- Comunicazione tra client tramite il server
- Memoria condivisa per la comunicazione inter-processo

## Struttura del progetto
```
Paroliere/
├── paroliere_srv.c    # Codice del server
├── paroliere_cl.c     # Codice del client
├── macros.h           # Macro per le system call
├── Makefile           # Script di compilazione
└── dictionary_ita.txt # Dizionario italiano
```

## Compilazione
```bash
make
```

## Avvio

**Server:**
```bash
./paroliere_srv nome_server porta_server [--matrici data_filename] [--durata durata_in_minuti] [--seed rnd_seed] [--diz dizionario] [--disconnessione-dopo tempo_in_minuti]
```

**Client:**
```bash
./paroliere_cl nome_server porta_server
```

## Esempio
```bash
# Avvia il server sulla porta 8080 con partite da 3 minuti
./paroliere_srv localhost 8080 --durata 3

# Connetti un client
./paroliere_cl localhost 8080
```

## Tecnologie

- **Linguaggio:** C
- **Comunicazione:** Socket TCP/IP
- **Concorrenza:** POSIX Threads (pthread)
- **IPC:** Memoria condivisa (shared memory)
- **Build:** Make

## Autore

[Alexandru Boghiu](https://github.com/AlexandruBoghiu)
