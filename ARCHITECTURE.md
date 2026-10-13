# Décision d'architecture — 13 octobre 2026

Étude complémentaire : [THEORETICAL-LIMITS.md](THEORETICAL-LIMITS.md) détaille le budget restant et une mesure des capacités : le pilote annonce trois buffers pour H.264 4K, contre sept utilisés par le MFT. La présentation YUV directe constitue une expérience à envisager avant une réécriture complète ; aucun changement de moteur supplémentaire n'a été appliqué pendant cette étude.

## Objectif et point de départ

Minimiser la RAM système pendant la lecture prolongée, sans déplacer excessivement le coût vers le CPU, le GPU ou le disque. Cible : moins de 50 MiB pour une vidéo, puis mesurer séparément plusieurs écrans. Un pic au démarrage est acceptable. Le témoin actuel atteint 127,22 MiB de WS moyen et 115,70 MiB médian sur dix minutes, 14 boucles ; voir MEMORY-AUDIT.md.

## Direction retenue avec l'utilisateur

Construire un lecteur vidéo spécialisé C++ natif. Préparer les vidéos une seule fois dans un importateur indépendant, puis fermer cet outil. Conserver le décodage matériel du codec standard. La cible à long terme est une lecture/parsing bornés avec D3D11 vidéo directement piloté ; aucun gain RAM n'est présumé avant comparaison réelle.

Le lecteur conserve le host WorkerW, les affectations par écran et les pauses. Le périphérique GPU reste partagé. Les files et les surfaces doivent avoir des limites explicites ; les ressources sont réutilisées au bouclage. La compatibilité avec tous les codecs n'est pas la priorité. Une copie préparée peut être plus volumineuse ; l'original reste intact.

## Ordre de développement et critères de passage

1. Ajouter des réglages expérimentaux explicites et l'inspection du MFT actuel. Mesurer la texture réellement allouée, pas uniquement l'acceptation d'un attribut.
2. Préparer une copie H.264 4K sur GPU, demander une référence et aucune B-frame, puis vérifier les valeurs du bitstream. Comparer la vidéo préparée au témoin sur le même moteur et la même machine.
3. Comparer un minimum de sortie progressif réduit et une purge SourceReader au seek, une variable à la fois. Ces réglages restent opt-in jusqu'à validation.
4. Si le coût demeure dans Media Foundation / le pilote, présenter les résultats puis choisir avec l'utilisateur le prototype suivant : MFT directement piloté ou D3D11 vidéo direct. Un blocage imposant une nouvelle méthode nécessite une question avant bascule.
5. Retenir seulement les gains mesurés en RAM durable avec cadence correcte, CPU raisonnable, bouclage stable et absence de croissance. Valider ensuite dix minutes puis plusieurs heures, plusieurs écrans et le cycle veille/verrouillage.

## Alternatives

| Méthode | Intérêt | Limite |
|---|---|---|
| Media Foundation configuré | Première comparaison peu coûteuse ; infrastructure déjà fonctionnelle | Certaines exigences du décodeur/pilote restent imposées |
| D3D11 vidéo direct | Contrôle des références, surfaces et commandes | Parsing H.264 et synchronisation complexes ; même pilote |
| NVDEC direct | Minima de surfaces documentés | NVIDIA uniquement ; contexte CUDA et interop à mesurer |
| D3D12 vidéo | Gestion explicite | Complexité supplémentaire ; moins de RAM non garanti |
| Nouveau codec logiciel / shader | Format adapté possible | Perte de l'avantage du circuit vidéo ; coût CPU/GPU/disque potentiel |

## Autres applications et sources

Lively utilise mpv par défaut et propose d'autres moteurs, dont Media Foundation : https://github.com/rocksdanister/lively/wiki/Video-Guide . Wallpaper Engine décode les vidéos sur GPU et peut libérer la mémoire lorsque le bureau est masqué : https://help.wallpaperengine.io/en/videos/performance.html et https://help.wallpaperengine.io/en/performance/game.html . Ces fonctionnalités ne prouvent pas une consommation RAM donnée sur notre fichier.

NVIDIA documente la réduction des références et des surfaces : https://developer.nvidia.com/blog/optimizing-video-memory-usage-with-the-nvdecode-api-and-nvidia-video-codec-sdk/ . Les gains décrits concernent notamment la mémoire vidéo, pas une garantie de working set système sous 50 MiB.

Microsoft documente le minimum de sortie progressif : https://learn.microsoft.com/en-us/windows/win32/medfound/mf-sa-minimum-output-sample-count-progressive . L'attribut peut être ignoré. Il ne fixe pas le nombre total de références du codec.

## Première implémentation et mesures

