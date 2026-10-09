# Compilación de la demo Windows x64

Este paquete se compiló en Linux para Windows con GCC/MinGW-w64 13.2.0
(modelo POSIX), mingw-w64 11.0.1, binutils 2.41, JUCE 7.0.12, C++20,
CMake 4.4.4 y Ninja 1.13.2. Configuración Release; solo target VST3.

Las fuentes correspondientes están en este proyecto y `vendor/JUCE`.
La carpeta vendor conserva sin modificaciones todos los módulos JUCE,
el SDK VST3 y las herramientas necesarias. Se omiten las aplicaciones y
ejemplos adicionales de JUCE que no participan en esta compilación.

Para reproducir con MSVC en Windows, usa los comandos del README.
La compilación nativa genera `moduleinfo.json`; en la compilación cruzada
se desactiva `VST3_AUTO_MANIFEST`, porque el helper Windows no puede
ejecutarse en Linux. Ese archivo es opcional: se conservan los exports
`GetPluginFactory`, `InitDll` y `ExitDll` para descubrir el plugin.

En Linux, JUCE 7 ejecuta `attrib` para personalizar el icono de carpeta
de Explorer después de enlazar. Se omitió únicamente ese paso cosmético
mediante un ejecutable `attrib` que devuelve éxito. Los atributos de
Explorer no se conservan en un ZIP y no afectan al plugin.

MinGW usa el renderizador Windows/GDI de JUCE 7, con su aviso de que
DirectWrite no está implementado en esa configuración. No se corrigió
ni modificó código de JUCE para silenciarlo.

Los flags `-static -static-libgcc -static-libstdc++` están en CMake para
evitar distribuir DLL de GCC/MinGW. El binario empaquetado se procesó con
`strip --strip-unneeded`; los exports VST3 se verificaron después.

Verificación ejecutada: compilación/enlace Windows, formato PE32+ x64,
exports VST3 y lista de DLL importadas. Las pruebas de cola y de escritura
WAV se ejecutaron en Linux; no se ejecutó el binario Windows ni FL Studio.
