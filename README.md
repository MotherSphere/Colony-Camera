# Colony Camera

Mod de caméra indépendant pour Skyrim, écrit en Rust avec une passerelle C++
CommonLibSSE-NG. **Version 0.1.2 alpha : correction de l’intégration caméra, validation en jeu requise.** Ce n'est pas une version de SmoothCam ni une copie de son code.

## Fonctionnalités de cette alpha

- Lissage de position indépendant de la fréquence d'images, avec retard maximal.
- Trois profils : exploration, arme dégainée, visée (arc/arbalète dégainé ou magie).
- Décalages supplémentaires horizontal, profondeur et hauteur dans l'espace caméra.
- Inversion de l'épaule native, avec restauration immédiate des réglages Skyrim.
- Collision native exécutée après le lissage, avec conservation de sa correction.
- Réinitialisation après chargement, changement de cellule, téléportation,
  entrée en troisième personne et longue interruption de mise à jour.
- Configuration INI rechargée à la demande, validée en entier avant application.
- Première personne, montures, menus et dialogues exclus du traitement.
- Détection de SmoothCam chargé simultanément : Colony Camera reste inactif.

Le profil de visée ne déplace ni ne lisse la caméra par défaut, pour conserver
l'alignement natif du réticule. Changer ce profil peut modifier la précision de
visée. Le menu MCM, le réticule personnalisé, la trajectoire des projectiles et
l'import de presets SmoothCam ne sont pas implémentés. Aucun support complet de
TDM, Improved Camera, systèmes de dialogue ou autres gestionnaires n'est revendiqué.

## Dépendances et installation de test

Cible unique : **Skyrim Steam 1.7.104.0**, SKSE **2.3.1** et Address Library
pour cette version. La DLL refuse les autres versions. Aucun ESP ni script
Papyrus nécessaire. Compilation Windows x64 ; chargement initial observé sous Proton, rendu du correctif à vérifier.

1. Sauvegarder sa configuration, fermer Skyrim et désactiver SmoothCam ainsi que
   les autres remplaçants complets de la caméra à la troisième personne. Improved Camera
   peut rester activé pour cet essai : son interception des collisions est conservée.
2. Installer le dossier `Colony Camera 0.1.2 alpha` dans un gestionnaire de mods.
   Il doit contenir `SKSE/Plugins/ColonyCamera.dll` et `ColonyCamera.ini`.
3. Lancer Skyrim via SKSE et tester sur une sauvegarde de test.
4. Consulter `Documents/My Games/Skyrim Special Edition/SKSE/ColonyCamera.log`
   dans le préfixe Windows/Proton concerné en cas de refus ou d'absence d'effet.

Raccourcis par défaut (clavier, lorsque les menus sont fermés et la caméra est à
la troisième personne) : **Ctrl+F8** activation, **Ctrl+F9** épaule,
**Ctrl+F10** rechargement INI. Un raccourci utilisé en première personne sera
traité au prochain passage en troisième personne. Touches configurables par code
clavier SKSE ; aucun raccourci manette dans cette alpha. L'état d'activation et
l'épaule changés au clavier ne sont pas écrits dans la sauvegarde ou dans l'INI.

`x/y/z` sont des ajouts à la caméra native, en unités du jeu, limités à ±300.
`half_life` est le temps en secondes pour diviser l'erreur restante par deux
(0 = immédiat). `max_lag` limite la distance à la position demandée (0 = immédiat).
En combat, une demi-vie plus courte donne une réponse plus rapide. Les valeurs
par défaut ne déplacent pas la caméra ; elles ajoutent seulement son lissage.

Pour désinstaller : fermer le jeu et désactiver le dossier du mod. Aucun fichier
vanilla, plugin de gameplay ou donnée de sauvegarde n'est modifié.

## Compilation

Rust/Cargo, Python 3, Git, CMake >=3.24, C++23. Dépendances épinglées dans
`dependencies.json`; leurs sources restent séparées sous `deps/`.

```sh
python3 scripts/fetch-dependencies.py
cargo test --locked
cargo clippy --all-targets -- -D warnings
rustup target add x86_64-pc-windows-msvc
```

