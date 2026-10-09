# JUCE PLUGGIN — BY FR3DO - Demo en pañales

## Instalar y probar en FL Studio (Windows x64)

1. Extrae todo el ZIP a una carpeta. Dentro encontrarás la carpeta completa
   `JUCE PLUGGIN.vst3`. Conserva su estructura `Contents/x86_64-win/`.
2. Copia **la carpeta completa** a `C:\Program Files\Common Files\VST3`.
   Windows puede pedir autorización al copiar a esa carpeta.
   También puedes copiarla a una carpeta propia, por ejemplo
   `Documentos\FR3DO\VST3`, y agregar esa ruta en el Plugin Manager de FL Studio.
3. Abre FL Studio → **Options → Manage plugins → Find installed plugins**.
   Busca **JUCE PLUGGIN** y actívalo como favorito.
4. Carga el plugin en un slot de efectos del **Mixer**, en el canal que recibe
   el audio que quieres capturar. El plugin es un efecto estéreo, no un instrumento.
5. Si quieres audio de otra aplicación: envía esa aplicación al cable virtual
   y selecciona su salida de grabación como **entrada estéreo del Mixer**.
   El driver/dispositivo de FL debe ofrecer esa entrada. El plugin recibe
   lo que le entrega el DAW; no selecciona por sí mismo el audio del sistema.
6. Verifica movimiento en L/R, ajusta Gain y pulsa **Record**. Pulsa **Stop**
   y espera a que termine de escribir el WAV. La toma aparecerá abajo.
7. Arrastra la onda inferior a Playlist, Sampler o un destino que admita WAV.
   Usa **Guardar como** para elegir una carpeta y un nombre. Si el host no
   admite el arrastre externo, usa **Ver carpeta** e importa el WAV desde allí.

El plugin no abre navegador, pestañas ni selectores de pantalla. No necesita
conexión a internet, instalar fuentes ni abrir la demo web para grabar.
El monitoreo de entrada puede activarse/desactivarse sin cambiar la captura.
Una toma termina también al cambiar la configuración de audio, usar bypass
o desactivar el motor del host. Máximo de esta versión: 30 minutos por toma.

## Qué incluye esta compilación

- VST3 para Windows **x64**, Release, C++20, JUCE **7.0.12**.
- Ganancia, monitoreo, medidores estéreo, onda de la toma, arrastre de WAV,
  guardado, modo claro/oscuro e interfaces Rack/Mesa.
- WAV estéreo **24-bit PCM**, con la frecuencia de muestreo que entrega el DAW.
- Bibliotecas de ejecución de GCC/MinGW enlazadas estáticamente.
- Demo temprana: compilada desde Linux para Windows. No se ha abierto todavía
  en FL Studio/Windows, ni comprobado allí un cable virtual o el arrastre.

No incluye un binario AU ni VST3 de macOS. Las fuentes tienen configuración
para compilarlos con Xcode; el paquete Windows no sirve para instalarlos en Mac.

## Fuentes y licencias

El paquete de fuentes completo, con las fuentes de JUCE 7.0.12 y las instrucciones CMake,
está en la misma sección Software del portafolio:
https://nicolas-cortez.vercel.app/#software

Esta demo se distribuye bajo GNU GPL v3. Consulta `LICENSE.txt` y
`THIRD-PARTY-NOTICES.md`. Las fuentes incrustadas tienen licencia SIL OFL.
La aplicación y el nombre del proyecto no implican afiliación con JUCE o Steinberg.
