#ifndef CONEXIONES_H
#define CONEXIONES_H

// ============================================================
//  conexiones.h
//  Funciones para manejo de sockets.
//  Incluir en cualquier módulo con: #include "conexiones.h"
// ============================================================

// Levanta un servidor en el puerto indicado.
// Devuelve el file descriptor (fd) del servidor.
// Uso: int fd_server = iniciar_servidor("8001");
int iniciar_servidor(char* puerto);

// Espera a que alguien se conecte al servidor.
// Bloquea hasta que llega una conexión.
// Devuelve el fd del cliente conectado.
// Uso: int fd_cliente = esperar_cliente(fd_server);
int esperar_cliente(int fd_servidor);

// Se conecta como cliente a un servidor.
// Devuelve el fd de la conexión, o -1 si falló.
// Uso: int fd = crear_conexion("127.0.0.1", "8001");
int crear_conexion(char* ip, char* puerto);

// Envía un mensaje por el socket indicado.
// El buffer es un t_buffer* de la commons.
// Uso: enviar_mensaje(fd, buffer);
void enviar_mensaje(int fd, void* buffer, int size);

// Recibe un mensaje del socket indicado.
// Devuelve los bytes recibidos en un buffer con malloc.
// El caller es responsable de liberar la memoria.
// Uso: void* data = recibir_mensaje(fd, &size);
void* recibir_mensaje(int fd, int* size);

#endif // CONEXIONES_H