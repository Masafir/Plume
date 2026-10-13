<div align="center">

<img src="assets/plume.png" alt="Icône de Plume" width="128" />

# Plume

**Un peu de mouvement sur votre bureau.**

Des fonds vidéo pour Windows, une vidéo par écran, avec un lecteur natif pensé pour limiter les ressources.

[**Télécharger la prérelease →**](https://github.com/Masafir/Plume/releases/tag/v0.2.0-alpha)

![Version](https://img.shields.io/badge/version-0.2.0--alpha-648cff)
![Windows](https://img.shields.io/badge/Windows-11%20x64-0078D4)
![C++](https://img.shields.io/badge/C%2B%2B-20-00599C)
![Licence](https://img.shields.io/badge/licence-MIT-38a169)

</div>

---

## Pourquoi Plume ?

Plume est né d’une idée simple : profiter d’un fond d’écran animé sans embarquer un navigateur ou un gros framework. Le projet utilise C++ et les composants vidéo de Windows pour lire des fichiers locaux derrière les icônes du bureau.

La priorité est de réduire la consommation de mémoire et de garder un fonctionnement discret. **Plume est encore en alpha** : cette première version permet de tester le lecteur, l’interface et le comportement sur plusieurs écrans. L’optimisation continue à partir de ces essais.

## Ce que vous pouvez faire

- **Une vidéo différente par écran**, depuis une interface avec des aperçus animés.
- **Lecture en boucle**, avec remplissage de l’écran ou affichage de l’image entière.
- **Préparation des vidéos sur le GPU NVIDIA**, avec progression et estimation du temps restant.
- **Imports limités à 30 images/s par défaut**, ou conservation de la cadence d’origine au choix.
- **Pause automatique** en plein écran, sur batterie, au verrouillage et pendant la veille.
- **Fonctionnement en arrière-plan** : fermer l’interface laisse les fonds actifs.
- **Lancement à l’ouverture de session**, facultatif.

Les vidéos originales restent intactes. Si une préparation est nécessaire, Plume crée une copie locale ; FFmpeg se ferme à la fin de cette étape.

## Installer et démarrer

1. Ouvrez la [prérelease **v0.2.0-alpha**](https://github.com/Masafir/Plume/releases/tag/v0.2.0-alpha).
2. Dans **Assets**, téléchargez **Plume-0.2.0-alpha-Setup.exe**.
3. Lancez le setup et choisissez vos raccourcis. L’installation se fait pour votre compte utilisateur, sans droits administrateur.
4. Si proposé, installez **FFmpeg** pour préparer vos vidéos sur le GPU NVIDIA. Cette option nécessite Internet.
5. Lancez **Plume**, sélectionnez un écran, puis cliquez sur **Choisir une vidéo…**. Répétez pour les autres écrans.

Pendant une préparation, la vidéo précédente reste active. Une vidéo 4K longue peut demander plusieurs minutes : la barre indique l’avancement réel.

**Au quotidien :** fermez l’interface pour laisser Plume tourner ; utilisez l’icône de notification pour la rouvrir, mettre en pause ou quitter.

> La version alpha est non signée : Windows peut afficher un avertissement d’éditeur inconnu. Téléchargez depuis la release du projet. Le [guide d’installation](docs/INSTALLATION.md) détaille les avertissements Windows, la version portable et la désinstallation.

## Configuration requise

| Élément | Prérequis |
| :--- | :--- |
| Système | Windows 11 64 bits, bureau Explorer et Media Foundation |
| Lecture vidéo | GPU compatible D3D11 et décodage matériel H.264 |
| Préparation GPU | NVIDIA compatible CUDA / NVDEC / NVENC, et FFmpeg |
| Stockage | Espace disponible pour les copies vidéo préparées |

La version actuelle a été testée sur **NVIDIA RTX 2070 SUPER**. La lecture sur Intel/AMD reste à valider ; la préparation GPU est actuellement réservée à NVIDIA. Windows N nécessite le Media Feature Pack.

Aucun runtime .NET, Electron ou Visual C++ supplémentaire n’est nécessaire pour le lecteur. Les vidéos déjà compatibles peuvent être lues sans FFmpeg.

## Et les performances ?

Le lecteur utilise **Media Foundation et D3D11**. Les images décodées restent dans des textures GPU ; le traitement vidéo assure la conversion et le redimensionnement sans rapatrier les pixels en mémoire CPU. Les aperçus réutilisent les images du fond déjà décodées.

Cela réduit le travail du lecteur, mais les composants Windows et le pilote ont aussi un coût mémoire. **L’objectif de moins de 50 Mo de RAM n’est pas encore atteint.** Les essais avec deux vidéos 4K à 30 images/s ont montré environ **120–160 Mo**, selon la session sur la machine de développement. Ces chiffres ne garantissent pas la consommation sur une autre configuration.

La prochaine étape est de réduire davantage les ressources en s’appuyant sur des mesures et des tests réels. Le projet ne promet pas une consommation nulle ni une compatibilité universelle.

## Vie privée et limites actuelles

Vos vidéos restent sur votre ordinateur. Plume ne collecte pas de télémétrie et ne nécessite pas Internet pendant la lecture. Seule l’installation facultative de FFmpeg utilise le réseau.

Le profil vidéo validé est **H.264 progressif SDR**. Le HDR et certains formats particuliers restent limités. Les configurations avec plusieurs GPU, les changements d’écrans, le DPI et la reprise après veille nécessitent encore des tests sur d’autres machines. Le cache des copies préparées ne se nettoie pas automatiquement.

## Un problème ou une idée ?

[Ouvrez une issue](https://github.com/Masafir/Plume/issues) avec la version de Plume, votre version de Windows, votre GPU, le nombre d’écrans et les étapes pour reproduire le problème. Pour un souci vidéo, précisez la résolution, le codec et la cadence si vous les connaissez.

Les journaux se trouvent dans `%LOCALAPPDATA%\PlumeWallpaperV0`. Relisez-les avant de les partager : ils peuvent contenir des chemins personnels.

## Pour les développeurs

Le projet est écrit en **C++20 / Win32**, avec CMake et MSVC. Le lecteur, le rendu, la gestion du bureau et l’interface sont séparés dans les sources.

Pour compiler, installez Visual Studio Build Tools 2022 avec les outils C++ x64, CMake et le Windows SDK **10.0.28000** ou ultérieur, puis lancez :

```powershell
.\build.ps1
```

La version avec interface est générée dans `dist\V0\PlumeV0.exe`.

| Documentation | Contenu |
| :--- | :--- |
| [Installation et utilisation](docs/INSTALLATION.md) | Installation, réglages et dépannage |
| [Développement](DEVELOPMENT.md) | Organisation du code et commandes utiles |
| [Architecture](ARCHITECTURE.md) | Choix techniques et pipeline vidéo |
| [Mesures mémoire](MEMORY-AUDIT.md) | Méthode d’analyse et résultats |
| [Distribution](docs/DISTRIBUTION.md) | Construction du setup et publication |
| [Revue de sécurité](docs/SECURITY-REVIEW.md) | Périmètre vérifié et limites de la revue |

## Licence

Plume est un projet ouvert sous [licence MIT](LICENSE), créé par **Masafir**. Vous pouvez utiliser, modifier et redistribuer le code en conservant les mentions de licence. FFmpeg et les autres composants conservent leurs propres licences : voir les [mentions tierces](THIRD-PARTY-NOTICES.md).