- Ajout de l'inspection MFT et de `--decoder-samples N`, opt-in, sans changer le comportement normal. Le minimum progressif n'était pas présent dans les attributs du décodeur. La demande de 1 est acceptée mais le pool du témoin reste à 12 : pas de réduction démontrée.
- Ajout de `--flush-loop`, opt-in, purge synchrone SourceReader avant seek. Aucune purge GPU par frame.
- Importateur `tools/prepare-video.ps1` : CUDA pour décoder, NVENC pour encoder, pas de remplacement ni fallback CPU. Résolution conservée. La première copie 4K prend 20,47 s à préparer et occupe 68 660 635 octets, contre 64 263 860 pour le témoin. La demande `-refs 1 -bf 0` donne un SPS qui déclare 2 références, zéro réordonnancement et un buffering de 2, contre 4/2/4 auparavant.
- La copie préparée réduit le pool réel de 12 à 10 surfaces et le GPU local de 288,13 à 264,25 MiB.

Essais courts de 90 secondes, deux boucles, 12 relevés périodiques après les 30 premières secondes. Ces échantillons permettent de comparer des pistes ; ils ne remplacent pas la validation prolongée ni une comparaison visuelle de qualité. La préparation GPU était terminée avant les relevés retenus du premier essai, mais elle a eu lieu en parallèle avec son démarrage.

| Essai | Moyenne WS après 30 s | Médiane WS | Engagement privé moyen | Pic WS session | CPU un cœur |
|---|---:|---:|---:|---:|---:|
| Témoin, minimum sortie demandé = 1 | 121,95 MiB | 103,50 MiB | 285,84 MiB | 238,38 MiB | 4,08 % |
| Copie préparée, réglages décodeur par défaut | 115,94 MiB | 97,79 MiB | 255,84 MiB | 214,80 MiB | 3,98 % |
| Copie préparée, purge au bouclage | 117,58 MiB | 98,65 MiB | 258,15 MiB | 214,87 MiB | 3,57 % |

Sources : `audit/dev-pool1.log`, `audit/dev-prepared.log`, `audit/dev-prepared-flush.log`, en-têtes `audit/dev-lowmemory-headers.txt`. Tous les essais terminés ci-dessus ont décodé environ 2700 frames avec HRESULT de succès. Le gain observé est modeste en WS système ; moins de 50 MiB n'est pas atteint.

La purge ne montre aucun gain RAM dans cette comparaison et reste désactivée par défaut. Le minimum de samples demandé n'a pas réduit le pool dans le chemin de configuration testé ; cela ne prouve pas qu'aucun autre réglage ou ordre de négociation puisse avoir un effet. La préparation réduit effectivement les surfaces, mais ne suffit pas à l'objectif. La prochaine cible prévue reste le moteur spécialisé : isoler la lecture des samples compressés, puis piloter explicitement les surfaces de décodage D3D11. Le moteur historique sert de témoin ; une réécriture complète n'est pas encore livrée.

Validation de cette étape : MSVC Release x64 sans avertissements, imports de DLL Windows uniquement, syntaxe de l'importateur contrôlée. Tests visuels de qualité, de veille et de plusieurs écrans encore nécessaires. Aucun essai de dix minutes n'a été répété sur ces nouveaux réglages, car les premiers résultats ne les rendent pas candidats à la cible de 50 MiB.

## Deuxième implémentation : MP4 borné et MFT directement piloté

Le prototype `--bounded` utilise `src/video/bounded_mp4.h` pour lire les tables du MP4, construire les offsets et timestamps, puis lire un paquet compressé à la demande. Aucun mdat entier ni fichier complet n'est chargé en RAM. Les NAL AVCC deviennent Annex B dans les buffers réutilisés. Les références COM de chaque buffer empêchent une réécriture tant que le décodeur en conserve une référence. Les limites de taille et les formats refusés sont documentés dans README.md.

`src/video/bounded_decoder.h` pilote explicitement `ProcessInput`, `ProcessOutput`, les changements de type et le drain du MFT H.264 Microsoft, relié au gestionnaire D3D11. Le rendu et les contrôles de bureau sont réutilisés. Le MFT alloue encore ses propres surfaces : cette étape élimine le démultiplexeur et la gestion des files SourceReader, mais ne remplace pas encore le codec par des appels `ID3D11VideoDecoder` directs.

Le premier pool de huit paquets s'est avéré insuffisant : la sortie normale démarre après treize entrées. La limite a été portée à seize, puis les buffers dimensionnés selon les images clés et les autres paquets. Sur ce fichier : 4 871 246 octets compressés réservés. Les images clés sont assez espacées pour employer deux buffers de grande taille et des buffers plus compacts.

Le mode opt-in `--bounded-low-latency` configure `CODECAPI_AVLowLatencyMode` avant la négociation. La vidéo préparée n'a pas de réordonnancement ; le premier sample apparaît après une entrée au lieu de treize. Le pool compressé peut alors être borné à quatre buffers (1 709 726 octets sur ce fichier). Aucune modification silencieuse du réglage par défaut. Qualité visuelle à vérifier, nombre d'images contrôlé.

