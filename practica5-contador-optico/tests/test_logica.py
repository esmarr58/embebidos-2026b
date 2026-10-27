"""Pruebas del filtro real en C, sin placa. Uso: --cc <gcc de escritorio>."""
from pathlib import Path
import argparse
import shutil
import subprocess
import tempfile

TEST = r'''
#include <assert.h>
#include <stdio.h>
#include "logica_optica.h"
int main(void) {
    optico_t s;
    optico_iniciar(&s,0,0,2000,2000);
    optico_avanzar(&s,2000); assert(s.armado && s.total==0);
    optico_flanco(&s,1,10000); optico_avanzar(&s,12000);
    assert(s.total==1 && !s.armado);
    optico_avanzar(&s,2000000); assert(s.total==1); // objeto sostenido
    optico_flanco(&s,0,2100000); optico_avanzar(&s,2102000);
    // Pico aislado de 1 ms no confirmado, no cuenta.
    optico_flanco(&s,1,2200000); optico_flanco(&s,0,2201000);
    optico_avanzar(&s,2203000); assert(s.total==1 && s.rechazados>0);
    // Pulsos completos ya terminados, procesados luego: reconstruccion por tiempos.
    for(int i=0;i<101;++i) {
        int64_t t=2300000+i*10000;
        optico_flanco(&s,1,t); optico_flanco(&s,0,t+3000);
    }
    assert(s.total==102 && s.total%100==2);
    // Un hueco libre de 1 ms no rearma ni duplica el mismo objeto.
    optico_iniciar(&s,0,0,2000,2000); optico_avanzar(&s,2000);
    optico_flanco(&s,1,10000); optico_avanzar(&s,12000);
    optico_flanco(&s,0,15000); optico_flanco(&s,1,16000);
    optico_avanzar(&s,20000); assert(s.total==1);
    // Arranque con objeto: no inventar el flanco anterior al encendido.
    optico_iniciar(&s,1,0,2000,2000); optico_avanzar(&s,10000);
    assert(s.total==0 && !s.armado);
    optico_flanco(&s,0,20000); optico_flanco(&s,1,25000);
    optico_avanzar(&s,27000); assert(s.total==1);
    // Perdida: preservar total conocido y desarmar hasta nuevo intervalo libre.
    optico_resincronizar(&s,1,30000); optico_avanzar(&s,40000);
    assert(s.total==1 && !s.armado);
    optico_flanco(&s,0,50000); optico_flanco(&s,1,55000);
    optico_avanzar(&s,57000); assert(s.total==2);
    puts("PASS: sostenido, ruido corto, rearme, cola tardia, 99->00, arranque bloqueado y resincronizacion");
    return 0;
}
'''
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc',default=shutil.which('gcc'))
    args=parser.parse_args()
    if not args.cc: parser.error('Se requiere --cc')
    include=Path(__file__).resolve().parents[1]/'base/src'
    with tempfile.TemporaryDirectory(prefix='p5-optico-') as name:
        root=Path(name)
        (root/'test.c').write_text(TEST,encoding='utf-8')
        subprocess.run([args.cc,'-std=c11','-Wall','-Wextra','-Werror','-I',str(include),
                        str(root/'test.c'),'-o',str(root/'test.exe')],check=True)
        subprocess.run([str(root/'test.exe')],check=True,timeout=10)
    print('Limite: no valida circuitos, ISR real, concurrencia ni ancho de banda fisico.')
if __name__=='__main__': main()
