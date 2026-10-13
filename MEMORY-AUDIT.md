# Audit mémoire — PlumeWallpaper

Date : 11 octobre 2026. Windows 11 build 26300, RTX 2070 SUPER, MSVC Release x64, CRT statique. Une vidéo H.264 SDR 4K 30 fps, durée 40,97 s, fichier `bojji-4k-h264-gpu.mp4`. La copie 1080p conserve la durée et la cadence. Un écran, politique de décodage matériel stricte.

Suite du développement le 13 octobre : voir `ARCHITECTURE.md` pour les réglages expérimentaux, la préparation GPU et leurs trois premières comparaisons. La copie préparée réduit le pool de 12 à 10 surfaces mais reste à environ 116 MiB de WS moyen dans l'essai court ; la purge au seek n'apporte pas de gain observé.

Le prototype MP4 borné + MFT direct en faible latence atteint ensuite **81,80 MiB de WS total moyen sur dix minutes**, médiane 77,57, pic échantillonné 89,59, 14 boucles et succès. WS privé moyen 51,33 MiB, plage 46,32–59,02. Environ 36 % de baisse du WS total par rapport au témoin initial ; cible 50 MiB encore manquée. Voir la deuxième implémentation et la validation prolongée dans ARCHITECTURE.md pour les limites et les compteurs distincts.

Le critère utilisateur est la **consommation pendant la lecture prolongée**. Un pic au lancement est acceptable ; les hausses répétées à chaque boucle comptent dans l'usage prolongé. La cible reste moins de 50 MiB de RAM système. Aucun changement d'architecture n'a été effectué dans cet audit.

## Résultat de lecture prolongée

Test de 600,38 secondes terminé : 18 001 frames présentées, HRESULT de succès, 14 boucles, CPU moyen 4,25 % d'un cœur. Après exclusion des 30 premières secondes, 114 instantanés espacés d'environ cinq secondes donnent :

| Mesure | Résultat |
|---|---:|
| Working set moyen | 127,22 MiB |
| Working set médian | 115,70 MiB |
| Minimum / maximum des instantanés périodiques | 89,58 / 233,29 MiB |
| Pic WS échantillonné plus fréquemment par le programme, session entière | 240,23 MiB |
| Engagement privé moyen / minimum / maximum périodiques | 290,66 / 253,40 / 397,11 MiB |
| Moyenne WS entre 30 et 130 s | 126,57 MiB |
| Moyenne WS sur les 100 dernières secondes mesurées | 119,04 MiB |

La moyenne est celle des instantanés réguliers, pas une intégrale continue exacte. Les hausses à chaque boucle font partie des résultats après stabilisation. Le maximum global du programme inclut le lancement, mais les relevés périodiques montrent également des pics après les bouclages. Pas de dérive ascendante démontrée sur dix minutes ; cela n'exclut pas une fuite plus lente sur plusieurs heures. La cible de 50 MiB est manquée sur le régime courant lui-même.

Sources : `audit/steady-4k-10min.log` et `audit/steady-4k-10min.csv`, calcul via `tools/summarize-audit.ps1`. Pendant la fin de l'essai, `nvidia-smi` relève 979 MiB de VRAM totale utilisée sur 8192 MiB, pilote 617.42 : aucune saturation globale observée à cet instant. WPR confirme ensuite qu'aucune capture n'est encore active.

La hausse principale vient du chemin de décodage Media Foundation / D3D11 / NVIDIA et de ses allocations système, avec une contribution du lecteur MP4. Elle ne vient pas principalement de la fenêtre Win32 ou d'un chargement de bibliothèques de 196 MiB.

Le working set 4K redescend vers 100–115 MiB après le lancement dans l'essai instrumenté de 55 secondes. Des pics reviennent au bouclage. L'essai de 150 secondes observe des creux autour de 94–108 MiB et un pic global de 238,83 MiB sur trois boucles. Le test de dix minutes confirme un niveau durable élevé avec des hausses répétées, sans croissance continue démontrée.

## Où la mémoire apparaît

Mesures du même essai 4K de 55 secondes ; MiB = 1 048 576 octets.