| Prototype | Durée | Moyenne WS après 30 s | Médiane WS | Engagement privé moyen | Pic WS session | CPU un cœur |
|---|---:|---:|---:|---:|---:|---:|
| MP4 borné, 16 buffers de taille maximale | 90 s | 111,36 | 96,41 | 134,59 | 168,41 | 4,19 % |
| MP4 borné, 16 buffers dimensionnés | 150 s | 100,53 | 91,50 | 125,21 | 163,56 | 3,49 % |
| Faible latence, 16 buffers dimensionnés | 90 s | 90,37 | 91,34 | 114,37 | 103,37 | 3,94 % |

Toutes les valeurs mémoire sont en MiB. Les essais courts sont des comparaisons exploratoires, pas une preuve d'un gain identique sur tout fichier. Le moteur faible latence à quatre buffers est soumis ensuite à dix minutes de lecture.

Le pool de sortie réel passe de dix surfaces sur le fichier préparé avec SourceReader à sept avec le MFT direct ; le GPU local passe de 264,25 à 228,69 MiB. Chaque boucle complète vérifiée restitue 1229 images pour 1229 paquets. Le compteur `pid_2696_...engtype_VideoDecode` affiche 13 % à un instant de l'essai prolongé ; cette mesure propre au processus confirme l'activité du circuit vidéo GPU (CSV dans audit/bounded-gpu-engines.csv).

L'engagement partagé atteint environ 215,86 MiB contre environ 132 MiB dans le témoin SourceReader : la répartition des engagements change aussi. La seule diminution de l'engagement privé ne doit pas être présentée comme une économie équivalente de RAM physique. Le working set total, le working set privé, l'engagement et les segments GPU restent distincts.

Validation : MSVC Release sans avertissements, imports Windows uniquement. CTest vérifie le fichier préparé, les timestamps/durée/rewind, les buffers encore retenus, les pools de 16 et 4, le rejet du réordonnancement et quatre structures invalides/tronquées. Les essais GPU vérifient le nombre d'images et l'absence de surfaces CPU. Le plein écran, la veille, plusieurs vidéos simultanées et la qualité visuelle restent à tester sur ce nouveau moteur.

### Validation prolongée du prototype à quatre buffers

Essai terminé en 600,39 secondes : 18 002 images présentées, 14 boucles complètes, HRESULT de succès. Chaque cycle complet comporte 1229 entrées et 1229 sorties. 114 instantanés périodiques après exclusion des 30 premières secondes :

| Compteur | Témoin initial, dix minutes | Prototype, dix minutes |
|---|---:|---:|
| Working set total moyen | 127,22 MiB | 81,80 MiB |
| Working set total médian | 115,70 MiB | 77,57 MiB |
| Minimum / maximum des relevés périodiques | 89,58 / 233,29 MiB | 76,69 / 89,59 MiB |
| Pic WS échantillonné session entière | 240,23 MiB | 89,59 MiB |
| Working set privé moyen | Non recalculé ici | 51,33 MiB |
| Working set privé minimum / maximum | Non recalculé ici | 46,32 / 59,02 MiB |
| Engagement privé moyen | 290,66 MiB | 106,60 MiB |
| CPU moyen, pourcentage d'un cœur | 4,25 % | 3,35 % |

Les premiers 100 s après warmup donnent 79,72 MiB de WS moyen ; les 100 dernières secondes donnent 81,76 MiB. Pas de croissance continue démontrée sur cette durée ; le niveau alterne entre environ 77 et 90 MiB. La RAM totale résidente diminue d'environ 36 % dans cette comparaison de chaînes complètes. La préparation de la vidéo et le moteur changent ensemble ; les essais intermédiaires isolent une partie des effets, mais ce pourcentage n'est pas attribuable uniquement au lecteur MP4.

La cible de moins de 50 MiB de WS total reste manquée. Même les pages privées résidentes ne restent pas toutes sous 50 MiB : moyenne 51,33, maximum 59,02. Ne pas présenter le minimum privé de 46,32 comme une réussite globale. Les DLL partagées font partie du WS total, mais leur coût physique n'est pas multiplié par chaque processus qui les partage.

Sources : audit/dev-bounded-lowlatency-10min.log et .csv, script tools/summarize-audit.ps1. Les réglages restent expérimentaux et non persistés. La prochaine étape prévue pour poursuivre la réduction consiste à remplacer le MFT par le pilotage direct des surfaces/références via ID3D11VideoDecoder, avec parsing H.264 explicite ; aucun gain sous 50 MiB n'est promis. Le prototype actuel reste utilisable pour comparer cette future implémentation au même fichier préparé.

