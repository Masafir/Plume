# Pourquoi le prototype dépasse encore 50 MiB — étude du 13 octobre 2026

## Conclusion

Une lecture 4K sous 50 MiB de working set total reste plausible, mais n'est pas démontrée sur cette machine. Aucun minimum universel Windows n'interdit ce résultat ; aucun document consulté ne le garantit. Le coût actuel appartient principalement au chemin décodeur/pilote/présentation et à ses allocations internes, plutôt qu'au petit lecteur MP4 natif. Il faut encore isoler l'allocation des pages privées du nouveau prototype. La trace WPR existante concerne l'ancien moteur SourceReader et ne fournit pas l'attribution exacte des 51 MiB privés du nouveau moteur.

## Budget observé

Un écran, H.264 4K préparé, mode borné faible latence, 600,39 s et 14 boucles.

| Élément | Mesure / portée |
|---|---|
| Working set total moyen | 81,80 MiB |
| Working set privé moyen | 51,33 MiB, plage 46,32–59,02 |
| Différence total − privé | 30,47 MiB en moyenne : part non privée, sans attribution exclusive à une DLL |
| Buffers compressés explicitement réservés | 1 709 726 octets, soit 1,63 MiB ; inclus dans les compteurs processus |
| Fenêtre/UI avant GPU | WS 12,44 MiB ; privé 1,82 MiB |
| Après création D3D11 | WS 34,72 ; privé 8,90 MiB |
| Après décodeur et renderer, avant première lecture | WS 41,89 ; privé 13,21 MiB |
| Code/images résidents dans un relevé détaillé de lecture | Environ 32 MiB ; DLL NVIDIA principales environ 12 MiB résidents, pas leurs 192 MiB de taille virtuelle |
| Segments GPU pendant la lecture | Local ~228,69 MiB ; non local ~25,5 MiB, compteurs séparés du working set |

Avec une part non privée inchangée de 30,47 MiB, atteindre un total de 50 exige de ramener le privé à environ 19,53 MiB : une baisse d'environ 31,80 MiB du privé moyen actuel. Optimiser 1,63 MiB de paquets ne suffit pas. Retirer les seules pages de code de `msmpeg2vdec.dll` (~0,8 MiB résidents) ne suffit pas non plus. Le gros gain espéré du remplacement du MFT doit venir du comportement d'allocation, pas de sa taille de DLL.

Les instantanés de première frame peuvent être suivis d'allocations sur des threads internes : la différence entre deux étapes ne constitue pas une attribution exclusive à la dernière fonction appelée.

Après arrêt des lecteurs/renders, le WS observé est encore 84,62 MiB ; juste après libération D3D11, il est 61,57 MiB, dont 42,19 MiB privés. Ce résidu est compatible avec caches, rétention de heaps et destructions différées, mais ne les prouve pas et ne démontre pas une fuite. Il ne constitue pas un plancher : au démarrage froid le processus ne prend que 10,15 MiB. Une attribution détaillée et une observation après délai manquent.

## Nouveau résultat : configuration annoncée par le pilote

L'outil de diagnostic `tools/gpu_caps.cpp`, cible CMake `GpuCaps` exclue du build standard, interroge les capacités D3D11 sans lire de vidéo ni remplacer le moteur.

Sur RTX 2070 SUPER, profil `D3D11_DECODER_PROFILE_H264_VLD_NOFGT`, NV12, 3840×2160 : trois configurations, `ConfigBitstreamRaw` = 1 / 2 / 2. Pour chacune, `ConfigMinRenderTargetBuffCount` = **3**. Même valeur en 1080p. Résultat conservé dans `audit/gpu-caps.txt`.

Le pool de sept n'est donc pas établi comme le minimum universel du matériel. Avec le SPS préparé qui déclare deux références et aucun réordonnancement, un prototype de trois ou quatre surfaces est justifié. Mais il doit respecter simultanément la configuration du pilote, les références H.264 et les surfaces encore utilisées pour présenter une image. La déclaration du pilote ne valide pas à elle seule un décodage correct à trois surfaces.

Une surface NV12 3840×2160 représente théoriquement 11,87 MiB de pixels. Sept : 83,06 MiB ; trois : 35,60 MiB ; quatre : 47,46 MiB. Réduire sept à trois enlèverait 47,46 MiB de pixels du pool **GPU**, hors alignements et métadonnées. Ce nombre n'est PAS une estimation de RAM système économisée ; le backing et les buffers du pilote peuvent évoluer différemment.

