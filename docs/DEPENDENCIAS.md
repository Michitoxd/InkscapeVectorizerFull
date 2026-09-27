# Dependencias — InkscapeVectorizerFull (v1.2)

## Obligatorias

| Paquete (Fedora / Debian) | Uso |
|---|---|
| `glibmm24-devel` / `libglibmm-2.4-dev` | Glib, Glib::ustring, threads |
| `gdk-pixbuf2-devel` / `libgdk-pixbuf-2.0-dev` | Carga de imágenes (PNG, JPG, BMP, …) |
| `gdkmm30-devel` / `libgdkmm-3.0-dev` | Wrapper C++ de GdkPixbuf |
| `lib2geom-devel` (o submódulo) / `lib2geom-dev` | Geometría de curvas y escritura de paths SVG |
| `libxml2-devel` / `libxml2-dev` | Validación/serialización XML del SVG |
| `potrace-devel` / `libpotrace-dev` | Motor de trazado binario (backend de Potrace) |
| `cmake`, `gcc-c++` / `build-essential` | Sistema de build |

## Opcionales

| Paquete (Fedora / Debian) | Uso |
|---|---|
| `gtkmm30-devel` / `libgtkmm-3.0-dev` | Solo la GUI (`trace-bitmap-gui`). Se puede omitir compilando con `-DIVF_BUILD_GUI=OFF` |

## Ya NO necesarias (desde v1.1)

- **AutoTrace:** el motor y el código vendido (`src/core/3rdparty/autotrace/`)
  fueron eliminados junto con los modos centerline/autotrace.
  (La GUI GTK3 regresó en v1.3 como componente opcional.)
- **AutoTrace:** el motor y el código vendido (`src/core/3rdparty/autotrace/`)
  fueron eliminados junto con los modos centerline/autotrace.
- **boost:** nunca fue obligatoria en runtime; el único uso era
  `boost::hash<Geom::Point>` declarado en el `inkscape-potrace.h` extraído.
  Si tu paquete de lib2geom arrastra headers de boost, instálalos, pero este
  proyecto no los requiere.

## Instalación

**Fedora:**

```bash
sudo dnf install cmake gcc-c++ \
    glibmm24-devel gdk-pixbuf2-devel gdkmm30-devel \
    lib2geom-devel libxml2-devel potrace-devel
```

**Debian/Ubuntu:**

```bash
sudo apt install cmake g++ \
    libglibmm-2.4-dev libgdk-pixbuf-2.0-dev libgdkmm-3.0-dev \
    lib2geom-dev libxml2-dev libpotrace-dev
```

**Windows (MSYS2):**

```bash
pacman -S mingw-w64-ucrt-x86_64-{toolchain,cmake,glibmm,gdk-pixbuf2,gtkmm3,libxml2,potrace}
```

**macOS (Homebrew):**

```bash
brew install cmake pkg-config glibmm@2.68 gdk-pixbuf gdkmm gtkmm3 libxml2 potrace
```

(lib2geom puede requerir compilarla desde fuente en macOS y Windows.)

## Notas

- gettext/NLS: no se usa; todos los mensajes de la app están en inglés/español
  literal en el código.
- potrace se enlaza como librería del sistema (`find_library(POTRACE ...)`);
  no hay copia vendida (Inkscape usa la suya propia, misma licencia GPL).
