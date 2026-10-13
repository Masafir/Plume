# Plume — Installation et utilisation

Plume affiche une vidéo derrière les icônes de chaque écran. Cette version **0.2.0-alpha** est une première version de test destinée à Windows 11 64 bits.

## Installation recommandée

1. Télécharger **Plume-0.2.0-alpha-Setup.exe** depuis la page officielle de publication.
2. Fermer une éventuelle version de Plume déjà ouverte, puis lancer le setup.
3. Choisir les raccourcis et, si nécessaire, **Installer FFmpeg** pour préparer les vidéos sur un GPU NVIDIA. Cette option nécessite Internet et télécharge une application tierce via WinGet ; elle est facultative.
4. Lancer **Plume** depuis le menu Démarrer.

L’installation se fait dans le compte utilisateur, sans droits administrateur. Aucun runtime Visual C++, .NET ou Electron n’est à installer pour le lecteur. Le lancement automatique à l’ouverture de session est facultatif et désactivé par défaut.

Si WinGet ou Internet ne sont pas disponibles, installer Plume sans FFmpeg, puis consulter la section ci-dessous. Le setup indique si l’installation de FFmpeg a échoué ; cela n’empêche pas la lecture d’une vidéo déjà compatible.

## Version portable

Télécharger **Plume.exe**, le placer dans un dossier de son choix et le lancer. Cette version n’ajoute pas de raccourcis ni d’entrée de désinstallation. Pour disposer aussi du guide et de la licence, télécharger le ZIP portable et l’extraire avant lancement.

## Configuration requise

- Windows 11 64 bits, avec le bureau Explorer et Media Foundation. Windows 10 n’a pas encore été validé. Windows N nécessite le Media Feature Pack.
- Carte graphique prenant en charge D3D11, le traitement vidéo et le décodage matériel H.264, avec un pilote à jour. Le fonctionnement a été vérifié sur NVIDIA ; Intel/AMD restent à tester pour la lecture.
- **Import GPU : carte NVIDIA compatible CUDA/NVDEC/NVENC et FFmpeg.** L’import GPU ne prend actuellement pas en charge Intel/AMD ; aucun repli CPU automatique n’est utilisé.
- Assez d’espace disque pour les copies préparées. Une vidéo 4K peut occuper plusieurs centaines de Mo ; les originales restent intactes.

## Choisir ses vidéos

Sélectionner une carte d’écran, puis **Choisir une vidéo…**. Répéter pour les autres écrans : les vidéos peuvent être différentes. Les aperçus animés réutilisent les images décodées du fond.

Un MP4 H.264 progressif SDR, avec pixels carrés et sans images B, peut être lu directement. Les autres fichiers courants sont préparés sur le GPU, à leur résolution d’origine. Le choix **Prochains imports** permet de limiter à 30 images/s ou de conserver la cadence d’origine. Une vidéo déjà compatible dont la cadence dépasse la limite choisie est également préparée.

La barre avance selon le travail réellement effectué. Elle indique le pourcentage, le temps écoulé et une estimation du temps restant. Elle reste à zéro pendant l’analyse initiale et n’affiche pas d’animation en boucle. Une vidéo longue ou à haute cadence peut prendre plusieurs minutes. La vidéo précédente continue pendant la préparation.

**Remplir l’écran** recadre ; **Vidéo entière** conserve l’image avec des bandes. **Retirer de cet écran** arrête son fond. **Pause** suspend les fonds. Le logiciel se met aussi en pause sur batterie, lors d’un plein écran sur l’écran concerné, du verrouillage et de la veille.

Fermer l’interface laisse les fonds actifs. L’icône de notification ou un nouveau lancement rouvre l’interface. **Quitter** arrête Plume et une éventuelle préparation en cours.

## Installer FFmpeg séparément

Dans un terminal, avec WinGet :

```powershell
winget install --id Gyan.FFmpeg --exact --source winget --scope user
```

