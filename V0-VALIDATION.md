# Validation V0 — 13 octobre 2026

- Release x64 MSVC compilée sans avertissement ; exécutable ~323 Kio, runtime /MT. Dépendances vérifiées avec dumpbin : bibliothèques Windows uniquement.
- Test CTest BoundedMp4Safety : réussi (buffers retenus, lecture/rewind, rejet des timestamps réordonnés et fichiers malformés).
- Test V0 natif 90 s : création des contrôles et inventaire d’écrans vérifiés ; import du MP4 HEVC 4K original par CUDA/NVENC terminé en ~22 s, sortie MP4 validée, lecture en boucle. 2 688 images, HRESULT 0. Original conservé.
- Test exécutable distribué, interface fermée, 65 s : 1 942 images, une boucle complète, HRESULT 0. Working set après 30 s : moyenne 86,52 Mio, intervalle 76,22–88,26 Mio. Working set privé moyen 56,13 Mio. CPU du programme : 3,71 % d’un cœur en moyenne. Ce court test ne remplace pas un essai prolongé.
- La dernière correction limite la préparation aux fichiers rejetés par la validation MP4 ; une erreur d’affectation ne déclenche plus un import. Recompilation Release réussie.

Restent à confirmer par le test utilisateur : plusieurs moniteurs physiques simultanés, changements de DPI/topologie, veille/reprise prolongée, annulation pendant la conversion et comportement des codecs atypiques.

Rapports locaux : v0-import-test.log et v0-final-test.log à la racine du projet (non distribués, ignorés par Git).

## Correctifs V0.1 et essais en session réelle

- Cause de l’invisibilité reproduite par inventaire Win32 : Progman possédait les enfants SHELLDLL_DefView, WorkerW, puis Plume dans cet ordre. Le fond statique WorkerW masquait Plume. Le placement est corrigé en SHELLDLL_DefView, Plume, WorkerW. Affichage confirmé par l’utilisateur.
- Référence technique : https://github.com/rocksdanister/lively/blob/d27589c821ed48248f2b9ff95e236400c20540a3/src/Lively/Lively/Core/WinDesktopCore.cs (décrit le bureau moderne Windows 11 et l’ordre des fenêtres). Aucun changement de décodeur ni de modèle de swapchain n’a été nécessaire.
- Aperçu GPU animé confirmé par l’utilisateur pour l’écran 1 ; les journaux comptent les images de l’aperçu et du fond sans créer un second décodeur.
- L’écran 2 attendait une préparation de la vidéo Persona : 3840×2194 à 120 images/s, durée 115 s. L’import a terminé après environ 236 s, puis les journaux ont enregistré 1 616 images sur DISPLAY2 sans erreur.
- Barre de progression vérifiée via les contrôles Win32 de Plume : position 84, texte 84 %, 93 s écoulées et environ 17 s restantes. Elle lit out_time_us produit par FFmpeg ; l’estimation ne constitue pas une garantie.
- Choix utilisateur : nouvelle copie Persona à 30 images/s. Décodage CUDA, encodage NVENC, cadence CFR 30 ; les données progress montrent les images supprimées et une vitesse proche du temps réel.
- CTest BoundedMp4Safety à nouveau réussi. La mesure initiale de 87 Mo était obtenue avec une fenêtre vidéo masquée et ne valide donc pas la RAM en usage visible. Les mesures réelles de plusieurs vidéos restent au-dessus de 50 Mo.
- Copie Persona terminée en environ 110 s, affectée à DISPLAY2. FFprobe confirme H.264, 3840×2194, cadence moyenne et nominale 30/1. Après affectation, les deux écrans comptent environ 300 images présentées toutes les dix secondes. RAM observée ensuite : environ 125 Mo avec l’interface ouverte.
- Retour utilisateur sur la barre : animation qui repartait en boucle. Le mode PBS_MARQUEE et les changements répétés du style ont été supprimés. La position ne change que lorsque le pourcentage progresse ; le dernier pourcentage connu est conservé si une lecture du fichier progress est momentanément incomplète.
- Vérification après correction de la barre : le nouvel import de Bojji a terminé et a été affecté à DISPLAY1 ; les deux vidéos continuent à environ 30 images/s. La barre finale est un contrôle Win32 dessiné avec un remplissage uni, sans minuterie ni animation de thème, pour éviter aussi le reflet animé des contrôles Windows. Elle est masquée après l’import terminé. L’application finale distribuée a été relancée avec les affectations conservées.

## Icône et refactoring — validation

- Icône générée avec image_gen, PNG avec alpha réel (0–255), ICO de sept tailles 16–256 px embarqué dans les deux exécutables. La fenêtre et le tray chargent la même ressource IDI_PLUME.
- Code réorganisé en véritables unités de compilation : application, configuration, bureau, cycle de vie, diagnostics, périphérique GPU, lecture, réglages, cartes et import GPU. En-têtes autonomes, identifiants des contrôles nommés, mise en forme clang-format 23.1.3.
- Release x64 des deux exécutables compilée sans avertissement. CTest BoundedMp4Safety réussi. Vérification de mise en forme réussie.
- Essai réel avec les deux vidéos persistées : premières images du fond et de l’aperçu sur DISPLAY1 et DISPLAY2, environ 30 images/s par écran, bouclage des deux vidéos constaté, aucune erreur enregistrée. La fermeture de l’interface libère les aperçus et laisse les fonds actifs.
- Dépendances de l’exécutable vérifiées avec dumpbin : bibliothèques Windows uniquement ; pas de redistribuable C++ ni d’outil Python/clang-format nécessaire à l’exécution.
