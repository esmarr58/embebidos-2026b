"""Ensayos del main.c REAL con GPIO/RTOS/timers simulados (no hardware).

Uso: python tests/test_host.py --cc gcc
Requiere compilador C de escritorio. No valida corriente, RF ni tiempos físicos.
"""
from pathlib import Path
import argparse
import shutil
import subprocess
import tempfile

STUB = r'''
#ifndef STUB_H
#define STUB_H
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
typedef int gpio_num_t;
typedef int BaseType_t;
typedef int portMUX_TYPE;
typedef void *TaskHandle_t;
typedef void *SemaphoreHandle_t;
typedef int esp_err_t;
typedef int esp_sleep_wakeup_cause_t;
#define GPIO_NUM_1 1
#define GPIO_MODE_INPUT 1
#define GPIO_MODE_OUTPUT 2
#define GPIO_PULLUP_ENABLE 1
#define GPIO_PULLDOWN_DISABLE 0
#define GPIO_INTR_DISABLE 0
#define GPIO_INTR_NEGEDGE 2
#define GPIO_INTR_LOW_LEVEL 4
#define ESP_SLEEP_WAKEUP_ALL 99
#define ESP_SLEEP_WAKEUP_GPIO 7
#define ESP_SLEEP_WAKEUP_UNDEFINED 0
#define ESP_OK 0
#define pdFALSE 0
#define pdTRUE 1
#define portMAX_DELAY 0xffffffffU
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#define portYIELD_FROM_ISR() ((void)0)
#define ESP_ERROR_CHECK(x) assert((x)==0)
#define configASSERT(x) assert(x)
typedef struct {uint64_t pin_bit_mask; int mode,pull_up_en,pull_down_en,intr_type;} gpio_config_t;
typedef struct {void (*callback)(void *); void *arg; const char *name; bool skip_unhandled_events;} esp_timer_create_args_t;
typedef struct {esp_timer_create_args_t args; bool running;} fake_timer_t;
typedef fake_timer_t *esp_timer_handle_t;
static int64_t now_us, press_start_us, press_end_us, second_press_us, second_end_us;
static int pins[64], intr_type, events, max_events, notifications, sleep_calls;
static bool external_pressed, intr_enabled;
static fake_timer_t pool[8];
static int allocated;
static void vTaskDelay(unsigned ticks) {assert(ticks>0); now_us += ticks*10000;}
static int64_t esp_timer_get_time(void) {return now_us;}
static int gpio_get_level(int pin) {
    if(pin!=1) return pins[pin];
    if(!external_pressed) return 1;
    if(now_us<press_end_us) {
        if(events==1 && now_us-press_start_us<40000)
            return (int)((now_us-press_start_us)/10000)%2; // rebote inicial
        return 0;
    }
    if(second_press_us && now_us>=second_press_us && now_us<second_end_us) return 0;
    return 1;
}
static int gpio_set_level(int pin,int level) {
    if(pin==10 && level) assert(!pins[11]);
    if(pin==11 && level) assert(!pins[10]);
    if(pin!=10 && pin!=11) assert(!pins[10] && !pins[11]);
    pins[pin]=level; return 0;
}
static int gpio_reset_pin(int p) {pins[p]=0; return 0;}
static int gpio_set_direction(int p,int d) {(void)p;(void)d;return 0;}
static int gpio_sleep_sel_dis(int p) {(void)p;return 0;}
static int gpio_config(const gpio_config_t *c) {(void)c;return 0;}
static int gpio_install_isr_service(int x) {(void)x;return 0;}
static int gpio_isr_handler_add(int p,void (*cb)(void*),void *arg) {(void)p;(void)cb;(void)arg;return 0;}
static int gpio_intr_disable(int p) {(void)p;intr_enabled=false;return 0;}
static int gpio_intr_enable(int p) {(void)p;intr_enabled=true;return 0;}
static int gpio_set_intr_type(int p,int t) {(void)p;intr_type=t;return 0;}
static TaskHandle_t xTaskGetCurrentTaskHandle(void) {return (void*)1;}
static void vTaskNotifyGiveFromISR(TaskHandle_t t, BaseType_t *w) {(void)t;*w=0;++notifications;}
static uint32_t ulTaskNotifyTake(int clear,unsigned timeout);
static int esp_timer_create(const esp_timer_create_args_t *args,esp_timer_handle_t *out) {
    assert(allocated<8); *out=&pool[allocated++]; (*out)->args=*args; return 0;
}
static int esp_timer_start_periodic(esp_timer_handle_t t,uint64_t us) {assert(us);t->running=true;return 0;}
static int esp_timer_stop(esp_timer_handle_t t) {assert(t->running);t->running=false;return 0;}
static int esp_timer_start_once(esp_timer_handle_t t,uint64_t us) {now_us+=(int64_t)us;t->args.callback(t->args.arg);return 0;}
static SemaphoreHandle_t xSemaphoreCreateBinary(void) {return (void*)1;}
static int xSemaphoreGive(SemaphoreHandle_t x) {(void)x;return 1;}
static int xSemaphoreTake(SemaphoreHandle_t x,unsigned timeout) {(void)x;(void)timeout;return 1;}
static int gpio_wakeup_enable(int p,int t) {(void)p;intr_type=t;return 0;}
static int gpio_wakeup_disable(int p) {(void)p;return 0;}
static int esp_sleep_enable_gpio_wakeup(void) {return 0;}
static int esp_sleep_disable_wakeup_source(int s) {(void)s;return 0;}
static esp_sleep_wakeup_cause_t esp_sleep_get_wakeup_cause(void) {return ESP_SLEEP_WAKEUP_GPIO;}
static const char *esp_err_to_name(int e) {return e==0 ? "ESP_OK" : "ERROR";}
static int esp_light_sleep_start(void);
#endif
'''

