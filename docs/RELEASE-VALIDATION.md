# Version 0.2.0-alpha — Validation de distribution

- Release x64 MSVC : les deux variantes compilent sans avertissement ; mise en forme vérifiée et CTest BoundedMp4Safety réussi.
- Métadonnées PE présentes : Plume, version 0.2.0.0 / produit 0.2.0-alpha, icône, original filename Plume.exe, licence MIT. L'éditeur descriptif n'est pas une identité certifiée.
- Setup Inno Setup 6.7.3 compilé : installation par utilisateur, architecture x64 hors ARM64, Windows 11 minimum, raccourcis et autostart facultatifs, FFmpeg séparé via WinGet si demandé.
- Installation réelle de test dans le dossier du projet : log « User privileges: None », « Administrative install mode: No », racine HKCU. Empreinte du programme installé identique au binaire distribué.
- Programme installé lancé huit secondes : inventaire/contrôles UI validés, lecture des deux vidéos configurées, résultats HRESULT 0. Le test court inclut le démarrage et ne constitue pas une mesure CPU/RAM stabilisée.
- Désinstallation réussie, binaire et inscription de désinstallation retirés ; log « Removed all? Yes », redémarrage non nécessaire. Données utilisateur et FFmpeg conservés. Version du workspace relancée après le test.
- Nouvelle restriction de protocoles FFmpeg testée sur un court extrait du fichier HEVC : CUDA/NVENC vers H.264 3840×2160 à 30 images/s réussi, sans méthode CPU.
- Archive source vérifiée : sources de compilation, licence et style présents ; aucune vidéo, configuration ou journal. Les outils installés du formateur sont exclus.
- Setup et programme : statut Authenticode NotSigned. Aucune promesse d'absence d'alerte SmartScreen ou de blocage Smart App Control.

Restent à vérifier avant une diffusion large : installation FFmpeg sur un compte/machine sans dépendance, comportement sans WinGet/Internet, autres GPU et Windows N, puis signature de l'application/setup/désinstalleur avec une identité reconnue. Aucun site ou dépôt public n'a été publié.
- Analyse personnalisée Microsoft Defender des artefacts de distribution : aucune menace détectée, sans quarantaine ni modification. Protection antivirus et temps réel actifs au contrôle. Ce résultat local ne constitue pas une certification SmartScreen.
