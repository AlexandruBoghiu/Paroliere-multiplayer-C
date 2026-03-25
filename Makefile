# Variabili
CC = gcc
CFLAGS = -Wall
LDFLAGS = -pthread

# File sorgente
SERVER_SRC = paroliere_srv.c
CLIENT_SRC = paroliere_cl.c

# File oggetto
SERVER_OBJ = $(SERVER_SRC:.c=.o)
CLIENT_OBJ = $(CLIENT_SRC:.c=.o)

# Obiettivi principali
all: server client

server: $(SERVER_OBJ)
	$(CC) $(CFLAGS) -o server $(SERVER_OBJ) $(LDFLAGS)

client: $(CLIENT_OBJ)
	$(CC) $(CFLAGS) -o client $(CLIENT_OBJ) $(LDFLAGS)

# Pattern rule per compilare i file .o da file .c
%.o: %.c
	$(CC) $(CFLAGS) -c $<

# Pulire i file oggetto e gli eseguibili
clean:
	rm -f $(SERVER_OBJ) $(CLIENT_OBJ) server client
