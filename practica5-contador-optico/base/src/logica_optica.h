// Logica sin dependencias de ESP-IDF: se puede probar en escritorio.
// 0 = haz libre, 1 = bloqueado. Confirmacion temporal, no histeresis analogica.
#ifndef LOGICA_OPTICA_H
#define LOGICA_OPTICA_H
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int bruto, estable;
    bool armado;
    int64_t desde_us, ultimo_us;
    int64_t libre_us, bloqueado_us;
    uint64_t total, rechazados, repetidos;
} optico_t;

static inline void optico_iniciar(optico_t *s, int nivel, int64_t ahora,
                                  int64_t libre_us, int64_t bloqueado_us)
{
    *s = (optico_t){.bruto = nivel, .estable = -1, .armado = false,
        .desde_us = ahora, .ultimo_us = ahora,
        .libre_us = libre_us, .bloqueado_us = bloqueado_us};
}

// Confirmar ANTES de aplicar un flanco posterior permite reconstruir
// pulsos que ya terminaron cuando la tarea vacia la cola.
static inline void optico_avanzar(optico_t *s, int64_t hasta_us)
{
    if (hasta_us < s->ultimo_us) return;
    s->ultimo_us = hasta_us;
    int64_t ventana = s->bruto ? s->bloqueado_us : s->libre_us;
    if (s->bruto == s->estable || hasta_us - s->desde_us < ventana) return;
    s->estable = s->bruto;
    if (s->estable == 0) {
        s->armado = true; // hace falta volver a liberar para otro objeto
    } else if (s->armado) {
        ++s->total;
        s->armado = false;
    }
}

static inline void optico_flanco(optico_t *s, int nivel, int64_t t_us)
{
    if (t_us < s->ultimo_us) { ++s->rechazados; return; }
    optico_avanzar(s, t_us);
    if (nivel == s->bruto) { ++s->repetidos; return; }
    if (s->bruto != s->estable) ++s->rechazados;
    s->bruto = nivel;
    s->desde_us = t_us;
}

// Tras perder flancos no se adivinan eventos. Conservar total conocido,
// desarmar y exigir un nuevo intervalo libre antes de contar otra vez.
static inline void optico_resincronizar(optico_t *s, int nivel, int64_t ahora)
{
    s->bruto = nivel;
    s->estable = -1;
    s->armado = false;
    s->desde_us = s->ultimo_us = ahora;
}
#endif
