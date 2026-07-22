#include "serializacion.h"

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../../utils/src/utils/mensajes.h"

void escribir_en_buffer(
    void* buffer,
    uint32_t* desplazamiento,
    const void* dato,
    uint32_t tamanio
) {
    memcpy(
        (uint8_t*) buffer + *desplazamiento,
        dato,
        tamanio
    );

    *desplazamiento += tamanio;
}

void* serializar_contexto(t_contexto* contexto,int* tamanio_buffer,t_log* logger){
    if (contexto == NULL || tamanio_buffer == NULL) {
        log_error(
            logger,
            "Contexto o puntero de tamaño NULL al serializar."
        );
        return NULL;
    }
    if (contexto->tabla_segmentos == NULL) {
        log_error(
            logger,
            "La tabla de segmentos es NULL."
        );
        return NULL;
    }

    int cantidad_segmentos =
        list_size(contexto->tabla_segmentos);

    /*
     * Estructura del buffer:
     *
     * t_registros
     * int cantidad_segmentos
     * t_segmento segmento_0
     * t_segmento segmento_1
     * ...
     * bool proximo_a_detener
     */
    *tamanio_buffer =
        sizeof(t_registros)
        + sizeof(int)
        + cantidad_segmentos * sizeof(t_segmento)
        + sizeof(bool);

    void* buffer = malloc(*tamanio_buffer);

    if (buffer == NULL) {
        log_error(
            logger,
            "## ERROR: No se pudo reservar memoria para serializar el contexto."
        );
        return NULL;
    }

    uint32_t desplazamiento = 0;

    /* Registros */
    escribir_en_buffer(
        buffer,
        &desplazamiento,
        &contexto->registros,
        sizeof(t_registros)
    );

    /* Cantidad de segmentos */
    escribir_en_buffer(
        buffer,
        &desplazamiento,
        &cantidad_segmentos,
        sizeof(int)
    );

    /* Segmentos */
    for (int i = 0; i < cantidad_segmentos; i++) {
        t_segmento* segmento =
            list_get(contexto->tabla_segmentos, i);

        if (segmento == NULL) {
            log_error(logger,
            "## ERROR: El segmento de la posición %d es NULL.",
            i);
            free(buffer);
            return NULL;
        }

        escribir_en_buffer(
            buffer,
            &desplazamiento,
            segmento,
            sizeof(t_segmento)
        );
    }
    /* Estado del contexto */
    escribir_en_buffer(
        buffer,
        &desplazamiento,
        &contexto->proximo_a_detener,
        sizeof(bool)
    );

    /*
     * Validación final:
     * el desplazamiento debe coincidir con el tamaño calculado.
     */
    if (desplazamiento != (uint32_t)*tamanio_buffer) {
        log_error(
            logger,
            "## ERROR: Falla de serialización: desplazamiento=%u, tamaño=%d",
            desplazamiento,
            *tamanio_buffer
        );

        free(buffer);
        return NULL;
    }

    log_info(logger, "===== CONTEXTO SERIALIZADO =====");
    log_info(logger,"Registros:");
    log_info(
        logger,
        "AX=%u BX=%u CX=%u DX=%u",
        contexto->registros.ax,
        contexto->registros.bx,
        contexto->registros.cx,
        contexto->registros.dx);
    log_info(
        logger,
        "EAX=%u EBX=%u ECX=%u EDX=%u",
        contexto->registros.eax,
        contexto->registros.ebx,
        contexto->registros.ecx,
        contexto->registros.edx);
    log_info(
        logger,
        "SI=%u DI=%u PC=%u",
        contexto->registros.si,
        contexto->registros.di,
        contexto->registros.pc);
    log_info(
        logger,
        "Cantidad de segmentos: %d",
        cantidad_segmentos);

    for (int i = 0; i < cantidad_segmentos; i++) {
            t_segmento* segmento = list_get(
            contexto->tabla_segmentos,
            i);
        log_info(
            logger,
            "Segmento[%d] -> ID=%u BASE=%u TAMANIO=%u",
            i,
            segmento->id_segmento,
            segmento->base,
            segmento->tamanio);
    }
    log_info(
        logger,
        "Proximo a detener: %s",
        contexto->proximo_a_detener ? "true" : "false");
    log_info(
        logger,
        "Tamaño buffer serializado: %d bytes",
        *tamanio_buffer);
    log_info(logger, "===== FIN CONTEXTO =====");
    return buffer;
}