Références : [configuration du décodeur D3D11](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ns-d3d11-d3d11_video_decoder_config), [allocation des surfaces par un MFT](https://learn.microsoft.com/en-us/windows/win32/medfound/supporting-direct3d-11-video-decoding-in-media-foundation).

## Changements théoriques, par priorité expérimentale

1. **Isoler décodage et présentation.** Relever les allocations privées du nouveau moteur, comparer le décodage seul avec une soumission GPU contrôlée, puis la présentation. Sans Present, la gestion des commandes/libérations change : une ablation brute ne constitue pas une comparaison équivalente. Établir si le gros coût est dans le MFT, le pilote, le VideoProcessor ou la présentation.
2. **Tester la présentation YUV directe avant la réécriture complète.** `IDXGIFactoryMedia::CreateDecodeSwapChainForCompositionSurfaceHandle` accepte les buffers de décodage déjà existants ; `IDXGIDecodeSwapChain::PresentBuffer` présente une slice. Cela ouvre une voie DirectComposition sans imposer notre conversion vers des backbuffers BGRA séparés. Compatibilité derrière WorkerW, scaling/couleurs, durée de vie des surfaces et comportement du pilote doivent être mesurés. L'activation d'un overlay matériel n'est pas garantie, surtout avec la composition des icônes.
3. **Remplacer le MFT par le pilotage D3D11 vidéo direct si les surfaces internes restent coûteuses.** Le CPU parse SPS/PPS et les paramètres de slices, gère le DPB et les références ; le circuit VLD continue de décoder les pixels. Choisir trois/quatre surfaces selon les exigences réellement validées, sans file de plusieurs images ni allocations applicatives par frame. La complexité est principalement la correction H.264 et la synchronisation, pas la création d'une fenêtre. Le pilote conserve ses allocations internes : pas de garantie de gain système proportionnel au nombre de surfaces.
4. **Maîtriser l'encodage importé.** Pas de réordonnancement, minimum réel de références, pas de résolution/dimensions maximales surdimensionnées. Le réglage NVENC demandé n'est pas automatiquement la déclaration obtenue dans le SPS. H.264 intra-only retire des dépendances temporelles mais augmente potentiellement beaucoup le débit, le fichier et les accès disque ; le pilote peut conserver son minimum de trois surfaces. Ne pas en faire le profil par défaut sans comparaison.
5. **Finaliser un exécutable de lecture spécialisé.** Retirer le moteur témoin, ses imports et l'outil d'import résident (déjà externe), limiter les threads et les ressources à la vidéo réellement visible. Cela économise probablement des ressources secondaires ; aucune économie de 30 MiB par nettoyage de DLL n'est démontrée.

Références de présentation : [création d'une decode swap chain YUV](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_3/nf-dxgi1_3-idxgifactorymedia-createdecodeswapchainforcompositionsurfacehandle), [présentation d'une slice](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_3/nf-dxgi1_3-idxgidecodeswapchain-presentbuffer), [flip model et composition/overlays](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/for-best-performance--use-dxgi-flip-model). Le flip model est déjà utilisé ; ce n'est pas une optimisation oubliée à activer simplement.

## Alternatives et limites de la promesse

NVDEC donne des contrôles explicites sur les surfaces et une optimisation de leur allocation, mais ajoute le contexte CUDA et l'interopérabilité avec le rendu Windows. NVIDIA mesure historiquement un contexte de 84 MiB **de mémoire vidéo**, pas de RAM système, sur un autre test RTX 2070 ; ce chiffre ne doit pas être transformé en plancher de RAM. D3D12 donne une gestion plus explicite, mais n'efface pas le coût du pilote. D3D9Ex/DXVA2 est une autre implémentation du backend à comparer, avec des limites de compatibilité. Aucune de ces voies n'est prouvée plus légère sur le fichier étudié.

Références : [guide NVDEC et surfaces](https://docs.nvidia.com/video-technologies/video-codec-sdk/13.1/nvdec-video-decoder-api-prog-guide/index.html), [mesures NVIDIA de mémoire vidéo](https://developer.nvidia.com/blog/optimizing-video-memory-usage-with-the-nvdecode-api-and-nvidia-video-codec-sdk/), [résidence mémoire D3D12](https://learn.microsoft.com/en-us/windows/win32/direct3d12/residency).

Si le chemin pilote + présentation impose finalement plus de 50 MiB avec une seule vidéo 4K, réécrire du code applicatif ne supprime pas ce plancher observé. Les choix deviennent un autre backend/pilote, une résolution inférieure, ou un contenu différent (image fixe animée par quelques effets, au prix de ne plus restituer la vidéo arbitraire). Deux vidéos indépendantes 4K doivent être budgétées séparément ; le partage du périphérique n'élimine pas les pools distincts.

Le résultat honnête attendu d'un prochain prototype est un budget mesuré complet, incluant pages privées et non privées, RAM GPU partagée et VRAM séparément, sans les additionner aveuglément ni déplacer la mémoire dans un processus auxiliaire. Aucun EmptyWorkingSet, pagination forcée ou codec logiciel maison n'est recommandé pour afficher artificiellement un chiffre inférieur.
