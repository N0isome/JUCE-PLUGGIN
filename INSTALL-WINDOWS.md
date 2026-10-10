# Instalar JUCE PLUGGIN 1.1 en Windows x64

1. Cierra FL Studio.
2. Extrae el ZIP y copia la carpeta completa **JUCE PLUGGIN.vst3** en:
   `C:\Program Files\Common Files\VST3\`
3. Reemplaza el bundle anterior. No copies solo el binario de Contents.
4. Abre FL → Options → Manage plugins → Find installed plugins.
   Activa Rescan previously verified plugins si conservas una instancia anterior.
5. Inserta JUCE PLUGGIN como efecto en un insert dedicado del Mixer.

## Capturar Chrome o audio del sistema sin cable virtual

Fuente: **Audio sistema (Windows)** → Dispositivo: la salida donde escuchas
Chrome → **Conectar** → comprobar L/R → Record → Stop.

No requiere instalar VB-CABLE ni abrir un navegador desde el plugin.
La selección funciona con las salidas de reproducción que Windows expone,
independientemente del modelo de tarjeta. Captura la mezcla del dispositivo
seleccionado, no exclusivamente Chrome.

Monitorear queda deshabilitado para evitar realimentación. Mantén el motor
activo y desactiva Smart disable para esta instancia si FL suspende sus callbacks.
Si cambias audífonos o tarjeta: Actualizar dispositivos → seleccionar → Conectar.

[Guía de audio del sistema, pruebas y límites](SYSTEM-AUDIO.md).

## Entrada del DAW / cable

Para grabar una pista de FL, selecciona **Entrada del DAW / cable** y enruta
esa pista al insert donde está el plugin. Pulsa Conectar y comprueba L/R.

VB-CABLE sigue siendo una alternativa de ruteo: salida de Chrome = CABLE Input;
FL Studio ASIO → Show ASIO panel → Input = CABLE Output, Output = parlantes/interfaz;
Mixer IN = par estéreo del cable. El ZIP no instala este driver.
Mantén la salida del DAW en el dispositivo físico para evitar realimentación.

## Exportar

Al detener, espera el cierre del WAV. Arrastra la onda inferior a Playlist o
Sampler; **Guardar como** y **Descargar WAV** abren el selector de archivo nativo.
Guarda las tomas en tu carpeta de proyecto; los WAV temporales no forman parte
del estado del proyecto FL.

La demo es de 64 bits. macOS requiere un binario compilado para macOS; este ZIP
contiene solamente Windows. La captura directa de esta versión es WASAPI/Windows;
en macOS usa la entrada del DAW con BlackHole/Loopback.
