# Validación ejecutada

Fecha: 9 de octubre de 2026.

## Resultados

| Comprobación | Resultado |
| --- | --- |
| CMake: configuración del proyecto completo con JUCE 8.0.6 | Correcta |
| Compilación C++20 de procesador, editor y módulos JUCE | Correcta |
| Enlace de `LoopCapture_VST3` y creación de `moduleinfo.json` | Correcto, Linux x86_64 |
| Enlace del ejecutable JUCE de integración | Correcto |
| `ctest`: `CaptureCoreTests` | PASS |
| `ctest`: `CaptureIntegrationTests` | PASS |
| Núcleo con AddressSanitizer + UndefinedBehaviorSanitizer | PASS |
| Núcleo con ThreadSanitizer | PASS, sin reportes de carreras |
| Sintaxis de procesador/editor/integración con headers JUCE 7.0.12 | PASS, `-Wall -Wextra -Wpedantic -Werror` |
| Inspección del callback | Sin archivos, mutexes, waits, asignaciones de buffers o wakeups en `processBlock` |

Entorno utilizado: Linux x86_64, GCC 13.3.0, CMake 4.4.4, Ninja 1.13.2,
JUCE 8.0.6, configuración Debug para el plugin y la integración. JUCE 7.0.12
se verificó a nivel de sintaxis/API; no se enlazó en este entorno.

Salida final de CTest:

```text
Start 1: CaptureCoreTests
1/2 Test #1: CaptureCoreTests ................. Passed
Start 2: CaptureIntegrationTests
2/2 Test #2: CaptureIntegrationTests .......... Passed
100% tests passed out of 2
```

## Qué comprueban las pruebas

`CaptureCoreTests` verifica el wrap físico del buffer, capacidad exacta,
rechazo de bloques completos en overflow, alineación de canales y transferencia
concurrente de **2.000.000 frames estéreo**. También reproduce un Stop cuando
un productor ya había entrado: los recursos siguen activos hasta que ese
productor termina, se rechazan productores nuevos y se conserva el último bloque.

`CaptureIntegrationTests` usa el procesador real y su worker JUCE para escribir
y reabrir WAVs. Comprueba 24-bit PCM, dos canales, 48 kHz, longitud exacta,
ganancia de +6 dB, peaks antes de mute y contenido de ambos canales con Monitor
apagado. También verifica Stop sin callbacks posteriores, restauración APVTS,
una segunda toma con archivo diferente sin borrar la primera, release del motor,
cancelación durante Starting, overflow con conservación del prefijo y cierre
de una toma activa al destruir el procesador.

Los WAVs de prueba conocidos se eliminan tras validarlos. No se incluyen binarios
de prueba ni dependencias descargadas en el paquete de fuentes.

## Límites de esta validación

- No se ejecutaron builds nativos Windows/macOS ni un AU de Apple. El workflow
  incluido proporciona una matriz para esos sistemas con JUCE 7.0.12 y 8.0.6;
  todavía no se ha ejecutado en un repositorio remoto.
- No se abrió FL Studio ni se comprobó una importación real en Playlist/Sampler,
  un dispositivo VB-Cable/BlackHole/Loopback o el arrastre nativo entre aplicaciones.
- No se hizo inspección visual en un host. La implementación GUI sí fue compilada.
- ASan/UBSan se ejecutaron con `ASAN_OPTIONS=detect_leaks=0`: LeakSanitizer no
  puede enumerar los procesos necesarios dentro de este entorno. Esto no
  constituye una prueba de ausencia de fugas.
- Los sanitizers cubrieron el núcleo SPSC y su protocolo de Stop; la integración
  JUCE se ejecutó en Debug sin sanitizers. No se certifican todos los posibles
  contratos o modos de suspensión de todos los hosts.

El checklist del README permite completar la comprobación nativa y del DAW.

## Actualización de identidad: JUCE PLUGGIN

Se incorporó el PNG original enviado por el usuario, sin modificar sus bytes.
CMake se configuró nuevamente con JUCE 8.0.6; se generó y compiló el target
`LoopCaptureBranding`, y se recompilaron correctamente los objetos del
procesador y del editor con el recurso incrustado y el nuevo nombre visible.
Esta actualización no volvió a enlazar el VST3 ni ejecutó las pruebas de DSP;
los resultados anteriores corresponden a la versión previa al cambio de identidad.

## Interfaz Rack/Mesa y exportación

Se configuró CMake con los dos archivos TTF incrustados y se compiló
`LoopCaptureBranding`. Los objetos de procesador, editor y editor de integración
se recompilaron con JUCE 8.0.6 sin avisos del compilador. Se verificaron
las dimensiones mínimas de los componentes leyendo el layout. No se ejecutó
el selector nativo de archivos ni el arrastre a un DAW en este entorno.

La demo web pasó pruebas de Stop, PCM24, L/R, límite de toma, creación del File
WAV, arrastre/soltado al canal de prueba, reproducción del canal, guardado del
File exacto, cancelación, descarga alternativa y cambios de tema/interfaz.
Esas pruebas usan stubs del DOM y las APIs del navegador; no certifican
permisos de captura, renderizado visual ni arrastre externo en FL Studio.

## Paquete Windows x64 / firma FR3DO

Se compiló y enlazó el VST3 Release para Windows x64 con JUCE 7.0.12 y
MinGW-w64. Se verificaron PE32+, los exports GetPluginFactory/InitDll/ExitDll
y las dependencias de DLL: únicamente bibliotecas del sistema Windows,
sin libstdc++, libgcc ni winpthread externos. Se omite el manifiesto opcional
moduleinfo.json durante la compilación cruzada.

CaptureCoreTests pasó nuevamente (wrap, overflow, 2 millones de frames estéreo
y Stop concurrente). CaptureIntegrationTests se recompiló con JUCE 8.0.6 y
pasó: WAV24, ganancia antes de mute, estéreo, Stop sin nuevos callbacks, APVTS,
release/cancel, overflow y destrucción. La demo web pasó recorder.cjs y
sample-flow.cjs con la firma nueva.

Estos resultados no reemplazan la prueba de carga y operación dentro de
FL Studio/Windows. No se ejecutó el VST3 Windows, el selector de carpeta
nativo ni el drag and drop externo. No hay binario AU/macOS en esta entrega.

## Corrección: interfaz nativa equivalente a la web

El editor se recompiló y enlazó como VST3 Windows x64/JUCE 7.0.12. Se revisaron
exports y DLL de sistema. La integración JUCE 8.0.6 en Linux pasó nuevamente,
incluyendo preview con Monitor apagado, duración completa y resampling
48 → 44.1 kHz, además de grabación PCM24, APVTS, Stop, overflow y destrucción.
Se renderizaron e inspeccionaron Rack oscuro, Rack claro, Mesa claro y una toma
terminada usando el editor C++ real (no HTML). Esas capturas se obtuvieron en
Linux con JUCE 8; no certifican el renderizador Windows/GDI.

El usuario confirmó que la versión anterior cargó en FL Studio mediante una
captura. La revisión nueva no se ha ejecutado en FL Studio desde este entorno;
el arrastre entre aplicaciones y el diálogo nativo de guardado aún requieren
la comprobación en Windows.
