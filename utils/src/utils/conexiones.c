#include "conexiones.h"
 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

 
// ============================================================
//  conexiones.c
//  Implementación de las funciones de sockets.
// ============================================================
 
// ------------------------------------------------------------------
// iniciar_servidor
// Crea un socket servidor que escucha en el puerto indicado.
// ------------------------------------------------------------------
int iniciar_servidor(char* puerto) {
    // getaddrinfo nos da la info necesaria para crear el socket
    struct addrinfo hints, *servinfo;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;       // IPv4
    hints.ai_socktype = SOCK_STREAM;   // TCP
    hints.ai_flags    = AI_PASSIVE;    // usar IP local
 
    getaddrinfo(NULL, puerto, &hints, &servinfo);
 
    // Creamos el socket
    int fd_servidor = socket(servinfo->ai_family,
                             servinfo->ai_socktype,
                             servinfo->ai_protocol);
 
    // Permite reusar el puerto si el proceso se reinicia
    int activar = 1;
    setsockopt(fd_servidor, SOL_SOCKET, SO_REUSEADDR, &activar, sizeof(activar));
 
    // Asociamos el socket al puerto
    bind(fd_servidor, servinfo->ai_addr, servinfo->ai_addrlen);
 
    // Empezamos a escuchar (hasta 20 conexiones pendientes en cola)
    listen(fd_servidor, 20);
 
    freeaddrinfo(servinfo);
 
    return fd_servidor;
}
 
// ------------------------------------------------------------------
// esperar_cliente
// Bloquea hasta que alguien se conecta. Devuelve el fd del cliente.
// ------------------------------------------------------------------
int esperar_cliente(int fd_servidor) {
    struct sockaddr_in dir_cliente;
    socklen_t longitud = sizeof(struct sockaddr_in);
 
    int fd_cliente = accept(fd_servidor,
                            (struct sockaddr*) &dir_cliente,
                            &longitud);
    return fd_cliente;
}
 
// ------------------------------------------------------------------
// crear_conexion
// Se conecta a un servidor. Devuelve el fd o -1 si falla.
// ------------------------------------------------------------------
int crear_conexion(char* ip, char* puerto) {
    struct addrinfo hints, *servinfo;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
 
    getaddrinfo(ip, puerto, &hints, &servinfo);
 
    int fd = socket(servinfo->ai_family,
                    servinfo->ai_socktype,
                    servinfo->ai_protocol);
 
    int resultado = connect(fd, servinfo->ai_addr, servinfo->ai_addrlen);
 
    freeaddrinfo(servinfo);
 
    if (resultado == -1) {
        close(fd);
        return -1;
    }
 
    return fd;
}
 
// ------------------------------------------------------------------
// enviar_mensaje
// Envía size bytes del buffer por el socket fd.
// Primero manda el tamaño (4 bytes) y después el contenido.
// ------------------------------------------------------------------
void enviar_mensaje(int fd, void* buffer, int size) {
    // Mandamos primero cuántos bytes vienen
    send(fd, &size, sizeof(int), 0);
    // Después mandamos el contenido
    send(fd, buffer, size, 0);
}
 
// ------------------------------------------------------------------
// recibir_mensaje
// Recibe un mensaje del socket fd.
// Guarda en *size cuántos bytes recibió.
// Devuelve un puntero al buffer recibido (hay que hacer free después).
// ------------------------------------------------------------------
void* recibir_mensaje(int fd, int* size) {
    // Primero leemos cuántos bytes vienen
    recv(fd, size, sizeof(int), MSG_WAITALL);
 
    // Reservamos memoria y leemos el contenido
    void* buffer = malloc(*size);
    recv(fd, buffer, *size, MSG_WAITALL);
 
    return buffer;
}