| Étape | Working set total | Working set privé | Engagement privé |
|---|---:|---:|---:|
| Début du processus | 10,15 | — | — |
| Périphérique D3D11 prêt | 34,62 | 8,74 | 49,21 |
| Source MP4 ouverte | 42,75 | — | 51,14 |
| Décodeur négocié | 44,88 | 11,34 | 64,51 |
| Swapchain / processeur vidéo prêts | 45,59 | 11,46 | 64,66 |
| Première frame décodée | 139,74 | 104,37 | 323,11 |
| Première présentation | 203,81 | 167,42 | 371,35 |
| Vers 10 secondes | 115,49 | 79,08 | 276,97 |

Source : `audit-4k-gpu-memory.log`. Les allocations se font aussi sur des threads internes ; deux relevés successifs ne sont pas une attribution exclusive à la dernière fonction appelée.

En 1080p, le même pipeline observe environ 72–74 MiB après stabilisation et un pic de 110,84 MiB sur 90 secondes (`audit-1080p-stages.log`). La résolution influence donc fortement la RAM système réelle, malgré l'absence de copies CPU de pixels dans notre code.

## Ce que la trace WPR démontre

Trace administrateur de 20 secondes, PID 23168 : 587 frames, succès, pic WS échantillonné 162,82 MiB. WPR a été arrêté et le fichier ETL enregistré. Les modules des piles sont résolus ; les noms de fonctions apparaissent en `?`, donc les fonctions internes exactes restent inconnues.

Les trois premières piles d'allocations engagées sont :

| Engagement cumulé sur cette pile | Chemin observé |
|---:|---|
| 143,44 MiB, un appel | `msmpeg2vdec.dll → d3d11.dll → nvwgf2umx.dll → DXCore / dxgkrnl / dxgmms2` |
| 72,00 MiB, six appels | `nvwgf2umx.dll → d3d11 / DXCore → dxgkrnl / dxgmms2` |
| 35,87 MiB, 111 appels | `RTWorkQ → mfmp4srcsnk.dll → mfplat.dll → ucrtbase.dll` |

La première pile implique explicitement le décodeur vidéo Microsoft et le pilote NVIDIA. La troisième implique le lecteur MP4 et ses buffers. L'agrégation par module au sommet attribue 275,19 MiB cumulés à `dxgmms2.sys`, 85,87 MiB à `ucrtbase.dll`, 30,09 MiB à `nvwgf2umx.dll`, et 0,30 MiB directement à `PlumeWallpaper.exe`.

**Ces valeurs sont des sommes d'engagements sur 20 secondes, pas des Mo résidents simultanément, et pas des pourcentages du working set.** Une allocation peut être libérée puis remplacée. Le processus termine avant la fin de la trace : les compteurs outstanding finaux à zéro ne prouvent pas l'absence de fuite pendant une session longue. Les allocations indirectement déclenchées par l'application sont comptées dans les modules internes, pas nécessairement dans l'exécutable.

Fichiers : `audit/allocations.etl`, `audit/allocation-stacks.txt`, `audit/allocation-images.txt`, `audit/trace-run.log`. L'ETL contient également des événements système ; il reste local et n'est pas publié.

## Pourquoi « textures GPU » ne garantit pas « zéro RAM système »

La texture de sortie est NV12, 3840 × 2160, 12 slices, accès CPU interdit. Le volume théorique des pixels du pool est environ 142,38 MiB, proche de l'allocation de 143,44 MiB dans la pile du décodeur. Cette proximité suggère une allocation liée au pool ; elle ne prouve pas une copie CPU de toutes les frames ni la nature exacte du backing du pilote.

`QueryVideoMemoryInfo` observe simultanément environ 288 MiB sur le segment GPU local, et environ 25,5 MiB sur le segment non local après stabilisation ; le segment non local atteint environ 133 MiB au lancement. Les GPU discrets peuvent donc utiliser un segment adossé à de la mémoire système. Les compteurs GPU et processus ne doivent pas être additionnés : leur comptabilisation peut se recouvrir.

Les deux grosses DLL NVIDIA ont ensemble environ 192 MiB de taille virtuelle d'image, mais seulement environ 12 MiB de pages résidentes observées. Présenter leur taille d'image comme 192 MiB de RAM serait faux. L'ensemble des pages d'images résidentes représente plutôt 37–38 MiB dans ces essais.

Le working set mesure les pages physiques résidentes, partagées comprises ; l'engagement privé mesure une obligation de backing, pas uniquement la RAM physique occupée. Références : [compteurs processus Microsoft](https://learn.microsoft.com/en-us/windows/win32/api/psapi/ns-psapi-process_memory_counters_ex), [segments GPU Microsoft](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_4/ne-dxgi1_4-dxgi_memory_segment_group).