t_contexto* deserializar_contexto(void* buffer,int tamanio_buffer,t_log* logger){
    if (buffer == NULL) {
        log_error(logger, "# ERROR: No se puede deserializar: buffer NULL.");
        return NULL;
    }
    if (tamanio_buffer < (int)(
        sizeof(t_registros) +
        sizeof(int) +
        sizeof(bool))) {
        log_error(logger,"## ERROR: Buffer demasiado pequeño: %d bytes.",tamanio_buffer);
        return NULL;
        }
    uint32_t desplazamiento = 0;

    t_contexto* contexto = calloc(1, sizeof(t_contexto));

    if (contexto == NULL) {
        log_error(logger,"## ERROR: No se pudo reservar memoria para t_contexto.");
        return NULL;
    }

    // 1. Deserializar registros.

    memcpy(
        &contexto->registros,
        (uint8_t*)buffer + desplazamiento,
        sizeof(t_registros)
    );

    desplazamiento += sizeof(t_registros);

    // 2. Deserializar cantidad de segmentos.

    int cantidad_segmentos = 0;

    memcpy(
        &cantidad_segmentos,
        (uint8_t*)buffer + desplazamiento,
        sizeof(int)
    );

    desplazamiento += sizeof(int);

    if (cantidad_segmentos < 0) {
        log_error(logger,"## ERROR: Cantidad de segmentos inválida: %d.",cantidad_segmentos);
        free(contexto);
        return NULL;
    }

    /*
     * Antes de continuar, calculamos el tamaño que debería tener
     * el buffer según la cantidad de segmentos recibida.
     */
    size_t tamanio_esperado =
        sizeof(t_registros)
        + sizeof(int)
        + ((size_t)cantidad_segmentos * sizeof(t_segmento))
        + sizeof(bool);

    if ((size_t)tamanio_buffer != tamanio_esperado) {
        log_error(logger,
            "## ERROR: Tamaño de contexto incorrecto. Recibido: %d - Esperado: %zu",
            tamanio_buffer,
            tamanio_esperado);

        free(contexto);
        return NULL;
    }

     // 3. Crear la lista de segmentos.
    contexto->tabla_segmentos = list_create();

    if (contexto->tabla_segmentos == NULL) {
        log_error(logger,"## ERROR: No se pudo crear la tabla de segmentos.");
        free(contexto);
        return NULL;
    }

    // 4. Deserializar cada segmento.

    for (int i = 0; i < cantidad_segmentos; i++) {
        t_segmento* segmento = malloc(sizeof(t_segmento));

        if (segmento == NULL) {
            log_error(logger,"## ERROR: No se pudo reservar memoria para el segmento %d.",i);
            list_destroy_and_destroy_elements(
                contexto->tabla_segmentos,
                free);
            free(contexto);
            return NULL;
        }

        memcpy(
            segmento,
            (uint8_t*)buffer + desplazamiento,
            sizeof(t_segmento)
        );

        desplazamiento += sizeof(t_segmento);

        list_add(
            contexto->tabla_segmentos,
            segmento
        );
    }

    //5. Deserializar proximo_a_detener.

    memcpy(
        &contexto->proximo_a_detener,
        (uint8_t*)buffer + desplazamiento,
        sizeof(bool)
    );
    desplazamiento += sizeof(bool);

    // 6. Validación final.
    if (desplazamiento != (uint32_t)tamanio_buffer) {
        log_error(
            logger,
            "## ERROR: Deserialización inconsistente. Leídos: %u - Buffer: %d",
            desplazamiento,
            tamanio_buffer
        );

        list_destroy_and_destroy_elements(
            contexto->tabla_segmentos,
            free
        );
        free(contexto);
        return NULL;
    }

    //7. Mostrar el contexto reconstruido.

    log_info(logger, "===== CONTEXTO DESERIALIZADO =====");
    log_info(
        logger,
        "AX=%u BX=%u CX=%u DX=%u",
        contexto->registros.ax,
        contexto->registros.bx,
        contexto->registros.cx,
        contexto->registros.dx);
    log_info(logger,
        "EAX=%u EBX=%u ECX=%u EDX=%u",
        contexto->registros.eax,
        contexto->registros.ebx,
        contexto->registros.ecx,
        contexto->registros.edx);
    log_info(logger,
        "SI=%u DI=%u PC=%u",
        contexto->registros.si,
        contexto->registros.di,
        contexto->registros.pc);
    log_info(logger,"Cantidad de segmentos: %d",cantidad_segmentos);

    for (int i = 0; i < cantidad_segmentos; i++) {
        t_segmento* segmento = list_get(
            contexto->tabla_segmentos,
            i
        );
        log_info(logger,
            "Segmento[%d] -> ID=%u BASE=%u TAMANIO=%u",
            i,
            segmento->id_segmento,
            segmento->base,
            segmento->tamanio);
    }
    log_info(logger,"Proximo a detener: %s",
        contexto->proximo_a_detener
            ? "true"
            : "false");
    log_info(logger,"Bytes deserializados: %u",desplazamiento);
    log_info(logger, "===== FIN CONTEXTO =====");
    return contexto;
}