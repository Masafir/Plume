# Plume V0 — Test utilisateur

Double-cliquer sur **PlumeV0.exe**. Choisir un écran actif dans la liste, puis **Choisir une vidéo…**. Répéter pour chaque écran : les vidéos peuvent être différentes.

Les MP4 compatibles sont lus directement. Les autres vidéos sont préparées en H.264 par **décodage CUDA + encodage NVIDIA NVENC**, à leur résolution d’origine. L’original n’est pas modifié. La vidéo précédente continue pendant l’import. FFmpeg se ferme une fois la préparation terminée. Aucun repli CPU automatique : si le GPU ne prend pas en charge le fichier, l’import indique une erreur.

Cette V0 utilise le FFmpeg déjà installé sur cette machine (PATH ou installation WinGet Gyan). Pour l’import sur une autre machine, FFmpeg et une carte NVIDIA compatible sont nécessaires. **La lecture du MP4 préparé fonctionne sans FFmpeg ni runtime à installer** : C++ natif MSVC, runtime statique, bibliothèques Windows.

**Remplir l’écran** recadre ; **Vidéo entière** conserve l’image avec des bandes. Ce choix s’applique à tous les écrans. **Retirer de cet écran** arrête sa vidéo. **Pause** suspend tous les écrans. La pause automatique sur batterie, verrouillage, veille et plein écran est active.

Fermer l’interface laisse le fond d’écran fonctionner. Un clic sur l’icône de notification ou un nouveau lancement rouvre l’interface. **Quitter** arrête complètement l’application et l’éventuelle préparation GPU.

## Données et performances

Configuration et copies préparées : `%LOCALAPPDATA%\PlumeWallpaperV0`. Journaux de conversion dans le sous-dossier `cache`. Les affectations sont enregistrées automatiquement. Les anciennes copies restent dans le cache ; cette V0 ne fait pas encore de nettoyage automatique.

Lecteur actuel : décodeur matériel Media Foundation piloté directement, démultiplexage MP4 borné, quatre buffers compressés et rendu D3D11. Le seuil de 50 Mo **n’est pas atteint**. Sur la vidéo 4K testée, la mesure précédente de dix minutes donnait environ **82 Mo de working set moyen** et **51 Mo de working set privé moyen**. L’interface, les imports et plusieurs lectures simultanées peuvent augmenter ces valeurs. L’indicateur de l’interface affiche le working set total du programme ; FFmpeg est un processus séparé pendant l’import.

Profil vidéo validé : H.264 progressif SDR, pixels carrés, sans images B. Les HDR, vidéos verticales avec métadonnées de rotation et autres profils atypiques nécessitent des essais supplémentaires. L’import conserve la résolution ; il ne garantit pas encore la fidélité HDR ou la rotation des métadonnées.

## À tester

- Choisir ta vidéo 4K sur l’écran principal, puis une autre sur un deuxième écran si disponible.
- Vérifier les icônes du bureau, le recadrage, la boucle et la fluidité.
- Fermer l’interface et observer la RAM sur 10–30 minutes dans le Gestionnaire des tâches.
- Tester une application plein écran, verrouillage/déverrouillage, veille/reprise et changement de résolution.
- Relancer l’application et vérifier les vidéos affectées. Retirer une affectation et vérifier sa persistance.
- Tester l’annulation d’un import et un fichier invalide : la vidéo précédente doit rester active.

Pour le retour : écran concerné, fichier utilisé, action exacte, RAM/CPU observés et éventuel journal d’import. Les changements de topologie des écrans, la veille et le DPI restent à confirmer par le test utilisateur.

## Ligne de commande facultative

`PlumeV0.exe --autostart` active le lancement à l’ouverture de session.
`PlumeV0.exe --no-autostart` le désactive.
`PlumeV0.exe --quit` arrête la V0.

Compilation : exécuter `build.ps1` à la racine du projet avec MSVC Build Tools C++ et CMake. Le résultat est copié dans `dist\V0`.
## Mise à jour V0.1

Le placement du fond sur les bureaux Windows 11 récents est corrigé : la vidéo est placée entre les icônes et le fond statique de Windows. L’ordre est surveillé après les changements du bureau.

L’interface affiche des cartes pour les écrans actifs et des **aperçus animés GPU** (maximum 15 images/s). Cliquer sur une carte sélectionne son écran. Les aperçus réutilisent les images décodées du fond ; ils n’ouvrent pas un second décodeur. Ils s’arrêtent quand l’interface est masquée ou réduite ; fermer l’interface libère leurs surfaces.

Pendant l’import, une barre affiche le pourcentage calculé sur la durée de la vidéo, le temps écoulé et une estimation du temps restant. Pendant le démarrage ou si la durée n’est pas disponible, elle reste à zéro avec un message explicite, sans animation en boucle. 100 % signifie que le processus s’est terminé et que le MP4 a été validé puis affecté. Le bouton **Annuler l’import** conserve la vidéo précédente.

**Prochains imports** permet de choisir une copie à 30 images/s ou de conserver la cadence d’origine. Le profil à 30 images/s est désormais le choix par défaut. Il conserve la résolution et utilise CUDA/NVENC ; l’original n’est pas modifié. Il ne change pas les vidéos déjà affectées : il faut les importer à nouveau pour changer leur cadence.

La mesure initiale de 87 Mo ne représentait pas correctement le fond visible, car la fenêtre était masquée par Windows. Après correction, une session avec le fond visible et les aperçus a utilisé environ 123 Mo ; deux lectures 4K ont atteint environ 160–200 Mo sur cette machine. Le passage à 30 images/s réduit la charge de lecture ; il ne garantit pas moins de 50 Mo de RAM.

Le journal de diagnostic est écrit dans `%LOCALAPPDATA%\PlumeWallpaperV0\session.log` lors d’un lancement normal. Il contient l’état des écrans, les images présentées, les imports et les compteurs RAM.


## Icône et réorganisation du code

L’icône Plume est embarquée dans l’exécutable et utilisée par la fenêtre et la notification. Le code est découpé en fichiers C++ compilés séparément, avec des IDs nommés pour les contrôles et un style uniforme. `DEVELOPMENT.md` indique où modifier chaque comportement. Cette réorganisation n’ajoute aucune dépendance à l’exécution.
