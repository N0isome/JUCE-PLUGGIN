# Audio del sistema — Windows

JUCE PLUGGIN 1.1 agrega captura WASAPI loopback de una salida de Windows.
No requiere un modelo concreto de interfaz, VB-CABLE ni abrir el navegador.
Se listan los dispositivos de reproducción activos que Windows expone: altavoces,
audífonos, interfaces USB y salidas HDMI, entre otros. No cambia el driver ASIO de FL.

## Uso

1. Inserta el plugin en un insert dedicado del Mixer, con el motor de FL activo.
2. En Fuente elige **Audio sistema (Windows)**.
   El botón **Conexiones** abre una ventana con las salidas compartidas detectadas,
   las entradas físicas y controladores MIDI informativos, el estado de conexión y niveles L/R.
   Elige una salida y pulsa **Usar esta salida** para conectarla desde la ventana.
3. En Dispositivo elige la misma salida que utiliza Chrome o la aplicación fuente.
   **Salida predeterminada** resuelve la salida multimedia actual al conectar.
4. Pulsa **Conectar**. Espera el mensaje `Capturando: ...`.
5. Reproduce audio. Comprueba L/R y pulsa Record → Stop.
6. Arrastra la onda inferior a Playlist/Sampler, o usa **Guardar como**.

**Monitorear queda deshabilitado en este modo**, también frente a automatización
del host. Ya escuchas el audio en la salida original; reenviarlo a esa misma salida
produciría realimentación. La preescucha funciona después de detener la toma.
El loopback captura la mezcla de la salida, incluido audio del DAW si se reproduce
en ese mismo dispositivo. Para grabar exclusivamente una aplicación, dirige esa
aplicación a otra salida y selecciona esa salida aquí.

Después de conectar o desconectar audífonos/interfaces, elige **Actualizar
dispositivos**. Cambiar de dispositivo exige reconectar; una toma activa bloquea
los selectores para mantener una fuente y frecuencia coherentes. Si Windows
invalida el dispositivo o informa un salto de audio, la toma se cierra y muestra
el problema; el WAV disponible contiene el prefijo completado. Reconecta para
otra toma. No se cambia silenciosamente a la entrada del DAW.

## Alcance real

- Windows 10/11, salida en modo **compartido**. WASAPI loopback no captura flujos
  exclusivos ni la ruta ASIO privada de otro programa. La tarjeta debe exponer una
  salida al sistema Windows; un dispositivo exclusivamente ASIO no aparecerá.
- La lista de entradas físicas es informativa: el VST3 no puede abrir o cambiar
  las entradas de la interfaz que administra FL Studio. Selecciona esa entrada
  en el Mixer de FL y usa **Entrada del DAW / cable** para grabar una MPC física.
  La lista de salidas describe dispositivos, no identifica qué aplicación los usa.
  Ver una MPC como controlador MIDI indica comunicación MIDI, no que su audio
  esté siendo enviado a la salida de Windows.
- El motor del DAW debe seguir ejecutando el plugin. En FL desactiva **Smart disable**
  en este wrapper si el host suspende el procesamiento durante el silencio.
- Audio protegido puede quedar silenciado por el sistema. No se omiten estas restricciones.
- La selección predeterminada queda fijada al conectar; cambiar la salida predeterminada
  de Windows requiere reconectar para capturar la nueva salida.
- PCM 8/16/24/32 y float32; mono duplicado a estéreo, estéreo y par frontal L/R
  de formatos multicanal con layout conocido. Otros formatos se rechazan con un mensaje.
- Conversión a la frecuencia del DAW mediante interpolación Lagrange, filtro
  antialias Butterworth de orden 8 al reducir frecuencia y corrección de deriva
  limitada a ±0.1 %. Reserva inicial de 40 ms; la grabación conserva silencios.
- El callback no usa COM, mutexes, esperas ni asignaciones para esta captura.
  El acceso al dispositivo ocurre en un worker y una cola SPSC preasignada.
- En macOS permanece **Entrada del DAW / cable** (BlackHole/Loopback y CoreAudio
  configurados en el host). Esta versión no implementa captura directa ScreenCaptureKit.

## Verificación reproducible

`CaptureIntegrationTests` añade pruebas de decodificación PCM/float, silencios,
NaN/Inf, stride/layout inválidos, 25 combinaciones de frecuencia (8–384 kHz de
origen, 8–192 kHz de destino), rechazo de alias, separación L/R, 100 reconexiones
con callback concurrente, cierre al perder la fuente sin nuevos callbacks,
contenido real del WAV, prevención de feedback con Monitor automatizado y
rechazo de grabación desconectada. Windows también prueba un endpoint inexistente
contra la API real. Los endpoints sintéticos solo existen en el ejecutable de pruebas,
con `LOOPCAPTURE_TESTING`; no se compilan dentro del plugin distribuido.

Estas pruebas no sustituyen la captura audible en un equipo Windows real ni la
validación de arrastre en FL Studio. Las pruebas de grabación existentes se conservan.

Referencias: [WASAPI loopback](https://learn.microsoft.com/en-us/windows/win32/coreaudio/loopback-recording),
[IAudioCaptureClient](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudiocaptureclient-getbuffer).
