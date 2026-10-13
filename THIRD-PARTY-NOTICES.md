# Composants et licences

Le code et l'icône de Plume sont distribués sous licence MIT, fournie dans `LICENSE`.

Plume utilise les composants Windows Win32, D3D11, DXGI et Media Foundation fournis par le système. Aucun binaire de ces composants n'est redistribué avec Plume. Le runtime MSVC est lié statiquement ; la redistribution est réalisée par une compilation MSVC avec les composants redistribuables de l'outil de développement.

FFmpeg est une application tierce indépendante, lancée pendant l'import. Il n'est ni lié à l'exécutable Plume ni inclus dans le setup ou le ZIP portable. L'option d'installation du setup passe par WinGet pour obtenir le paquet `Gyan.FFmpeg` auprès de son distributeur. Les builds Gyan sont GPLv3 et ont leurs propres notices et sources :

- Distribution : https://www.gyan.dev/ffmpeg/builds/
- Projet et informations de licence : https://ffmpeg.org/legal.html
- Paquets WinGet : https://github.com/microsoft/winget-pkgs/tree/master/manifests/g/Gyan/FFmpeg

Si FFmpeg est ajouté manuellement à une distribution de Plume, cette distribution doit fournir les notices, licences et le code source correspondant requis par la licence exacte du binaire. La licence MIT de Plume ne remplace pas ces obligations. Ne pas redistribuer un build marqué `--enable-nonfree`.

L'installateur est généré avec Inno Setup, fourni sous sa propre licence : https://github.com/jrsoftware/issrc/blob/main/license.txt . Le compilateur Inno Setup, clang-format, Python et Pillow sont uniquement des outils de développement ; ils ne sont pas installés comme dépendances de Plume.
