# Solución de problemas — TinyTV

Guía rápida para resolver fallos habituales al usar o instalar **TinyTV**.

## Los plugins no funcionan o no cargan

* **Verifica `comun.js`:** si ningún plugin funciona o aparece error al cargar, revisa que `comun.js` exista dentro de la carpeta de plugins de tu configuración:

```bash
ls -la ~/.config/tinytv/plugins/comun.js
```

* **Si falta `comun.js`:** cópialo desde el directorio del sistema o del repositorio (por ejemplo `/usr/share/tinytv/plugins/comun.js` o `/tmp/tinytv/plugins/comun.js`), o descárgalo directamente desde GitHub:

```bash
mkdir -p ~/.config/tinytv/plugins
cp /usr/share/tinytv/plugins/comun.js ~/.config/tinytv/plugins/comun.js
```

* **Descargar `comun.js` si no está disponible localmente:**

```bash
mkdir -p ~/.config/tinytv/plugins
curl -fsSL "https://raw.githubusercontent.com/IamJony/tinytv/refs/heads/main/plugins/comun.js" -o ~/.config/tinytv/plugins/comun.js
```

## El video va lento o entrecortado

* Selecciona una calidad menor en **Ajustes** (`360p` o `160p`).
* Activa el **video liviano** en **Ajustes** para reducir la carga de cuadros en equipos con poca CPU o sin aceleración gráfica.
* Evita usar pantalla completa en equipos modestos y mantén la ventana en su tamaño normal.