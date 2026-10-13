# PlumeWallpaper

Fond vidéo Windows natif C++20 / Win32 / Media Foundation / D3D11. Sans .NET, Electron, mpv, audio ou framework UI. CRT MSVC lié statiquement (`/MT`) : aucun redistribuable Visual C++ à installer sur la machine cible. Les composants Media Foundation de Windows restent requis (Media Feature Pack sur Windows N).

## Compiler

Installer Visual Studio Build Tools 2022 ou ultérieur avec les outils C++ x64, CMake et **Windows SDK 10.0.28000** (contient `MF_READWRITE_USE_ONLY_HARDWARE_TRANSFORMS`). Dans PowerShell :

```powershell
.\build.ps1
```

Résultat : `dist\PlumeWallpaper.exe`. CMake génère également la solution MSVC dans `build`. Configuration Release x64, optimisation taille, runtime statique. Le script choisit le SDK installé le plus récent. Un SDK 26100 ancien ne contient pas l'attribut matériel strict. Ne pas distribuer une version Debug pour comparer la RAM.

## Utilisation

```powershell
.\dist\PlumeWallpaper.exe "D:\Videos\fond.mp4" --fit
.\dist\PlumeWallpaper.exe "D:\Videos\fond.mp4" --fill --span
.\dist\PlumeWallpaper.exe "D:\Videos\fond.mp4" --compat
.\dist\PlumeWallpaper.exe --list-screens
.\dist\PlumeWallpaper.exe --screen DISPLAY1 "D:\Videos\fond1.mp4" --screen DISPLAY2 "D:\Videos\fond2.mp4" --compat
.\dist\PlumeWallpaper.exe --autostart
.\dist\PlumeWallpaper.exe --quit
.\dist\PlumeWallpaper.exe --no-autostart
```

Sans argument : reprend le fichier enregistré ou ouvre le sélecteur Win32. Quitter l'instance existante avant de changer les options. Le chemin est converti en chemin absolu ; les URL ne sont pas acceptées. Les formats réellement lisibles dépendent des démultiplexeurs et codecs Windows ; une extension proposée par le sélecteur ne garantit pas sa lecture.

