# Développer et modifier Plume

Le projet est du C++20 natif : Win32, Media Foundation et D3D11. Les deux exécutables partagent les mêmes fichiers source ; `PLUME_V0` active l’interface et le lecteur optimisé par défaut. Le runtime MSVC reste lié statiquement (`/MT`).

## Où intervenir

| Modification | Fichier |
| --- | --- |
| Point d’entrée Windows | `src/main.cpp` |
| Arguments, initialisation et arrêt de l’application | `src/application.cpp` |
| Types, état des écrans, règles de partage | `src/app_state.h` |
| Configuration INI, affectations persistées, autostart | `src/config.cpp` |
| WorkerW/Progman, placement, écrans actifs, pause plein écran | `src/desktop.cpp` |
| Notifications Windows, tray, réparation du bureau, cycle de vie | `src/lifecycle.cpp` |
| Journaux et audit mémoire | `src/diagnostics.cpp`, `src/memory_audit.h` |
| Création du périphérique GPU partagé | `src/video/gpu_device.cpp` |
| Lecture, timing, rendu D3D11 et aperçus GPU | `src/video/playback.cpp` |
| Lecture MP4 bornée et buffers compressés | `src/video/bounded_mp4.h` |
| Pilotage du décodeur H.264 matériel | `src/video/bounded_decoder.h` |
| Fenêtre de réglages, commandes et affectation d’une vidéo | `src/ui/settings.cpp` |
| Cartes d’écrans, dessin, mise en page et barre de progression | `src/ui/screen_cards.cpp` |
| Import CUDA/NVENC, processus, progression et annulation | `src/ui/gpu_import.cpp` |
| Contrat public de l’interface | `src/ui/settings.h` |
| État interne et IDs nommés des contrôles Win32 | `src/ui/settings_state.h` |
| Icône PNG/ICO et prompt de génération | `assets/`, `src/resources/` |

Chaque `.cpp` est compilé séparément. Les en-têtes déclarent les contrats ; ils n’incluent plus de blocs entiers d’implémentation d’interface. Les classes de démultiplexage/décodage restent autonomes et définies dans leurs en-têtes. Leurs dépendances sont explicites ; les tests réutilisent le même type d’erreur HRESULT que l’application.

## Flux de fonctionnement

`main` appelle `RunApplication`, qui charge la configuration, crée les fenêtres et lance un thread de lecture par écran affecté. La boucle Win32 traite les commandes et les notifications. `Tick` actualise les pauses, répare le placement et appelle l’interface pour suivre l’import.

Le thread de lecture utilise le périphérique GPU partagé. Le lecteur borné fournit les paquets compressés au MFT ; les images décodées restent des textures GPU. Le VideoProcessor les rend dans le fond et, lorsque l’interface est visible, dans un petit swapchain d’aperçu. Aucun second décodeur n’est ouvert pour les aperçus.

L’import est un processus FFmpeg distinct attaché à un job Windows. La progression lit les fichiers de rapport ; le code valide le MP4 avant de remplacer l’affectation. Annuler ou quitter arrête le processus et conserve la vidéo précédente.

## Règles importantes pour modifier le code

- `application` et les collections d’écrans appartiennent au thread UI. `state` est un accès **thread-local** à la cible courante ; un thread de lecture ne parcourt pas les enfants.
- Arrêter et joindre le thread de lecture avant de modifier ses options. Les indicateurs de pause/arrêt et les compteurs sont atomiques.
- Le contexte D3D11 partagé est protégé par `renderMutex`. Les surfaces d’aperçu sont protégées par `previewMutex` ; conserver l’ordre de verrouillage **aperçu puis GPU**.
- La fermeture de l’interface libère les swapchains d’aperçu. Elle laisse les fenêtres de fond et les décodeurs fonctionner.
- Les frames doivent rester sur le GPU. Ne pas ajouter de texture staging, de copie CPU ou de décodeur de prévisualisation.
- Les IDs des contrôles sont centralisés dans `ControlId`. Modifier leur position dans `BuildCards`, leurs actions dans le traitement `WM_COMMAND`.
- Le bureau Windows 11 moderne nécessite l’ordre **icônes → Plume → fond statique WorkerW**. Une présentation DXGI réussie ne prouve pas à elle seule la visibilité du fond.

## Compiler et vérifier

```powershell
.\build.ps1
.\tools\format.ps1 -Check
```

Puis lancer CTest avec le CTest fourni par Visual Studio : `ctest --test-dir build -C Release --output-on-failure`. Le test MP4 est enregistré lorsque les deux fichiers vidéo de test existent à la racine.

Le style est défini dans `.clang-format` : quatre espaces, lignes limitées à 110 caractères, pas de fonctions compressées sur une ligne. `tools/format.ps1` reformate les fichiers C++ ; `-Check` vérifie sans modifier.

Le formateur est un **outil de développement facultatif**, indépendant de la compilation et du lecteur. Cette session utilise clang-format 23.1.3 dans `tools/.formatter`, ignoré par Git. Pour le reproduire : `python -m pip install clang-format==23.1.3 --target tools/.formatter`. Un clang-format disponible dans PATH fonctionne également. Le script de format utilise `rg`.

L’icône est déjà livrée en ICO, donc sa génération n’est pas nécessaire pour compiler. Après modification du PNG, exécuter `tools/make_icon.py` avec Pillow, puis recompiler. Les versions facultatives des outils sont dans `tools/requirements-dev.txt`. L’ICO contient sept tailles et est embarqué comme ressource Windows ; aucun PNG externe n’est chargé à l’exécution.

Pour une modification du bureau ou du rendu, vérifier sur le bureau réel : images visibles derrière les icônes, deux vidéos différentes, aperçus, fermeture/réouverture de l’interface, pause et reprise. Les journaux se trouvent normalement dans `%LOCALAPPDATA%\PlumeWallpaperV0\session.log`. Les 50 Mo de RAM restent un objectif non atteint.
