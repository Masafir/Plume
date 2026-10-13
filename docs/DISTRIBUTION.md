# Préparer une distribution de Plume

## Artefacts disponibles

`tools/build-release.ps1` produit dans `dist/release/<version>/` :

- `Plume-<version>-Setup.exe` : installation dans le compte utilisateur, raccourcis, désinstallation, option FFmpeg via WinGet ;
- `Plume.exe` et le ZIP portable avec README/licence ;
- une archive du code source MIT ;
- `SHA256SUMS.txt` et `RELEASE.json`, avec versions, tailles, empreintes et statut des signatures.

Le setup n'embarque pas FFmpeg, n'installe aucun pilote/service et ne demande pas les droits administrateur. L'option d'import télécharge séparément `Gyan.FFmpeg` via WinGet, avec consentement dans l'assistant. WinGet est utilisé avec `--scope user` et sans ignorer la vérification des empreintes. FFmpeg déjà installé n'est pas réinstallé.

L'archive source est construite avec une liste explicite. Elle exclut `.git`, les installations d'outils, les vidéos, journaux, rapports RAM et réglages personnels. Ne pas créer la publication en zippant le dossier de travail complet.

## Construire

Installer MSVC/CMake/SDK comme dans DEVELOPMENT.md, et Inno Setup 6. L'outil Inno Setup est uniquement nécessaire sur la machine de compilation.

```powershell
.\tools\build-release.ps1
# Si la compilation Release est déjà terminée :
.\tools\build-release.ps1 -SkipBuild
```

`-IsccPath` permet de fournir le chemin du compilateur Inno. La version est extraite des métadonnées PE de l'application ; elle doit rester cohérente avec CMake et la version du setup.

## Signature et Windows

Une icône, un nom et une version ne remplacent pas une signature Authenticode. L'application et le setup actuels sont non signés ; un avertissement SmartScreen ou un blocage Smart App Control reste possible. Une nouvelle version signée peut aussi manquer de réputation. Les certificats auto-signés ne résolvent pas ce problème, et EV n'offre plus de passage immédiat sans avertissement. [Microsoft : réputation SmartScreen](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation).

Choisir une identité d'éditeur et un mécanisme de signature reconnu : certificat auprès d'une autorité, service Microsoft Artifact Signing selon éligibilité, ou distribution Store à évaluer. Aucun certificat n'a été acheté ni créé par ce projet. Les détails sont dans [les options de signature Microsoft](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/code-signing-options).

Pour une distribution signée : signer d'abord le binaire Release Plume avec SHA-256 et horodatage, puis fournir à `build-release.ps1` une commande de signature Inno (`-SignToolCommand`, avec son paramètre fichier `$f`). Le script Inno active alors `SignedUninstaller` et signe aussi le désinstalleur et le setup. `-RequireSignature` exige une signature valide de l'application et du setup avant émission des empreintes. Le processus ne doit pas extraire ni afficher de clé privée.

Le SignTool du SDK Windows peut signer et vérifier les fichiers ; les paramètres `/fd SHA256`, `/td SHA256`, `/tr` et le certificat doivent correspondre au fournisseur retenu. [Documentation SignTool](https://learn.microsoft.com/en-us/windows/win32/seccrypto/signtool). Les certificats personnels, jetons matériels et services de signature ont des modalités différentes ; la commande exacte sera configurée après choix de l'identité et du fournisseur.

Ne pas modifier les binaires après signature. Mettre à jour les mentions de signature dans le README et le texte d'installation lorsqu'une distribution signée est préparée. Tester aussi la signature du désinstalleur installé. Publier les SHA-256 définitifs sur la page de téléchargement, en plus de l'archive.

## Avant publication

1. Tester le setup avec un compte Windows standard, puis mise à jour et désinstallation. Vérifier les données conservées et l'autostart facultatif.
2. Tester un PC Windows 11 sans FFmpeg : option WinGet, absence d'Internet/WinGet, puis lancement. L'installation de la dépendance ne doit pas être déclarée réussie si elle échoue.
3. Tester au moins une autre carte NVIDIA. La lecture sur Intel/AMD et Windows 10 reste à valider ; l'import est actuellement NVIDIA uniquement.
4. Vérifier les signatures retenues et les fichiers dans les archives. Scanner les artefacts finaux avec l'antivirus local. Une absence de détection locale ne garantit pas l'absence de blocage sur tous les systèmes.
5. Créer une page de publication stable contenant setup, portable, sources MIT, SHA-256, guide, limites et moyen de signaler un problème. Aucun dépôt public ni site de téléchargement n'a encore été créé.

Les alertes de réputation ne se traitent pas comme une détection de malware. Si Defender signale une menace précise sur un artefact vérifié, conserver le fichier et les détails puis, avec accord de l'éditeur, le soumettre au portail Microsoft pour analyse. Une soumission de faux positif ne garantit pas de construire la réputation SmartScreen. Ne pas demander aux utilisateurs de désactiver leurs protections.

Plume utilise SetParent et l'ordre des fenêtres du bureau, une intégration Explorer non contractuelle qui peut évoluer avec Windows. Il n'injecte aucun code dans Explorer. Cette distinction technique n'est pas une promesse que tout antivirus lui accordera sa confiance.