| Argument | Effet |
|---|---|
| `--fill` / `--fit` | Recadrage pour remplir (défaut) / bandes noires, ratio pixel respecté |
| `--primary` / `--span` | Écran principal (défaut) / vidéo étirée sur le rectangle du bureau virtuel |
| `--screen DISPLAYn fichier` | Affectation indépendante, répétable pour plusieurs écrans actifs |
| `--list-screens` | Affiche les noms Windows et dimensions des écrans actifs |
| `--strict` | Décodeurs matériels seulement, Windows 11 25H2 minimum (défaut) |
| `--compat` | DXVA demandé, décodeur logiciel éventuellement accepté si sortie GPU ; aucune garantie de décodage matériel |
| `--pause-on-battery` / `--on-battery` | Pause sur batterie (défaut) / lecture autorisée |
| `--autostart` / `--no-autostart` | Ajoute / retire la valeur HKCU Run ; retrait puis sortie immédiate |
| `--quit` / `--help` | Arrête l'instance de la session / affiche l'aide |
| `--test-seconds N --report fichier.log` | Arrêt automatique et rapport diagnostic (sans modale d'erreur) |
| `--audit-memory` | Relevés mémoire et segments GPU par étape, puis toutes les cinq secondes dans le rapport |
| `--decoder-samples N` | Expérimental : minimum de samples progressifs demandé au MFT (1–32). Ne garantit pas la taille totale du pool |
| `--flush-loop` | Expérimental : purge SourceReader avant le seek de bouclage |
| `--bounded` | Prototype : lecteur MP4 borné et MFT H.264 piloté directement, vidéo préparée sans réordonnancement |
| `--bounded-low-latency` | Avec `--bounded` : faible latence du décodeur, pool compressé de quatre buffers au lieu de seize |

Clic droit sur l'icône de notification : **choisir une vidéo pour chacun des écrans actifs**, pause manuelle ou quitter. Un sélecteur Win32 suffit ; aucun panneau permanent. Configuration : `%LOCALAPPDATA%\PlumeWallpaper\settings.ini`, avec une section `screens`. Autostart utilise la configuration enregistrée : conserver l'exécutable à son emplacement. Aucune modification du démarrage Windows tant que l'argument n'est pas demandé. `--primary`, `--span` ou un chemin vidéo positionnel permettent de revenir à une vidéo unique et effacent les affectations sauvegardées ; pour préserver plusieurs affectations, utiliser `--screen`.

## Pipeline et limites matérielles

La [décision d'architecture](ARCHITECTURE.md) consigne la direction du lecteur spécialisé et les étapes de comparaison. Les réglages expérimentaux ne sont pas persistés dans la configuration. L'inspection du décodeur est activée avec `--audit-memory` ou `--decoder-samples` ; les textures réellement livrées permettent de contrôler si un réglage a un effet.

Le prototype retire **SourceReader et le démultiplexeur MP4 Media Foundation** de la lecture : le lecteur natif charge les tables des samples, puis un paquet compressé à la fois. Il convertit les longueurs NAL en start codes Annex B dans des buffers compressés réutilisables ; ces copies concernent le flux compressé, pas les pixels. Les buffers ne sont réutilisés qu'une fois les références du décodeur libérées. `ProcessInput` / `ProcessOutput` et le drain sont pilotés directement, et les surfaces CPU restent refusées. Le MFT Microsoft conserve l'allocation interne des textures : ce prototype ne constitue pas encore un décodeur utilisant directement `ID3D11VideoDecoder`.

```powershell
.\dist\PlumeWallpaper.exe ".\bojji-4k-h264-lowmemory.mp4" --primary --bounded --bounded-low-latency
```

Profil expérimental : MP4 non fragmenté, un flux vidéo `avc1`, longueurs NAL sur quatre octets, H.264 SDR progressif préparé, pixels carrés, timestamps sans réordonnancement. Audio ignoré. Les tables ont des limites de taille, chaque paquet est limité à 2 MiB et aucun fallback vers un autre lecteur n'est automatique. Les edit lists MP4 ne sont pas appliquées : le temps du média commence à zéro. Les fichiers anamorphiques et les changements de description de sample ne sont pas pris en charge. Le moteur historique reste le défaut et sert de témoin.

La politique `MF_READWRITE_USE_ONLY_HARDWARE_TRANSFORMS` concerne le SourceReader historique ; elle ne s'applique pas au MFT explicitement créé dans `--bounded`. Ce moteur exige le support D3D11 du décodeur, lui fournit le gestionnaire DXGI et vérifie les surfaces livrées. Le moteur Video Decode du processus a été observé sur la RTX 2070 SUPER. Le mode faible latence reste opt-in : nombre d'images vérifié, qualité visuelle et autres fichiers encore à valider.

Préparer une nouvelle copie H.264 en demandant une référence et aucune B-frame (la déclaration du bitstream doit être vérifiée ; NVENC déclare deux références dans le premier essai) :

```powershell
.\tools\prepare-video.ps1 -InputVideo "C:\chemin\original.mp4" -OutputVideo ".\copie-4k.mp4"
```

Ce premier importateur utilise CUDA/NVENC et nécessite une NVIDIA compatible ainsi que FFmpeg uniquement pendant la préparation. Aucun fichier existant n'est remplacé et aucun fallback CPU n'est appliqué. La résolution est conservée ; la qualité et la taille doivent être comparées à l'original. Le script termine ensuite et ne reste pas résident. La prise en charge HDR n'est pas assurée par ce profil de préparation SDR.

Le Source Reader utilise `IMFDXGIDeviceManager`, active les transforms matériels et DXVA, désactive le traitement vidéo logiciel et ne sélectionne que le flux vidéo. Sortie NV12 ou P010, obtenue par `IMFDXGIBuffer`. Les surfaces CPU, staging et celles autorisant un accès CPU sont refusées. Pas de `Lock`, `Map`, conversion CPU ou lecture de pixels.

`ID3D11VideoProcessor` convertit/redimensionne directement vers un des deux backbuffers BGRA de la swapchain `FLIP_DISCARD`. Les vues de sortie sont réutilisées ; jusqu'à 64 vues d'entrée (texture/slice) sont mises en cache dans un tableau fixe. Leur création pendant le remplissage initial du pool et après un redimensionnement reste nécessaire. La boucle applicative n'utilise pas d'allocations de conteneurs par frame ; **les samples COM, le Source Reader et le pilote peuvent allouer en interne**. Une promesse de zéro allocation globale serait incorrecte.

Le mode strict utilise l'[attribut Microsoft dédié](https://learn.microsoft.com/en-us/windows/win32/medfound/mf-readwrite-use-only-hardware-transforms), disponible à partir de Windows 11 25H2. Il peut refuser un codec alors qu'un décodeur Windows utilisant DXVA serait disponible : le filtre concerne les MFT matériels. Sur Windows 10 / Windows 11 antérieur, employer `--compat`, puis vérifier l'activité **Video Decode** du GPU. Une texture GPU ne prouve pas le décodage matériel, comme l'explique la [documentation DXVA du Source Reader](https://learn.microsoft.com/en-us/windows/win32/medfound/mf-source-reader-disable-dxva).

En mode indépendant, chaque écran dispose d'un Source Reader, d'une fenêtre enfant et d'une swapchain ; **le périphérique et le contexte D3D11 sont partagés dans le même processus**. Les appels de rendu sont sérialisés, les attentes de cadence ne le sont pas. Deux vidéos ajoutent leurs pools de décodage et surfaces GPU ; la cible mémoire doit être mesurée pour l'ensemble du processus. Les affectations aux écrans déconnectés sont conservées, les fenêtres sont masquées et la lecture suspendue jusqu'au retour du moniteur. Les identifiants `DISPLAYn` peuvent changer si Windows réorganise la topologie : vérifier les affectations après une modification importante du matériel.

En cas d'échec de codec, convertir en H.264 8 bits SDR progressif compatible avec le GPU, ou choisir `--compat` en acceptant une consommation potentiellement supérieure. Aucun fallback WARP ni upload de frames CPU n'est implémenté. Vidéos HDR/BT.2020 et entrelacées refusées ; pas de tone mapping. HEVC/AV1 dépendent du GPU et des codecs installés. Le fichier doit permettre un seek pour boucler.

## Pauses et bureau

Le thread de lecture attend les timestamps vidéo avec un événement Win32, sans boucle active. `Present(1)` limite au rafraîchissement de l'écran : une vidéo plus rapide que l'écran ne maintiendra pas sa cadence nominale. Sur pause, ni lecture du prochain sample ni Present ; au plus un sample déjà en cours termine. Au retour, l'horloge est recalée sans rattraper la période de pause.

Le thread UI vérifie le plein écran et la batterie chaque seconde ; verrouillage, déconnexion et veille utilisent les notifications Windows. Une fenêtre au premier plan couvrant son moniteur déclenche la pause de la vidéo de cet écran ; en mode span cela suspend toute la vidéo même si seul un écran est occupé. Les applications plein écran non actives peuvent ne pas être détectées.

Attachement WorkerW via message Progman `0x052C`, fallback Progman en bas du Z-order. Cette interface Explorer n'est pas contractuelle et demande des tests Windows 10/11. DPI PerMonitorV2, coordonnées converties en coordonnées du parent, écrans à positions négatives pris en charge. `--span` est **une seule vidéo panoramique**, pas une vidéo répétée par moniteur. Les trous entre écrans occupent également de la VRAM. Un GPU unique gère le bureau ; les écrans sur plusieurs adaptateurs peuvent induire des copies par Windows et restent à tester.

Changement de résolution : recrée les backbuffers et les vues. Perte D3D signalée par DXGI : jusqu'à trois reconstructions du pipeline, lecture reprise au début. D'autres erreurs du décodeur entraînent un arrêt avec HRESULT. La reprise après veille doit être validée sur le pilote cible. Un timer associé au thread UI survit à la destruction des fenêtres par Explorer : il tente de recréer les fenêtres et pipelines quand le bureau redevient disponible. Ce mécanisme reste à vérifier après un redémarrage réel d'Explorer.

## Mesure RAM avant / après

La cible **< 50 MiB de working set** est un objectif, pas un résultat acquis. Les DLL Windows, buffers compressés, pilotes, mémoire GPU partagée et caches varient selon machine/codec. La VRAM dédiée n'est pas le working set ; sur iGPU elle utilise de la RAM physique partagée. La résolution peut donc avoir un coût système malgré l'absence de copie CPU applicative.

Sur la même machine et le même fichier, fermer l'autre logiciel, lancer Plume, attendre 30 secondes, mesurer 120 secondes :

```powershell
$wallpaperProcess = Get-Process PlumeWallpaper
.\tools\measure-memory.ps1 -ProcessId $wallpaperProcess.Id -Seconds 120 -Output plume-1080p.csv
```

Répéter avec 4K, puis une vidéo longue et au moins dix boucles. Comparer avec le PID du logiciel précédent via le même script. Le CPU est exprimé en pourcentage d'un cœur, non normalisé par nombre de processeurs. Le working set inclut les pages partagées ; PrivateBytes correspond à la mémoire privée engagée, pas à son seul sous-ensemble résident. Ne pas utiliser `EmptyWorkingSet` pour présenter un résultat artificiel.

| Essai | Avant (logiciel précédent) | Plume | Statut |
|---|---|---|---|
| 1080p — régime / pic WS | À mesurer | Environ 72–74 / 110,84 MiB (90 s) | Cible RAM manquée |
| 4K — régime / pic WS | À mesurer | Environ 100–115 / 238,83 MiB (150 s) | Cible RAM manquée |
| 4K — moyenne / médiane après 30 s | À mesurer | 127,22 / 115,70 MiB sur dix minutes, 14 boucles | Niveau durable élevé ; pas de dérive continue démontrée |
| Prototype `--bounded --bounded-low-latency`, 4K préparée | À mesurer | WS moyen 81,80 ; médiane 77,57 ; pic échantillonné 89,59 MiB, dix minutes | 14 boucles, succès ; cible 50 MiB encore manquée |
| Prototype — pages privées résidentes | À mesurer | Moyenne 51,33 ; plage 46,32–59,02 MiB | Compteur distinct du WS total |
| CPU 4K | À mesurer | 3,95 % d'un cœur sur l'essai de 150 s | Dépend du pilote et de l'instrumentation |

Voir [l'audit détaillé](MEMORY-AUDIT.md) pour les piles WPR, les allocations du pilote et la consommation sur plusieurs boucles. La consommation après stabilisation constitue le critère principal ; un pic isolé au démarrage n'est pas éliminatoire. Les pics qui reviennent au bouclage font partie de la lecture prolongée.

## Vérification fonctionnelle avant diffusion

- H.264 1080p puis 4K : image, couleurs SDR, cadence, bandes / recadrage, dix boucles sans croissance persistante.
- AV1/HEVC non supporté : erreur explicite en strict ; comportement documenté en compat.
- Plein écran : pause sous une seconde, reprise sans accélération ; batterie, verrouillage et veille.
- Résolution, orientation, DPI 100/150/200 %, bureau avec écran à gauche et déconnexion d'écran.
- Veille/réveil et perte GPU : reprise ou message d'erreur ; aucune fenêtre au-dessus des icônes.
- Arrêt via tray / `--quit`, autostart, suppression de l'autostart, chemins Unicode avec espaces.

Les mesures RAM ci-dessus ont été effectuées dans une session Windows interactive. Les essais visuels et de cycle de vie de cette liste restent à valider ; ils ne sont pas déduits du succès du décodage.

## Vérifications déjà effectuées

- Tests du lecteur borné : conversion Annex B, timestamps/durée, lecture complète et rewind, buffers retenus par un consommateur, pools de 16 et 4 buffers, rejet des timestamps réordonnés et de quatre structures tronquées/invalides. `BoundedMp4Tests` utilise les deux vidéos locales de référence ; le test CTest n'est enregistré que si elles existent. Exécution : `build\Release\BoundedMp4Tests.exe fichier-préparé.mp4 fichier-réordonné.mp4` (fixture utilisateur 3840×2160, 1229 samples).

- Compilation MSVC 19.44, Release x64, SDK 28000 : succès, aucun avertissement.
- Imports examinés avec `dumpbin /dependents` : DLL Windows uniquement, aucun runtime MSVC redistribuable.
- Vidéo utilisateur : MP4 **HEVC Main**, 3840×2160, 30 fps, SDR BT.709, durée 40,97 s. Media Foundation renvoie `0xC00D5212` (codec introuvable) sans extension HEVC, en strict et en compat ; les pics mémoire à zéro de ces essais ne sont pas des mesures de lecture.
- Copie MP4 H.264 4K créée séparément pour poursuivre les tests ; l'original est conservé. FFmpeg n'est pas une dépendance de l'exécutable.
# V0 avec interface

La version destinée au test utilisateur est [dist/V0/PlumeV0.exe](dist/V0/PlumeV0.exe). Son guide est [V0-README.md](V0-README.md). Elle utilise par défaut le lecteur borné à faible latence, propose une vidéo par écran actif et prépare les fichiers incompatibles sur le GPU NVIDIA avec le FFmpeg déjà installé.


## Maintenance du code

Le code est découpé par responsabilité dans `src/`, `src/video/` et `src/ui/`. Voir [DEVELOPMENT.md](DEVELOPMENT.md) pour savoir où intervenir, les règles de thread/GPU et les commandes de mise en forme. L’icône est embarquée depuis `assets/plume.ico` ; son PNG et son prompt sont conservés dans `assets/`.

## Distribution

Version de test : **0.2.0-alpha**, sous licence MIT. Le setup et les archives sont générés par `tools/build-release.ps1`. Voir [docs/INSTALLATION.md](docs/INSTALLATION.md) pour les utilisateurs et [docs/DISTRIBUTION.md](docs/DISTRIBUTION.md) pour préparer une publication et sa signature. Les artefacts locaux sont dans `dist/release/0.2.0-alpha`.
