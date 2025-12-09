/*
 * server-project.c
 *
 * UDP Server - Template for Computer Networks assignment
 *
 * This file contains the boilerplate code for a UDP server
 * portable across Windows, Linux, and macOS.
 */

#if defined WIN32
#include <winsock.h>
#include <ws2tcpip.h>
#define NO_ERROR 0
#else
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#define closesocket close
extern int strcasecmp(const char*, const char*);
#endif

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "protocol.h"

#define REQUEST_SIZE (sizeof(char) + MAX_CITY_LEN)
#define RESPONSE_SIZE (sizeof(uint32_t) + sizeof(char) + sizeof(float))

void clearwinsock() {
#if defined WIN32
	WSACleanup();
#endif
}

// =========================================================================
// FUNZIONI DI UTILITY E LOGICA DI BUSINESS
// =========================================================================

// FUNZIONI METEO
float get_temperature(void) { return ((float)(rand() % 501) - 100) / 10.0; }
float get_humidity(void)    { return 20.0 + (float)(rand() % 801) / 10.0; }
float get_wind(void)        { return (float)(rand() % 1001) / 10.0; }
float get_pressure(void)    { return 950.0 + (float)(rand() % 1001) / 10.0; }

// CITTA' SUPPORTATE e Validazione Caratteri Speciali
int is_valid_city(const char *c) {
    const char *cities[] = {
        "bari", "roma", "milano", "napoli", "torino",
        "palermo", "genova", "bologna", "firenze", "venezia"
    };

    if (strchr(c, '\t') != NULL || strpbrk(c, "@#$%&^*") != NULL) {
        return 0; // Invalid syntax
    }

    for (int i = 0; i < 10; i++)
        if (strcasecmp(c, cities[i]) == 0)
            return 1;

    return 0; // City not found
}

// Deserializza la richiesta
int deserialize_request(const char *buffer, size_t len, weather_request_t *req) {
    if (len < 3) return 0;

    size_t offset = 0;

    // Deserializza type
    memcpy(&req->type, buffer + offset, sizeof(char));
    offset += sizeof(char);

    // Deserializza city
    size_t city_len = len - offset;
    if (city_len > MAX_CITY_LEN) city_len = MAX_CITY_LEN;

    memcpy(req->city, buffer + offset, city_len);
    req->city[MAX_CITY_LEN - 1] = '\0';

    return 1;
}

// Serializza la risposta
size_t serialize_response(const weather_response_t *res, char *buffer) {
    size_t offset = 0;

    // 1. Serializza status (NBO)
    uint32_t net_status = htonl(res->status);
    memcpy(buffer + offset, &net_status, sizeof(uint32_t));
    offset += sizeof(uint32_t);

    // 2. Serializza type
    memcpy(buffer + offset, &res->type, sizeof(char));
    offset += sizeof(char);

    // 3. Serializza value (NBO)
    uint32_t net_value;
    memcpy(&net_value, &res->value, sizeof(float));
    net_value = htonl(net_value);
    memcpy(buffer + offset, &net_value, sizeof(float));
    offset += sizeof(float);

    return offset;
}

// Log e DNS (Reverse Lookup)
void log_request(const struct sockaddr_in *client_addr, char type, const char *city) {
    char client_name[64] = {0};
    char client_ip[INET_ADDRSTRLEN] = {0};

    getnameinfo((struct sockaddr*)client_addr, sizeof(struct sockaddr_in),
                client_ip, INET_ADDRSTRLEN, NULL, 0, NI_NUMERICHOST);

    // Ottieni il nome host canonico
    if (getnameinfo((struct sockaddr*)client_addr, sizeof(struct sockaddr_in),
                    client_name, 64, NULL, 0, 0) != 0) {
        strcpy(client_name, client_ip);
    }

    // Output conforme
    printf("Richiesta ricevuta da %s (ip %s): type='%c', city='%s'\n",
           client_name, client_ip, type, city);
}


// =========================================================================
// MAIN LOGIC
// =========================================================================

int main(int argc, char *argv[]) {

	// Inizializzazione Winsock
#if defined WIN32
	WSADATA wsa_data;
	int result = WSAStartup(MAKEWORD(2,2), &wsa_data);
	if (result != NO_ERROR) {
		printf("Error at WSAStartup()\n");
		return 0;
	}
#endif

    srand(time(NULL));

    int port = SERVER_PORT;

    for (int i = 1; i < argc; i++)
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc)
            port = atoi(argv[++i]);

    // --- 1. Creazione Socket UDP (SOCK_DGRAM) ---
	int my_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (my_socket < 0) {
        perror("Errore nella creazione della socket UDP");
        clearwinsock();
        return EXIT_FAILURE;
    }

	// --- 2. Configurazione e Bind Socket ---
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(my_socket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("Errore nel bind");
        closesocket(my_socket);
        clearwinsock();
        return EXIT_FAILURE;
    }

    printf("Server UDP in ascolto sulla porta %d...\n", port);

	// --- 3. Implementazione Ciclo di Ricezione Datagram UDP ---

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t len = sizeof(client_addr);
        char rx_buffer[REQUEST_SIZE];

        // Riceve datagramma e acquisisce indirizzo client con recvfrom()
        ssize_t bytes_received = recvfrom(my_socket, rx_buffer, REQUEST_SIZE, 0,
                                          (struct sockaddr*)&client_addr, &len);

        if (bytes_received <= 0) {
            continue;
        }

        // --- 4. Deserializzazione Richiesta ---
        weather_request_t req;
        if (!deserialize_request(rx_buffer, bytes_received, &req)) {
            continue;
        }

        // --- 5. Log Richiesta (con DNS Reverse Lookup) ---
        log_request(&client_addr, req.type, req.city);

        // --- 6. Elaborazione Richiesta ---
        weather_response_t res;
        res.status = STATUS_OK;
        res.type = req.type;
        res.value = 0;

        // Validazione 1: Tipo di richiesta
        if (req.type != TYPE_TEMPERATURE &&
            req.type != TYPE_HUMIDITY &&
            req.type != TYPE_WIND &&
            req.type != TYPE_PRESSURE) {

            // Richiesta non valida (tipo non supportato) -> status 2
            res.status = STATUS_INVALID_REQUEST;
            res.type = '\0';
        }
        // Validazione 2: Città supportata (anche controllo su car. speciali)
        else if (!is_valid_city(req.city)) {

            if (strchr(req.city, '\t') != NULL || strpbrk(req.city, "@#$%&^*") != NULL) {
                 res.status = STATUS_INVALID_REQUEST; // Caratteri speciali/tabulazione
            } else {
                 res.status = STATUS_CITY_NOT_FOUND; // Città non nell'elenco
            }
            res.type = '\0';
        }
        else {
            // Generazione dati meteo
            if (req.type == TYPE_TEMPERATURE) res.value = get_temperature();
            else if (req.type == TYPE_HUMIDITY)    res.value = get_humidity();
            else if (req.type == TYPE_WIND)        res.value = get_wind();
            else if (req.type == TYPE_PRESSURE)    res.value = get_pressure();
        }

        // --- 7. Serializzazione e Invio Risposta (sendto) ---
        char tx_buffer[RESPONSE_SIZE];
        size_t res_len = serialize_response(&res, tx_buffer);

        if (sendto(my_socket, tx_buffer, res_len, 0, (struct sockaddr*)&client_addr, len) != res_len) {
            perror("sendto() fallita");
        }
    }

	printf("Server terminated.\n");

	closesocket(my_socket);
	clearwinsock();
	return 0;
} // main end
