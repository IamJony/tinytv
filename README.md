# TinyTV

**TinyTV** es un fork del proyecto **[Sanson](https://gitlab.com/AnddyCort/Sanson-FLinux )**, a diferencia del proyecto original este integra un **sistema de plugins externos** y añade una **Tienda de plugins** para instalar, actualizar y gestionar nuevas plataformas de streaming (LatinoTV, RamenTV, Internet Archive, etc.) .

![TinyTV: interfaz principal](capturas/ui.png)

Más capturas en el directorio [`capturas/`](capturas/).

---

## Novedades TinyTV

- **Arquitectura de plugins externos dinámicos:** ya no depende de plataformas fijas compiladas en el binario. TinyTV escanea e inicializa los plugins instalados en `~/.config/tinytv/plugins` (y en el directorio del sistema), permitiendo añadir nuevas fuentes o actualizarlas de forma independiente.
- **Tienda de plugins integrada:** desde la barra lateral (**Tienda**) se puede explorar el catálogo remoto, descargar e instalar plugins en un clic, ver versiones locales frente a disponibles y desinstalarlos.
- **Autenticación por cookies ampliada:** en **Ajustes** se pueden pegar sesiones tanto en formato encabezado HTTP (`Cookie: ...` / `clave=valor`) como en formato **Netscape HTTP Cookie File** (`cookies.txt`).
- **Incluye plugins base y soporte comunitario:** compatible con YouTube, YouTube Music (Música), Twitch, Kick, Internet Archive, LatinoTV y otros plugins del repositorio de la comunidad.

---

## Características heredadas de Sansón

- Portada, búsqueda (videos y canales) y exploración de canales por plataforma.
- Reproducción de videos y directos con selección de las **calidades que entrega cada plataforma**.
- Botón **Idioma** en los videos de YouTube: selección de pista de audio (por defecto suena el original, no el doblaje automático) y **subtítulos**, incluidos los generados automáticamente.
- Salto automático de anuncios en Twitch.
- **Chat en vivo** de los directos con emotes de cada plataforma y envío de mensajes con sesión iniciada.
- **Chat de repetición** en videos grabados (VODs) sincronizado con la reproducción.
- **Comentarios** en videos de YouTube y publicación con sesión iniciada.
- Controles sobre el video (pausa, barra de avance, volumen, calidad, pantalla completa) que aparecen al mover el ratón y se ocultan solos. Saltos de 10 segundos con las flechas, recarga junto a **Buscar** (o `F5`) y botón **Volver** arriba a la izquierda.
- Diseño pensado también para pantallas de 800×600 (netbooks): la ventana se adapta a pantallas pequeñas y en ancho reducido la barra lateral pasa a modo solo íconos.
- Reconexión automática si el flujo se traba y ajuste automático de calidad si vuelve a ocurrir.
- Canales seguidos e historial guardados localmente en el equipo.
- Pestaña **Música** (YouTube Music), solo audio y sin anuncios:
  - Canciones, álbumes y artistas (con opción de seguir artistas).
  - Portada del álbum y **letra sincronizada** al costado (clic en cualquier línea para saltar a ese instante).
  - **Cola de reproducción** editable (clic derecho para reproducir a continuación o añadir a la cola; subir, bajar, quitar o vaciar), modo aleatorio, repetir y **radio** continua.
  - Reproducción en segundo plano mientras navegas; el botón **Sonando** vuelve a la canción actual.
  - Respaldo automático en SoundCloud si YouTube no entrega el audio de una pista.
- **Descargas** integradas sin dependencias externas (`yt-dlp` ni `ffmpeg`): video con audio o solo audio, selección de calidad, nombre, carpeta de destino y descargas paralelas por múltiples conexiones (1 a 8 hilos).

---

## ¿Funcionará bien en mi equipo?

Depende del equipo. Reproducir video requiere dibujar cuadros continuamente en pantalla, lo que depende sobre todo del procesador y de la aceleración gráfica.

Como referencia en el equipo de pruebas original (AMD V120 de un núcleo, 744 MB de RAM, video sin aceleración), un directo a 360p con el modo de video liviano activado usa cerca de la mitad del procesador. En equipos más modestos puede ir lento o entrecortado.

Si va lento:

- Elige una calidad más baja en **Ajustes** (`360p` o `160p`).
- Activa el **video liviano** en **Ajustes** (muestra la mitad de los cuadros para aliviar la CPU).
- Mantén la ventana en su tamaño normal en lugar de pantalla completa.

---

## Instalar

### En FLinux / Tiny Core

1. Instala las dependencias base: `fltk-1.3`, `curl`, `mplayer-cli`, `libwebp1` y `gcc_libs`.
2. Carga el paquete `.tcz` generado en `tcz/`:

```sh
tce-load -i ./sanson.tcz
```