## Vérifications d'hypothèses et limites

- Les vues d'entrée restent à 12 après chaque boucle, dans un tableau fixe de capacité 64. Pas de croissance du cache de vues observée.
- Un essai de décodage sans swapchain, sans VideoProcessor et sans Present atteint encore 233,49 MiB : le rendu final n'est pas nécessaire pour provoquer la hausse. Cet essai ne constitue pas une architecture alternative comparable : l'absence de Present modifie la soumission des commandes et les libérations différées du pilote.
- Pas de `Map`, `Lock` de pixels, staging ou conversion CPU applicative. Cela ne contrôle pas les allocations internes de Media Foundation et du pilote.
- Le diagnostic détaillé utilise temporairement environ 1 MiB pour énumérer le working set. Les instantanés principaux sont pris avant cette allocation ; cet outil ne peut expliquer les dizaines ou centaines de MiB supplémentaires.
- Aucun `EmptyWorkingSet`, aucune baisse artificielle des compteurs. Pas de conclusion sur deux vidéos 4K simultanées, un autre GPU ou une session de plusieurs heures.

## Pistes causales, par ordre de priorité

Recherche dans la documentation Microsoft, confrontée à la trace et à une inspection du flux H.264. Les tests ci-dessous sont des propositions, pas des changements déjà effectués.

| Piste | Éléments disponibles | Test qui permettrait de trancher |
|---|---|---|
| Pool de surfaces décodeur trop généreux pour notre usage | Confirmé : texture de 12 slices. Le SPS H.264 déclare `max_num_ref_frames=4`, `max_num_reorder_frames=2`, `max_dec_frame_buffering=4`. Les besoins de sortie/pilote s'ajoutent ; on ne peut pas fixer arbitrairement le pool à quatre | Interroger les attributs du MFT réel, son minimum de samples progressifs et ses exigences. Comparer un réglage supporté avant allocation avec le témoin |
| Allocations système du pilote et gestionnaire graphique | Confirmé : gros appels dans les piles décodeur → NVIDIA → `dxgmms2`. Nature exacte du backing non résolue | Comparer le même fichier sur un autre backend GPU, puis si nécessaire un autre GPU/pilote. Ne pas assimiler automatiquement ces allocations à des copies CPU de pixels |
| Lecture anticipée / buffers compressés du lecteur MP4 | Confirmé : 35,87 MiB cumulés sur une pile du lecteur MP4 en 20 s. Part résidente durable non isolée | Lire le flux compressé sans décodeur, à cadence bornée ; comparer aux instants correspondants. Cela sépare le coût démultiplexeur du coût vidéo |
| Réinitialisation au seek et chevauchement de buffers | Forte corrélation : les hausses reviennent au début de chaque boucle | Comparer seek actuel, purge SourceReader au bouclage et lecture continue de même durée sans seek. Mesurer cadence et RAM, une variable à la fois |
| Références COM / destruction GPU différée | Les vues restent à 12, sans croissance visible ; elles conservent le pool. D3D11 peut différer les destructions | Journaliser identités des textures, tester libération des vues au bouclage avec soumission/synchronisation contrôlée. Un Flush par frame serait coûteux et n'est pas une solution par défaut |
| Cache et fragmentation, plutôt que fuite | Retour répété à un niveau bas ; pas de dérive démontrée sur les essais courts | Comparer les mêmes positions dans plusieurs boucles et prolonger plusieurs heures. Le working set seul ne prouve pas une fuite ; examiner aussi engagement et allocations encore vivantes |
| Pression VRAM / pagination liée à d'autres logiciels | Possible en général, non démontrée par les seules mesures per-process | Relever budget GPU, usage global et mêmes essais avec charges de bureau comparables. Un segment non local utilisé ne suffit pas à prouver une saturation VRAM |
| Mauvaise métrique mémoire | Exclue comme explication complète : même les pages privées résidentes dépassent souvent 50 MiB | Conserver WS total, WS privé, engagement et segments GPU séparés ; ne pas additionner les compteurs qui se recouvrent |

