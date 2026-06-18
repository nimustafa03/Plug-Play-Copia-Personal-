#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <commons/log.h>
#include <commons/config.h>
#include <utils/conexiones.h>
#include <utils/mensajes.h>
#include <unistd.h>
#include <string.h>
#include "cpu.h"

/*------------------------ MICRO OPERACIONES ------------------------*/

uint32_t obtener_valor(char* registro, t_registros* cpu) {
    if (strcmp(registro, "AX") == 0) return cpu->AX;
    if (strcmp(registro, "BX") == 0) return cpu->BX;
    if (strcmp(registro, "CX") == 0) return cpu->CX;
    if (strcmp(registro, "DX") == 0) return cpu->DX;
    if (strcmp(registro, "EAX") == 0) return cpu->EAX;
    if (strcmp(registro, "EBX") == 0) return cpu->EBX;
    if (strcmp(registro, "ECX") == 0) return cpu->ECX;
    if (strcmp(registro, "EDX") == 0) return cpu->EDX;
    if (strcmp(registro, "SI") == 0) return cpu->SI;
    if (strcmp(registro, "DI") == 0) return cpu->DI;
    return 0;
}

void escribir_registro(char* registro, t_registros* cpu, uint32_t valor) {
    if (strcmp(registro, "AX") == 0) cpu->AX = (uint8_t)valor;
    else if (strcmp(registro, "BX") == 0) cpu->BX = (uint8_t)valor;
    else if (strcmp(registro, "CX") == 0) cpu->CX = (uint8_t)valor;
    else if (strcmp(registro, "DX") == 0) cpu->DX = (uint8_t)valor;
    else if (strcmp(registro, "EAX") == 0) cpu->EAX = valor;
    else if (strcmp(registro, "EBX") == 0) cpu->EBX = valor;
    else if (strcmp(registro, "ECX") == 0) cpu->ECX = valor;
    else if (strcmp(registro, "EDX") == 0) cpu->EDX = valor;
    else if (strcmp(registro, "SI") == 0) cpu->SI = valor;
    else if (strcmp(registro, "DI") == 0) cpu->DI = valor;
}

/*------------------------ INSTRUCCIONES ------------------------*/

// SUMA

void sum(char* instruccion, t_registros* cpu) {
    char registro_destino[32];
    char registro_origen[32];
    sscanf(instruccion, "%*s %s %s", registro_destino, registro_origen);

    uint32_t valor_destino = obtener_valor(registro_destino, cpu);
    uint32_t valor_origen = obtener_valor(registro_origen, cpu);
    uint32_t resultado = valor_destino + valor_origen;
    escribir_registro(registro_destino, cpu, resultado);

    cpu->PC++;
}

// RESTA

void sub(char* instruccion, t_registros* cpu) {
    char registro_destino[32];
    char registro_origen[32];
    sscanf(instruccion, "%*s %s %s", registro_destino, registro_origen);

    uint32_t valor_destino = obtener_valor(registro_destino, cpu);
    uint32_t valor_origen = obtener_valor(registro_origen, cpu);
    uint32_t resultado = valor_destino - valor_origen;
    escribir_registro(registro_destino, cpu, resultado);

    cpu->PC++;
}

// SET

void set(char* instruccion, t_registros* cpu) {
    char registro_destino[32];
    char valor_str[32];
    sscanf(instruccion, "%*s %s %s", registro_destino, valor_str);

    uint32_t valor = atoi(valor_str);
    escribir_registro(registro_destino, cpu, valor);

    cpu->PC++;
}

// JNZ

void jnz(char* instruccion, t_registros* cpu) {

    char registro[32];
    char pc_str[32];
    sscanf(instruccion, "%*s %s %s", registro, pc_str);

    uint32_t valor = obtener_valor(registro, cpu);
    uint32_t nuevo_pc = (uint32_t) strtol(pc_str, NULL, 10);

    if (valor != 0) {
        cpu->PC = nuevo_pc;
        }  else {
        cpu->PC++;
    }
}


// NOOP

void noop(char* instruccion, t_registros* cpu) {
    cpu->PC++;
}

// MOV_IN
// MOV_OUT
// COPY_MEM
// todavia no se vio memoria