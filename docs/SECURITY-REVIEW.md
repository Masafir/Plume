# Revue ciblée de sécurité — 13 octobre 2026

Périmètre : lancement FFmpeg, arguments et protocoles, écritures/suppressions, parseur MP4, installation et désinstallation. Revue ciblée, sans audit exhaustif ni fuzzing.

- Aucun service, pilote, injection de code, télémétrie ou client réseau dans les sources de Plume examinées.
- FFmpeg lancé directement par CreateProcessW, chemin explicite et arguments échappés, sans shell. Protocoles limités à file,pipe. Import GPU sans fallback CPU.
- Renforcement : liste explicite des handles hérités (entrée NUL et journal uniquement), dossier de travail fixé au dossier de FFmpeg, contrôle de ResumeThread. Processus contenu dans un job arrêté à la fermeture.
- Parseur MP4 : lectures vérifiées contre la taille du fichier, limites de tables/paquets, test BoundedMp4Safety réussi.
- Installation par utilisateur sans élévation ; démarrage automatique optionnel dans HKCU. FFmpeg optionnel via WinGet/Gyan, aucune désactivation de l'antivirus. Désinstallation conserve les vidéos originales et les données utilisateur.
- Compilation Release réussie. Test réel de huit secondes : import lancé et présentation GPU sur les deux écrans, HRESULT final 0 pour les deux lecteurs. Ce test valide le lancement et l'arrêt, pas une conversion complète.
- Scan Defender du paquet reconstruit : aucune menace, code 0.

Limites : exécutables non signés ; confiance requise dans FFmpeg installé et les pilotes Windows/NVIDIA. La revue et le scan ne garantissent pas l'absence de toute vulnérabilité. Pas de validation sur une machine Windows vierge.
