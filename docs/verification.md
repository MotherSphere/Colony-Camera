# Vérification de Colony Camera 0.1 alpha

Date : 17 septembre 2026. Pas de test en jeu effectué. Installation Eidos intacte.

## Contrôles exécutés

- Six tests Rust : convergence et indépendance 30/60/144 FPS, retard maximal,
  téléportation, longues pauses, rotation des décalages, entrées invalides,
  configuration atomique, doublons, valeurs hors bornes, modificateur Ctrl réservé.
- `cargo clippy --all-targets -- -D warnings` et `cargo fmt --check`.
- Compilation complète de CommonLib et du plugin en Windows x64 avec clang-cl,
  SDK/CRT xwin et moteur Rust MSVC. Aucune dépendance Rust tierce.
- Exécutable C++ `camera_abi.exe` lancé sous Wine : structures échangées par valeur,
  configuration valide/invalide, conservation de la sortie en cas d'erreur,
  refus des pointeurs nuls et test de mutation de chaque octet des quatre gardes.
- `camera_load.exe` sous Wine : chargement réel de la DLL et de ses dépendances,
  présence des exports SKSEPlugin_Load / SKSEPlugin_Version, refus propre d'une
  interface SKSE nulle. Cela ne simule pas le chargement SKSE dans Skyrim.
- Vérification en lecture seule de SkyrimSE.exe et de l'Address Library : slots
  Begin/End/Update 1/2/3 de la vtable 205236 et entrée collision 50832. Préfixes
  binaires concordants. Les mêmes gardes sont appliqués avant installation des hooks.

Empreintes des fichiers du jeu lus pour cette vérification :

- SkyrimSE.exe : `846efccf0c1374d71f892907f46549560f2fcb0a75cb87a3eed438baa0f1402f`
- Address Library : `8aab3dd251d135b849bd983f86a4a205c920fa3e81f8e30c0e63ccfef9423842`

La compilation émet des avertissements dans les en-têtes de CommonLib.
Wine émet des avertissements Mesa dans ce préfixe de test sans rendu graphique ;
les deux exécutables de contrôle terminent avec le code 0.

## Relecture

Une relecture indépendante a identifié l'absence de restauration lors d'End,
la vérification trop précoce des conflits et la possibilité d'affecter Ctrl seul
à une action. Ces points ont été corrigés : restauration sur End et Begin,
revérification PostPostLoad/DataLoaded et refus des codes 29/157.
La restauration précède la réinitialisation et n'a lieu que si la position et
l'objet correspondent encore exactement à la dernière sortie appliquée par
Colony Camera ; une position déjà changée par le moteur n'est pas écrasée.
Le profil de visée natif ne lance pas de seconde collision. Une entrée invalide
invalide le résultat Rust et laisse la position native en place.

## Limites et essai requis

La sémantique complète de CheckCameraCollision après une position filtrée n'est
pas prouvée par son ABI ou ses préfixes. La dernière position acceptée est renvoyée
au filtre, mais les obstacles mobiles, angles et escaliers demandent un essai réel.
L'ordre du moteur, le sens des axes locaux et l'inversion de l'épaule doivent
également être confirmés visuellement. Pas de mesure des performances en jeu.

Les conflits détectés couvrent SmoothCam et les entrées natives/vtables surveillées
aux étapes de chargement. Un plugin qui modifie la caméra ailleurs ou plus tard
peut encore être incompatible. Aucune promesse de compatibilité globale.

Scénarios à vérifier avant publication stable : chargement d'une sauvegarde,
marche/course/rotation, murs et passages étroits, première/troisième personne,
dialogue, inventaire et console, changement de cellule, voyage rapide, mort,
monture, arc/arbalète et magie, changement d'épaule et bascule/rechargement INI.
Observer notamment le réticule en visée et comparer les FPS avec le mod désactivé.
Conserver la DLL et son PDB correspondant lors de la collecte d'un crash log.

## Fonctionnalités restantes

Menu MCM, réticule et trajectoire propres, compatibilités inter-mods testées,
prise en charge de plusieurs runtimes et presets avancés. Cette alpha n'est pas
un remplacement complet de toutes les fonctions de SmoothCam.

## Paquet livré

Archive `Colony Camera 0.1 alpha.zip` produite depuis le commit `76389e0`,
contenant la DLL, l'INI, les notices et les sources du projet et des dépendances.
ZIP et chaque fichier du manifeste SHA256 vérifiés après copie sur le Bureau.
SHA256 ZIP : `b25f40179bbeec7a70b79ff998c3d065f2675f5452dadfc1260dcff5870a3940`.
Le PDB correspondant est conservé à côté du ZIP, hors installation du mod.
Aucune installation Eidos, publication distante ou upload Nexus réalisé.
