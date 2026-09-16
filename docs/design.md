# Colony Camera — conception approuvée

Nouveau mod indépendant de caméra à la troisième personne, code original Rust et
passerelle C++ CommonLibSSE-NG. Aucun code, script, preset ou asset de SmoothCam.
Nom de travail Colony Camera. Cible initiale Steam Skyrim 1.7.104.0, SKSE 2.3.1,
Address Library correspondant. Pas de promesse de compatibilité VR ou anciennes versions.

Le moteur Rust, sans dépendance, reçoit position native, rotation, temps et profil.
Il calcule décalages locaux et interpolation exponentielle indépendante des FPS,
limite le retard et réinitialise les transitions/chargements/téléportations.
La passerelle chaîne la mise à jour native puis corrige la position candidate avec
la collision native avant de l'appliquer. Elle renvoie la position réellement
acceptée au moteur pour éviter une accumulation derrière un obstacle.
Première personne, montures, animations, menus et dialogues restent natifs.

Profils exploration, combat et visée, changement d'épaule, activation et
rechargement de configuration au clavier. Configuration validée atomiquement.
Pas d'allocation, lecture de fichier ni journalisation par image.
La visée conserve la position native par défaut pour ne pas décaler le réticule.
Réticule personnalisé, trajectoire de projectiles et menu MCM constituent une
étape ultérieure : aucune parité complète SmoothCam revendiquée pour cette alpha.

Tests du moteur sur Linux, compilation DLL Windows, inspection des exports et
vérification des points d'accroche dans l'exécutable réel. Un test en jeu reste
obligatoire avant une diffusion stable. Aucun remplacement automatique du mod actif.