HARNESS = r'''
#include <setjmp.h>
#include <string.h>
#define app_main firmware_app_main
#include SOURCE_FILE
#undef app_main
static jmp_buf finished;
static uint32_t ulTaskNotifyTake(int clear,unsigned timeout) {
    (void)clear;
    if(timeout==0) {notifications=0;return 0;}
    if(events==max_events) longjmp(finished,1);
    assert(intr_enabled && intr_type==GPIO_INTR_NEGEDGE);
#ifdef TEST_B
    cuenta=25;
#else
    assert(cuenta==events%100);
#endif
    ++events;
    external_pressed=true;
    press_start_us=now_us;
    press_end_us=now_us+(events==1 ? 2000000 : 100000);
#if defined(TEST_B) && TC5_CONTROL_DESPIERTO
    second_press_us=press_end_us+1000000;
    second_end_us=second_press_us+100000;
#endif
    boton_isr(NULL);
    assert(notifications>0);
    notifications=0;
    return 1;
}
static int esp_light_sleep_start(void) {
#ifdef TEST_B
    ++sleep_calls;
    assert(!contar && !refrescar);
    assert(!t_refresco->running && !t_conteo->running);
    for(int i=0;i<7;++i) assert(pins[SEG[i]]==0);
    assert(!pins[10] && !pins[11]);
    assert(intr_type==GPIO_INTR_LOW_LEVEL && !intr_enabled);
    assert(gpio_get_level(BTN)==1); // misma pulsacion no puede despertar
    // Intentar ejecutar callbacks en el periodo parado no cambia nada.
    uint8_t saved=cuenta;
    refresco_cb(NULL); conteo_cb(NULL);
    assert(cuenta==saved && !pins[10] && !pins[11]);
    now_us+=5000000;
    press_end_us=now_us+2000000; // otra pulsacion mantenida para despertar
#endif
    return 0;
}
int main(void) {
    max_events=
#ifdef TEST_B
        1;
#else
        102;
#endif
    if(setjmp(finished)==0) firmware_app_main();
    assert(events==max_events && intr_enabled && intr_type==GPIO_INTR_NEGEDGE);
#ifdef TEST_B
    assert(cuenta==25 && contar && refrescar);
    assert(t_refresco->running && t_conteo->running);
    assert(sleep_calls==(TC5_CONTROL_DESPIERTO ? 0 : 1));
    conteo_cb(NULL); assert(cuenta==26); // reanuda sin recuperar 5 s dormidos
    cuenta=99; conteo_cb(NULL); assert(cuenta==0);
#else
    assert(cuenta==2 && sleep_calls==0); // 102 pulsaciones, incluido hold 2 s
#endif
    cuenta=42;
    refresco_cb(NULL); // decenas 4: 0x66
    assert(pins[10] && !pins[11]);
    for(int i=0;i<7;++i) assert(pins[SEG[i]]==((0x66>>i)&1));
    refresco_cb(NULL); // unidades 2: 0x5B
    assert(!pins[10] && pins[11]);
    for(int i=0;i<7;++i) assert(pins[SEG[i]]==((0x5B>>i)&1));
    puts("PASS: eventos, hold, rollover, multiplexeo, estados y rearme");
    return 0;
}
'''

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default=shutil.which('gcc'))
    args = parser.parse_args()
    if not args.cc:
        parser.error('Indica compilador C de escritorio con --cc')
    root = Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix='tc05-host-') as folder:
        temp = Path(folder)
        (temp/'stub.h').write_text(STUB, encoding='utf-8')
        for header in ['freertos/FreeRTOS.h', 'freertos/task.h', 'freertos/semphr.h',
                       'driver/gpio.h', 'esp_timer.h', 'esp_err.h', 'esp_sleep.h']:
            target = temp/header
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text('#include "stub.h"\n', encoding='utf-8')
        harness = temp/'harness.c'
        harness.write_text(HARNESS, encoding='utf-8')
        for label, project, flags in [
            ('A: 102 pulsaciones y hold', 'programa-a-contador-isr', []),
            ('B: sleep y reanudacion', 'programa-b-light-sleep', ['-DTEST_B']),
            ('B: control sin sleep', 'programa-b-light-sleep', ['-DTEST_B', '-DTC5_CONTROL_DESPIERTO=1'])
        ]:
            output = temp/'test.exe'
            source = (root/project/'src/main.c').as_posix()
            subprocess.run([args.cc, '-std=c11', '-I', str(temp),
                            '-DSOURCE_FILE="'+source+'"', *flags,
                            str(harness), '-o', str(output)], check=True)
            result = subprocess.run([str(output)], capture_output=True, text=True, timeout=10)
            assert result.returncode == 0, result.stdout + result.stderr
            print(label, ':', result.stdout.splitlines()[-1])
    print('Limite: simulacion monohilo; no valida consumo, cableado ni rebote fisico.')

if __name__ == '__main__':
    main()