Microsoft précise que le [pool D3D11 du décodeur](https://learn.microsoft.com/en-us/windows/win32/medfound/supporting-direct3d-11-video-decoding-in-media-foundation) comprend références, traitement et buffering. L'attribut [minimum de samples progressifs](https://learn.microsoft.com/en-us/windows/win32/medfound/mf-sa-minimum-output-sample-count-progressive) est ignoré par les MFT qui n'utilisent pas le mécanisme concerné : ce n'est pas un plafond universel de mémoire. [GetTransformForStream](https://learn.microsoft.com/en-us/windows/win32/api/mfreadwrite/nf-mfreadwrite-imfsourcereaderex-gettransformforstream) permet l'inspection/configuration du MFT sans remplacer d'emblée le Source Reader ; il interdit de traiter directement les données ou de modifier les types par ce pointeur.

[Flush du Source Reader](https://learn.microsoft.com/en-us/windows/win32/api/mfreadwrite/nf-mfreadwrite-imfsourcereader-flush) purge les samples en attente ; il ne garantit pas la restitution de tous les pools. [Flush D3D11](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-flush) soumet les commandes et aide aux destructions différées, mais reste asynchrone et ajoute du coût. Ces deux opérations ne sont pas interchangeables. Aucun bug de pilote précis n'est identifié ; aucune source trouvée ne démontre qu'un changement vers D3D9 ou D3D12 atteindra 50 MiB sur cette machine.

## Comparaison des méthodes possibles

| Piste | Avantages | Inconvénients / limite RAM |
|---|---|---|
| Optimiser le lecteur actuel : lecture MP4 et files bornées, gestion du seek | Conserve le code fonctionnel ; cible la contribution du lecteur et les hausses répétées | Le gros pool décodeur/pilote demeure ; moins de 50 MiB non garanti |
| Copie H.264 GPU avec moins de références et sans réordonnancement de frames | Conserve la résolution 4K ; peut réduire les besoins du décodeur sans réécrire l'application | Nouvelle conversion ; compromis compression/taille/qualité ; le pilote peut conserver un pool minimal important |
| MFT Media Foundation piloté directement, entrée compressée et nombre de frames en vol contrôlés | Plus de contrôle sur le buffering et les références ; conserve D3D11 et le rendu | Gestion asynchrone et transitions plus complexe ; certaines tailles de pool sont imposées par le décodeur/pilote |
| Prototype D3D9Ex + DXVA2 avec Media Foundation | Autre chemin de pilote, coût mesurable dans un petit prototype ; adapté au H.264 SDR | API ancienne, contraintes codecs/couleurs et multi-écrans ; aucun gain RAM démontré sur cette machine |
| Décodeur D3D11/D3D12 direct et démultiplexeur MP4 explicite | Contrôle maximal des surfaces et des files | Gros développement : parsing H.264, références, synchronisation et erreurs ; même pilote GPU, donc pas de garantie sous 50 MiB |

Le [Source Reader prend en charge D3D9/DXVA2 et D3D11](https://learn.microsoft.com/en-us/windows/win32/medfound/processing-media-data-with-the-source-reader). Un changement vers MediaEngine ne fournit pas, à lui seul, de raison mesurée d'éliminer les pools du même chemin Media Foundation/D3D11. Une implémentation NVDEC ajoute une dépendance NVIDIA et un contexte CUDA ; elle nécessite également une mesure réelle.

La prochaine méthode sera choisie avec l'utilisateur, après présentation du résultat prolongé. Aucun gain chiffré d'une architecture non testée n'est promis.

Recommandation : commencer par l'inspection du MFT et trois comparaisons isolées sur le pipeline actuel (nombre de surfaces supporté, buffers MP4, purge au seek), avant de sélectionner une autre architecture. Le pool et le pilote sont prioritaires pour abaisser le régime permanent ; le seek concerne surtout les pics récurrents. Si le coût reste imposé par ce backend, un prototype DXVA2/D3D9Ex permettrait une comparaison concrète ; le MFT direct donnerait davantage de contrôle sur les files mais conserverait le même pilote D3D11.

Le mode [faible latence Microsoft](https://learn.microsoft.com/en-us/windows/win32/medfound/mf-low-latency) est une piste expérimentale, pas une optimisation RAM garantie. Microsoft signale un effet possible sur la qualité en lecture locale. Il ne sera pas activé silencieusement ; une éventuelle comparaison devra contrôler les frames et les besoins de réordonnancement de la vidéo.