Sous Linux : clang-cl, lld-link, llvm-lib, llvm-rc, llvm-mt, Ninja et un sysroot
xwin contenant `crt/` et `sdk/` (par défaut `~/.local/share/xwin`).

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=cmake/linux-xwin.cmake
cmake --build build -j 8
```

Sous Windows : terminal Visual Studio avec outils C++ et Cargo sur le PATH.

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

Pour reconstruire depuis le paquet Sources, extraire `colony-camera.tar.gz`,
puis `dependencies.tar.gz` dans le dossier `Colony-Camera` obtenu. Les dossiers
`deps/` contiennent alors les sources sans Git : sauter l'étape
`fetch-dependencies.py` et utiliser directement CMake.

Le moteur Rust est compilé automatiquement par CMake. CRT statique pour les
deux langages. La voie Linux a été exécutée ; la voie Visual Studio reste à tester.
Les exécutables `camera_abi` et `camera_load` testent l'interface entre langages,
les gardes binaires et le chargement Windows sans démarrer Skyrim.

```sh
# Linux/Wine : utiliser un préfixe de test distinct de celui du jeu.
WINEPREFIX="$PWD/build/wine" wine build/camera_abi.exe
WINEPREFIX="$PWD/build/wine" wine build/camera_load.exe "$(WINEPREFIX="$PWD/build/wine" winepath -w "$PWD/build/ColonyCamera.dll")"
python3 scripts/verify-runtime.py /chemin/SkyrimSE.exe /chemin/versionlib-1-7-104-0.bin
cmake --install build --prefix dist/staging
```

## Provenance et licence

Code original de ce projet sous GPL-3.0-or-later. Aucun code, asset, script,
menu ou preset de SmoothCam inclus. Bibliothèque moteur CommonLibSSE-NG
(alandtse et contributeurs), journalisation spdlog, en-têtes DirectXMath et
DirectXTK de Microsoft : versions et notices dans `dependencies.json` et
`THIRD-PARTY-NOTICES.md`. Les sources de l'alpha accompagnent son paquet de test.

La lecture de déclarations CommonLib et des points d'entrée de Skyrim permet
l'intégration au moteur ; elle n'établit pas une validation en jeu ni une promesse
de publication sur une plateforme. Voir `docs/verification.md` pour les limites.

## Correctif 0.1.1

L’alpha 0.1 bloquait son traitement dès qu’une autre extension interceptait les
fonctions de caméra. Les hooks sont désormais posés après chargement d’une partie
ou démarrage d’une nouvelle partie, en conservant les callbacks déjà présents.
La position est publiée dans l’état de caméra, sa racine et le nœud de rendu ;
la matrice de projection est ensuite recalculée par le moteur.

Le journal distingue l’installation, l’appel effectif du callback, l’application
d’une image et le premier déplacement non nul. Ces traces ne sont pas écrites
à chaque image. Les animations sans contrôle et les killmoves
restent natifs pour limiter les interférences avec Improved Camera.
Ce n’est pas une validation de toutes ses animations ou de tous les mods de caméra.

Le fonctionnement de raccordement a été étudié dans les sources publiques de
[SmoothCam](https://github.com/mwilsnd/SkyrimSE-SmoothCam/tree/66f3960ec4de2b28af5e863c794a3924e6a2dfdd),
notamment le chargement différé et la publication vers NiCamera. Aucun fichier
SmoothCam n’est embarqué ; la passerelle CommonLib est implémentée dans ce projet.

## Correctif 0.1.2

L’essai de 0.1.1 a confirmé l’appel du callback après TDM/SkyParkour, mais le
traitement restait bloqué par un diagnostic ambigu : NiCamera absente ou racine
attachée à un parent. La présence d’un parent n’est plus un motif de refus : la
position locale est obtenue par la transformation inverse CommonLib du parent,
tandis que les positions monde et le décalage propre au nœud de rendu sont préservés.
Une transformation invalide laisse les positions intactes.

Les diagnostics distinguent maintenant racine absente, type des enfants,
transformation invalide et application réussie. La détection d’Improved Camera
reconnaît aussi son vrai nom de fichier `ImprovedCamera.dll`.
Le jeu doit être relancé pour charger cette DLL ; la levée du blocage observé
reste à confirmer avec le nouveau journal.
