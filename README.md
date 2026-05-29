<div align="center">
  <img src="assets/icon.ico" alt="V.I.C" width="100"/>
  <h1>V.I.C — Video Image Comparator</h1>
  <p>Professional side-by-side video & image comparison tool<br>with real-time <strong>PSNR / SSIM / MSE</strong> metrics.</p>

  <a href="https://github.com/Crysisjim/VIC/releases"><img src="https://img.shields.io/badge/Version-2.1-blue" alt="Version"/></a>
  <a href="https://github.com/Crysisjim/VIC/wiki"><img src="https://img.shields.io/badge/📖_Wiki-Documentation-informational" alt="Wiki"/></a>
  <img src="https://img.shields.io/badge/Platform-Windows-lightgrey" alt="Platform"/>
  <img src="https://img.shields.io/badge/License-MIT-green" alt="License"/>
  <img src="https://img.shields.io/badge/GPU-OpenGL_3.3+-orange" alt="GPU"/>
  <img src="https://img.shields.io/badge/C%2B%2B-20-blue" alt="C++20"/>

  <br/><br/>

  🇬🇧 [English](#english) | 🇫🇷 [Français](#français)
</div>

---

<a name="english"></a>
## 🇬🇧 English

A high-performance native C++ tool for comparing two videos or images side-by-side, with real-time quality metrics (PSNR/SSIM/MSE), multiple view modes, ROI selection, and cinema mode.

[![📥 Download Portable](https://img.shields.io/badge/📥_Download-Portable_v2.1-blue?style=for-the-badge)](https://github.com/Crysisjim/VIC/releases/latest)

### What's new in v2.1

- **Cinema mode (F10)** — hides the entire UI to show only the comparison player
- **ROI / Crop** — draw a rectangle on the video to limit PSNR/SSIM/heatmap to a specific zone
- **Manual sync offset** — frame-accurate offset for videos that don't start at the same point
- **Window geometry saved** — position, size, and maximized state persist across sessions
- **Timestamp sync mode** — synchronizes by time rather than frame index (ideal for videos with different FPS)
- **Recent files** — quick reload of the last 8 pairs via File menu
- **Image support** — PNG, JPG, BMP, TIF/TIFF can be compared against videos or each other
- **All 19 keyboard shortcuts now functional** — Seek, Home/End, Speed, Modes, Metrics, Link were previously wired but inoperative
- **settings.ini** now saved next to the exe (not CWD) — settings no longer lost when launched from a different folder

### Features

| Feature | Description |
|---------|-------------|
| **Side-by-side wipe** | Draggable vertical split with zoom indicator |
| **Overlay** | Blend video 2 over video 1 with adjustable alpha |
| **Blink** | Alternating flicker between the two sources |
| **Click** | Hold left-click to toggle between sources |
| **Heatmap / Sub** | Absolute difference with JET colormap |
| **PSNR / SSIM / MSE** | Real-time metrics with scrolling graph history |
| **ROI** | Click-drag to restrict metrics to a zone |
| **Zoom + Pan** | Mouse wheel zoom (up to 8×), middle-click pan, linked or independent |
| **Sync modes** | Timestamp (recommended) or Frame Index; manual offset |
| **Cinema mode** | Full-screen player with hidden UI (F10) |
| **Fullscreen** | Native SDL2 fullscreen (F11) |
| **Drag & Drop** | Drop files directly onto left or right half |
| **Recent files** | Last 8 video/image pairs in File menu |
| **HW acceleration** | AUTO (FFMPEG → DSHOW fallback), Force DXVA2, Force CPU |
| **Variable speed** | 0.25× to 4× playback speed |
| **Persistent settings** | All options saved to `settings.ini` next to the exe |

### Quick Start — Portable (recommended)

1. Download `VIC_Portable_v2.1.zip` from [Releases](https://github.com/Crysisjim/VIC/releases)
2. Extract anywhere
3. Run `VIC.exe`
4. **Drag & drop** your files directly onto the window:
   - Drop on the **left half** → Media 1 | Drop on the **right half** → Media 2
   - Drop **two files at once** → both load automatically
   - Or use **File → Media 1… / Media 2…** for a file picker

> No installation required. No Python. No runtime dependencies beyond what's bundled.

### Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| `Space` | Play / Pause |
| `.` / `,` | Next frame / Previous frame |
| `Ctrl+→` / `Ctrl+←` | Seek +10 / −10 frames |
| `Home` / `End` | First frame / Last frame |
| `=` / `-` | Speed + / Speed − |
| `1` | Toggle Subtraction / Heatmap mode |
| `2` | Toggle Overlay mode |
| `3` | Toggle Blink mode |
| `4` | Toggle Click mode |
| `M` | Toggle Metrics (PSNR/SSIM/MSE) |
| `L` | Toggle Link (sync both timelines) |
| `Z` | Toggle Zoom Link |
| `R` | Reset ROI |
| `F10` | Cinema Mode (hide UI) |
| `F11` | Fullscreen |

All shortcuts can be rebound via **Options → Keyboard Shortcuts**.

### Supported formats

| Type | Formats |
|------|---------|
| **Video** | MP4, MKV, AVI, MOV, WEBM (via FFMPEG/DSHOW) |
| **Image** | PNG, JPG/JPEG, BMP, TIF/TIFF |

### Requirements

- **Windows 10/11** x64
- **GPU** with OpenGL 3.3+ (any modern GPU, including integrated graphics)
- ~30 MB disk space

No NVIDIA GPU required. No CUDA. No Python.

### Build from source (developers)

**Prerequisites:**
- Visual Studio 2022 Community
- [vcpkg](https://github.com/microsoft/vcpkg) at `C:\vcpkg`
- CMake 3.24+

```bat
git clone https://github.com/Crysisjim/VIC.git
cd VIC
rebuild.bat
```

The script configures CMake, compiles in Release, copies assets, and launches the exe automatically.

**vcpkg dependencies** (installed automatically via toolchain):
```
sdl2, sdl2-ttf, glew, opencv4[ffmpeg], imgui[sdl2-binding,opengl3-binding]
```

### Tech stack

| Library | Version | Role |
|---------|---------|------|
| C++ | 20 | Language |
| Dear ImGui | latest | Immediate-mode UI |
| SDL2 | 2.x | Window, OpenGL context, events |
| OpenGL / GLEW | 3.3 | GPU rendering |
| OpenCV | 4.x | Video decoding, image processing, metrics |

### Credits

- **Concept & Direction** — Crysisjim
- **Code & Architecture** — AI-assisted (Grok 4.0 60% · ChatGPT 5 20% · Gemini 3 Pro 20%)

### License

[MIT](LICENSE) — free to use, modify, and distribute.

---

<a name="français"></a>
## 🇫🇷 Français

Outil natif C++ haute performance pour comparer deux vidéos ou images côte à côte, avec métriques qualité temps réel (PSNR/SSIM/MSE), modes de vue multiples, sélection ROI et mode cinéma.

[![📥 Télécharger Portable](https://img.shields.io/badge/📥_Télécharger-Portable_v2.1-blue?style=for-the-badge)](https://github.com/Crysisjim/VIC/releases/latest)

### Nouveautés v2.1

- **Mode Cinéma (F10)** — masque toute l'interface pour n'afficher que le lecteur
- **ROI / Recadrage** — dessiner un rectangle sur la vidéo pour limiter PSNR/SSIM/heatmap à une zone
- **Offset de sync manuel** — décalage précis à la frame pour les vidéos qui ne démarrent pas au même instant
- **Géométrie de fenêtre sauvegardée** — position, taille et état maximisé persistent entre les sessions
- **Mode sync Timestamp** — synchronise par temps plutôt que par numéro de frame (idéal pour des FPS différents)
- **Fichiers récents** — rechargement rapide des 8 dernières paires via le menu Fichier
- **Support images** — PNG, JPG, BMP, TIF/TIFF comparables entre eux ou contre des vidéos
- **19 raccourcis clavier entièrement fonctionnels** — Seek, Home/End, Vitesse, Modes, Métriques, Link étaient câblés mais inopérants
- **settings.ini** sauvegardé à côté de l'exe — les paramètres ne sont plus perdus si lancé depuis un autre dossier

### Fonctionnalités

| Fonctionnalité | Description |
|----------------|-------------|
| **Wipe côte à côte** | Séparateur vertical glissable avec indicateur de zoom |
| **Overlay** | Superposer la vidéo 2 sur la vidéo 1 avec alpha ajustable |
| **Blink** | Clignotement alterné entre les deux sources |
| **Click** | Maintenir le clic gauche pour basculer entre les sources |
| **Heatmap / Soustraction** | Différence absolue avec colormap JET |
| **PSNR / SSIM / MSE** | Métriques temps réel avec historique graphique |
| **ROI** | Clic-glisser pour restreindre les métriques à une zone |
| **Zoom + Pan** | Zoom molette (jusqu'à 8×), pan clic-milieu, lié ou indépendant |
| **Modes sync** | Timestamp (recommandé) ou Index de frame ; offset manuel |
| **Mode cinéma** | Lecteur plein écran avec UI masquée (F10) |
| **Plein écran** | Plein écran natif SDL2 (F11) |
| **Drag & Drop** | Déposer les fichiers directement sur la moitié gauche ou droite |
| **Fichiers récents** | 8 dernières paires vidéo/image dans le menu Fichier |
| **Accélération HW** | AUTO (FFMPEG → DSHOW fallback), Force DXVA2, Force CPU |
| **Vitesse variable** | Lecture de 0,25× à 4× |
| **Paramètres persistants** | Toutes les options sauvegardées dans `settings.ini` à côté de l'exe |

### Démarrage rapide — Portable (recommandé)

1. Télécharger `VIC_Portable_v2.1.zip` depuis les [Releases](https://github.com/Crysisjim/VIC/releases)
2. Extraire n'importe où
3. Lancer `VIC.exe`
4. **Glisser-déposer** vos fichiers directement sur la fenêtre :
   - Sur la **moitié gauche** → Média 1 | Sur la **moitié droite** → Média 2
   - **Deux fichiers simultanément** → les deux se chargent automatiquement
   - Ou utiliser **Fichier → Média 1… / Média 2…** pour un sélecteur de fichier

> Aucune installation requise. Pas de Python. Pas de dépendances runtime au-delà de ce qui est inclus.

### Raccourcis clavier

| Raccourci | Action |
|-----------|--------|
| `Espace` | Lecture / Pause |
| `.` / `,` | Frame suivante / précédente |
| `Ctrl+→` / `Ctrl+←` | Avancer / reculer de 10 frames |
| `Début` / `Fin` | Première frame / Dernière frame |
| `=` / `-` | Vitesse + / Vitesse − |
| `1` | Activer/désactiver mode Soustraction / Heatmap |
| `2` | Activer/désactiver mode Overlay |
| `3` | Activer/désactiver mode Blink |
| `4` | Activer/désactiver mode Click |
| `M` | Afficher/masquer Métriques (PSNR/SSIM/MSE) |
| `L` | Lier/délier les deux timelines |
| `Z` | Lier/délier le zoom |
| `R` | Réinitialiser la ROI |
| `F10` | Mode Cinéma (masquer l'interface) |
| `F11` | Plein écran |

Tous les raccourcis sont reconfigurables via **Options → Raccourcis clavier**.

### Formats supportés

| Type | Formats |
|------|---------|
| **Vidéo** | MP4, MKV, AVI, MOV, WEBM (via FFMPEG/DSHOW) |
| **Image** | PNG, JPG/JPEG, BMP, TIF/TIFF |

### Prérequis

- **Windows 10/11** x64
- **GPU** avec OpenGL 3.3+ (n'importe quel GPU moderne, y compris intégré)
- ~30 Mo d'espace disque

Pas de GPU NVIDIA requis. Pas de CUDA. Pas de Python.

### Compiler depuis les sources (développeurs)

**Prérequis :**
- Visual Studio 2022 Community
- [vcpkg](https://github.com/microsoft/vcpkg) à `C:\vcpkg`
- CMake 3.24+

```bat
git clone https://github.com/Crysisjim/VIC.git
cd VIC
rebuild.bat
```

Le script configure CMake, compile en Release, copie les assets et lance l'exe automatiquement.

**Dépendances vcpkg** (installées automatiquement via le toolchain) :
```
sdl2, sdl2-ttf, glew, opencv4[ffmpeg], imgui[sdl2-binding,opengl3-binding]
```

### Stack technique

| Bibliothèque | Version | Rôle |
|-------------|---------|------|
| C++ | 20 | Langage |
| Dear ImGui | latest | Interface utilisateur immédiate |
| SDL2 | 2.x | Fenêtre, contexte OpenGL, événements |
| OpenGL / GLEW | 3.3 | Rendu GPU |
| OpenCV | 4.x | Décodage vidéo, traitement image, métriques |

### Crédits

- **Concept & Direction** — Crysisjim
- **Code & Architecture** — AI-assisté (Grok 4.0 60% · ChatGPT 5 20% · Gemini 3 Pro 20%)

### Licence

[MIT](LICENSE) — libre d'utilisation, de modification et de distribution.