Ou télécharger un build Windows depuis [la page FFmpeg pour Windows de Gyan](https://www.gyan.dev/ffmpeg/builds/), l’extraire et placer `ffmpeg.exe` dans le sous-dossier `tools` situé à côté de `Plume.exe`. La structure devient `Plume.exe` et `tools\ffmpeg.exe`.

Plume cherche FFmpeg dans ce dossier, dans PATH, puis dans les installations WinGet Gyan du compte utilisateur. FFmpeg ne tourne que pendant la préparation. Il est indépendant de Plume et conserve sa propre licence GPLv3 et ses propres conditions de distribution. Il n’est pas inclus dans ce setup.

## Avertissements de Windows

Cette version de test est **non signée**. Windows peut indiquer « Éditeur inconnu » ou « Windows a protégé votre ordinateur ». Cet avertissement de réputation ne constitue pas à lui seul une détection de malware. Il ne garantit pas non plus qu’un fichier est sûr.

Télécharger uniquement depuis la publication du projet et comparer le SHA-256 avec celui fourni séparément sur cette publication. Un SHA-256 permet de vérifier le fichier reçu ; il ne remplace pas une signature ni la confiance dans la source.

Si Microsoft Defender indique une menace précise, ou si Smart App Control bloque le fichier, conserver le nom de la détection et contacter le projet avant de poursuivre. Il n’est pas nécessaire de désactiver l’antivirus, d’ajouter une exclusion ou de désactiver Smart App Control pour utiliser les fonctions du logiciel. La signature d’éditeur reconnue reste à mettre en place pour les prochaines distributions.

Documentation : [réputation SmartScreen](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation), [Smart App Control](https://learn.microsoft.com/en-us/windows/apps/develop/smart-app-control/overview).

## Mise à jour et désinstallation

Fermer Plume, puis lancer le nouveau setup. Les affectations et les copies sont conservées. Pour la version portable, remplacer l’exécutable après fermeture.

Désinstaller depuis **Paramètres Windows → Applications → Applications installées → Plume**. La désinstallation retire le programme, les raccourcis et l’éventuelle entrée de lancement automatique créée par le setup. Elle conserve les vidéos originales et les données utilisateur. FFmpeg, installé séparément, n’est pas retiré automatiquement.

Les données se trouvent dans `%LOCALAPPDATA%\PlumeWallpaperV0` : `settings.ini`, le dossier `cache` et `session.log`. Le cache ne se nettoie pas automatiquement. Pour supprimer les copies et réglages après désinstallation, supprimer manuellement ce dossier ; ne pas le faire pendant que Plume fonctionne.

## Limites et dépannage

- **RAM :** la cible de 50 Mo n’est pas atteinte. Sur la machine de développement, deux vidéos 4K à 30 images/s ont utilisé environ 120–160 Mo selon la session ; les résultats varient selon le GPU, le pilote et les fichiers.
- **Fond absent :** vérifier que l’écran a une vidéo affectée et que le logiciel n’est pas en pause. Actualiser les écrans, puis quitter et relancer Plume après un redémarrage d’Explorer.
- **Import impossible :** vérifier FFmpeg et la compatibilité du GPU avec le codec. Lire le journal d’import dans `cache`. Ne pas installer des codecs ou changer le mode GPU au hasard.
- **HDR, entrelacement, rotations, codecs atypiques :** prise en charge limitée. Le profil validé est H.264 progressif SDR ; la préparation ne garantit pas la fidélité HDR.
- **Plusieurs cartes graphiques, branchement/débranchement d’écrans, DPI et veille :** essais supplémentaires nécessaires sur d’autres machines.

Pour signaler un problème : version de Plume, Windows, modèle de GPU/pilote, nombre d’écrans, fichier vidéo et action exacte. Un journal peut contenir des chemins personnels ; le relire avant de le partager.

## Confidentialité et licence

Plume ne collecte pas de télémétrie et ne télécharge rien pendant la lecture. L’installation facultative de FFmpeg via WinGet nécessite une connexion ; le lecteur et l’import de fichiers locaux fonctionnent ensuite hors ligne. Les vidéos restent sur l’ordinateur. L’import FFmpeg autorise uniquement les protocoles locaux `file` et `pipe`.

Plume est fourni sous **licence MIT**, incluse dans `LICENSE`. Le code source accompagne la publication dans une archive séparée. Cette licence ne remplace pas celles de Windows, NVIDIA ou FFmpeg.
