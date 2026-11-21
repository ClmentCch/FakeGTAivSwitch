# Grand Theft Auto VI

Lecteur vidéo plein écran pour Nintendo Switch, en homebrew (NRO). Il lit
**`sdmc:/switch/gtaVI/video.mp4`** dès le lancement, en boucle, avec le son, et
sans aucune interface. Le bouton **+** quitte l'application.

## Version 1.0.0.1 — base PlayerNX

Le lecteur est désormais construit sur le même principe que
[PlayerNX](https://github.com/XorTroll/PlayerNX) :

* rendu **1280×720** dans la fenêtre par défaut de libnx (le compositeur
  agrandit l'image à l'écran), comme PlayerNX ;
* trois modules seulement : `main.c` (démarrage direct), `player.c` (démuxage,
  décodage, lecture) et `gfx.c` (framebuffer) ;
* lecture **immédiate** du fichier, sans menu ni sélection.

L'API `gfx*` de PlayerNX (2018) n'existe plus dans libnx 4.x : l'équivalent
moderne (`framebufferCreate` + mode linéaire) est utilisé à la place, avec le
même résultat — une image plein écran écrite directement en mémoire.

Deux ajouts par rapport à PlayerNX : le **son** (`audout`, avec son propre
thread) et la **synchronisation** sur l'horloge système, pour que la vidéo
avance au rythme réel et saute des images plutôt que de ralentir.


Application homebrew Nintendo Switch (format NRO) qui lit une vidéo MP4 en
plein écran avec le son, en boucle, sans aucune interface : pas de menu, pas de
texte, pas de bouton, pas de barre de progression.

Au lancement (et à chaque redémarrage de la boucle), la première image de la
vidéo reste affichée deux secondes, puis la lecture démarre avec le son. Le
bouton **+** quitte proprement l'application.

## Structure de la carte SD

```
sdmc:/
└── switch/
    └── gtaVI/
        ├── gtaVI.nro
        └── video.mp4
```

Le chemin de lecture est fixé dans le code : `sdmc:/switch/gtaVI/video.mp4`.
Le fichier vidéo reste externe au NRO — il n'est ni embarqué, ni téléchargé,
ni remplacé par une vidéo de démonstration. Sans ce fichier, l'application
s'arrête immédiatement.

## Installation

1. Copiez `gtaVI.nro` dans `sdmc:/switch/gtaVI/`.
2. Copiez votre propre vidéo dans le même dossier, en la nommant exactement
   `video.mp4`.
3. Lancez **Grand Theft Auto VI** depuis le Homebrew Menu.

L'archive `gtaVI-1.0.0.1.zip` contient l'arborescence `switch/gtaVI/` :
décompressez-la directement à la racine de la carte SD, puis ajoutez votre
`video.mp4` dans `sdmc:/switch/gtaVI/`.

```
gtaVI-1.0.0.1.zip
└── switch/
    └── gtaVI/
        ├── gtaVI.nro
        └── README.txt
```

## Utilisation

| Action | Effet |
| --- | --- |
| Lancement | Affichage de la première image pendant ~2 s, puis lecture |
| Fin de la vidéo | Retour au début, première image pendant ~2 s, puis lecture |
| Bouton **+** | Quitte l'application |

Aucune connexion Internet n'est utilisée : tout est lu depuis la carte SD.

## Formats vidéo

Le décodage est assuré par FFmpeg (libavformat / libavcodec / libswscale /
libswresample) compilé pour la Switch, avec sortie plein écran directe dans le
framebuffer de la console (libnx). Aucune dépendance à SDL ni à OpenGL.

* Conteneur : MP4 (démuxeur `mov`) — MKV et les autres conteneurs reconnus par
  FFmpeg fonctionnent également.
* Vidéo : H.264 (recommandé), HEVC, VP9, MPEG-4/MPEG-2, etc. — tout codec
  présent dans la bibliothèque FFmpeg utilisée à la compilation.
* Audio : AAC (recommandé), MP3, AC-3, etc. — converti en stéréo 16 bits
  dans la fréquence du périphérique audio.
* La vidéo est mise à l'échelle en conservant son ratio ; des bandes noires
  apparaissent si le format n'est pas 16:9.

**Recommandation pour une lecture fluide :** H.264 dans un MP4, 1280×720 ou
1920×1080, 30 images/seconde, AAC stéréo 48 kHz. Le décodage est logiciel :
un fichier 1080p à 60 images/seconde peut dépasser les capacités du processeur
de la console et entraîner des images sautées (le son reste, lui, synchronisé).

## Synchronisation audio / vidéo

Le périphérique audio est l'horloge maître : la position de lecture est
calculée à partir des échantillons réellement consommés par le moteur audio,
et chaque image est présentée lorsqu'elle atteint son horodatage sur cette
horloge. Les images trop en retard sont sautées, la file audio est limitée à
0,5 s, et une horloge de secours prend le relais si le son s'interrompt. La
lecture reste donc synchronisée du début à la fin, y compris à chaque boucle.

## Compilation

Prérequis : [devkitPro](https://devkitpro.org/wiki/Getting_Started) avec
`devkitA64`, `libnx`, `switch-tools` et `switch-ffmpeg`. SDL2 n'est plus
nécessaire.

```bash
export DEVKITPRO=/opt/devkitpro
make            # produit gtaVI.elf, gtaVI.nacp et gtaVI.nro
make dist       # produit dist/gtaVI-1.0.0.1.zip
```

ou, en une commande :

```bash
./build.sh
```

Détails de la compilation :

* Le NACP est généré par `nacptool` (nom affiché : **Grand Theft Auto VI**,
  version 1.0.0.1) puis embarqué par `elf2nro`.
* L'icône du Homebrew Menu est `icon.jpg`, conversion 256×256 du logo fourni
  dans le dépôt (`logovideo.png`).
* Le NRO final s'appelle exactement `gtaVI.nro`.

## Structure du dépôt

```
Makefile              compilation devkitPro/libnx
build.sh              script de compilation + archive d'installation
source/main.c         point d'entrée, boucle, phase de 2 s, bouton +
source/player.c/.h     démuxage MP4, décodage FFmpeg, synchronisation
source/audio.c/.h     sortie audio audout (libnx) et horloge de lecture
source/gfx.c/.h     framebuffer plein écran (libnx) et mise à l'échelle
source/log.c/.h       journal de diagnostic sur la carte SD
source/timebase.c/.h  horloge monotone en millisecondes
icon.jpg              icône 256×256 du Homebrew Menu (issue de logovideo.png)
logovideo.png         logo d'origine fourni dans le dépôt
packaging/README.txt  notice incluse dans l'archive ZIP
```

## Corrections de la version 3.1.1

Le journal de la 3.0.0 a montré une source **H.264 1920×1080 à 60 i/s**, 0 image
sautée, mais une horloge audio qui n'avançait que de 0,3 s toutes les 2 s :
tout le pipeline tournait ~6× trop lentement, donc lecture au ralenti et son
haché (le périphérique n'était alimenté qu'au rythme du décodage).

* **Rendu en 1280×720** au lieu de 1920×1080, comme le fait PlayerNX : le
  compositeur agrandit l'image à l'écran. La conversion de couleurs par
  libswscale (sans noyau NEON pour le YUV→RGB, cf. l'avertissement du journal),
  la conversion block-linear de libnx et le vidage de cache travaillent sur
  2,25× moins de pixels.
* **Sources à plus de 40 i/s** (comme ce fichier 60 i/s) : le décodeur ne
  décode plus que les images de référence (`skip_frame = AVDISCARD_NONREF`),
  soit environ 30 images/s valides, et le filtrage de blocage est désactivé.
  La charge de décodage est à peu près divisée par deux.
* Le journal indique la restriction appliquée :
  `decoder limited: 1920x1080 at 60.0 fps (fast mode, reference frames only)`.

## Corrections de la version 3.0.0

* **Comptabilité des tampons audio corrigée** : le nombre d'échantillons encore
  dans le périphérique est désormais mémorisé par tampon au moment de l'envoi.
  Le service `audout` peut renvoyer la taille du tampon entier et non les
  données réellement envoyées : la file paraissait alors pleine en permanence,
  la boucle patientait (chute de la cadence) et des morceaux d'échantillons
  étaient coupés (son haché/saturé).
* **Sources lourdes (4K)** : en plus du mode rapide et du filtrage de blocage
  désactivé, seules les images de référence sont décodées
  (`skip_frame = AVDISCARD_NONREF`). La charge de décodage est à peu près
  divisée par deux pour une source qui ne peut de toute façon pas être affichée
  à sa cadence d'origine ; le mouvement reste continu.
* **AV1** : `libdav1d` (présent dans la toolchain) est utilisé en priorité, au
  lieu du décodeur AV1 intégré de FFmpeg qui est plusieurs fois plus lent.
* Le journal indique le décodeur réellement utilisé et la cadence obtenue.

## Corrections de la version 2.5.0

* **Son saturé / haché** : le périphérique audio n'était alimenté que depuis la
  boucle principale. Décoder une image lourde prenait plus de temps que les
  ~85 ms de tampon du périphérique, qui tombait en sous-alimentation. L'audio a
  maintenant **son propre thread** (priorité plus haute que la vidéo) qui
  réalimente `audout` toutes les 4 ms, avec 6 tampons au lieu de 4. Le son ne
  dépend plus du temps de décodage.
* **Environ 4 images/s** : le fichier source est très probablement du 4K, que ce
  matériel ne peut pas décoder en temps réel en logiciel. Pour les sources plus
  grandes que l'écran, le décodeur est réglé en mode rapide
  (`AV_CODEC_FLAG2_FAST`, filtrage de blocage désactivé) et 4 threads sont
  utilisés.
* **Décodeur le plus rapide** : pour l'AV1, `libdav1d` est utilisé en priorité
  s'il est disponible.
* Le journal précise la résolution, le codec et **le décodeur réellement
  utilisé**, ainsi que la cadence (`playing: N shown, M dropped`).
* Rappel de format : 1280×720 ou 1920×1080 en H.264 à 30 i/s pour une lecture
  fluide ; un fichier 4K sera lu avec des images sautées.

## Corrections de la version 2.2.0

* **La vidéo restait figée sur la première image pendant que le son continuait.**
  Le son démarrait avant que la première image de la boucle soit décodée : sur
  l'horloge audio, toutes les images suivantes paraissaient alors en retard et
  étaient jetées.
  - La première image de chaque boucle est maintenant décodée et affichée
    **avant** le démarrage de l'horloge audio.
  - Même si le décodeur ne tient pas le temps réel (vidéo 4K par exemple), au
    plus 4 images consécutives peuvent être sautées : la 5e est affichée quoi
    qu'il arrive, donc l'image ne gèle plus jamais.
* Le journal enregistre la cadence réelle : `playing: N shown, M dropped,
  clock Xs` toutes les 2 secondes, et `loop end: N shown, M dropped`.

## Corrections de la version 2.1.0

* **Cause du plantage au lancement** : le programme appelait
  `fsInitialize()` puis `fsdevMountSdmc()` et quittait si le montage échouait.
  Or hbmenu et sphaira **montent déjà `sdmc`** dans le processus qui exécute le
  NRO : le périphérique existe avant `main()`, le second montage échoue, et
  l'application se terminait avant même d'écrire son journal — d'où l'écran
  noir de 0,3 s et le retour immédiat au menu, sans fichier `gtaVI.log`.
  Le montage n'est plus tenté que si `sdmc` est absent, et le système de
  fichiers du lanceur n'est plus fermé ni démonté à la sortie.
* Le journal est donc enfin créé : `switch/gtaVI/gtaVI.log`.

## Corrections de la version 2.0.0

* **Nouveau moteur d'affichage** : SDL2 et OpenGL sont abandonnés au profit du
  framebuffer natif de libnx (`framebufferCreate` en mode linéaire). Le décodeur
  écrit l'image directement dans la mémoire de l'écran, sans couche
  intermédiaire susceptible d'échouer au démarrage.
* **Nouvelle sortie audio** : SDL2 est remplacé par `audout` de libnx, avec un
  tampon circulaire de 3 s et quatre tampons de 1024 échantillons. Si la
  console refuse le périphérique, la vidéo est lue silencieusement.
* **Binaire beaucoup plus léger** : `-Wl,--gc-sections` supprime le code mort
  et SDL n'est plus lié, ce qui fait passer le NRO de 20,7 Mo à 14,2 Mo —
  moins de mémoire à allouer au chargement par hbmenu/sphaira.
* Le journal `switch/gtaVI/gtaVI.log` reste écrit à chaque lancement et couvre
  désormais l'initialisation du framebuffer, de l'audio et du fichier vidéo.

## Corrections de la version 1.3.0

* **Lancement depuis le Homebrew Menu (mode applet / Album)** : l'application
  appelait `SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)` et
  quittait immédiatement si l'un des sous-systèmes échouait. En mode applet le
  périphérique audio est souvent refusé, ce qui provoquait un écran noir de
  quelques dixièmes de seconde puis un retour direct au Homebrew Menu. La
  vidéo est maintenant initialisée seule (obligatoire) et l'audio est tenté à
  part : s'il échoue, la vidéo est lue sans son au lieu de quitter.
* Le système de fichiers est monté **avant** toute initialisation, pour que le
  journal et la vidéo soient accessibles dès le démarrage.
* **Journal de diagnostic** : l'application écrit `switch/gtaVI/gtaVI.log`
  (invisible à l'écran) avec chaque étape du démarrage, les messages de FFmpeg
  et les erreurs, ce qui permet de comprendre un plantage.
* Mise en veille automatique désactivée pendant la lecture : l'écran ne
  s'éteint plus au bout de quelques minutes sans bouton pressé.

## Corrections de la version 1.0.1

* Ouverture du fichier vidéo via libnx (`fopen`) et un `AVIOContext` FFmpeg
  personnalisé : FFmpeg interprétait `sdmc:` comme un nom de protocole et
  refusait d'ouvrir `sdmc:/switch/gtaVI/video.mp4`.
* Silence garanti pendant les deux secondes d'affichage de la première image :
  la sortie audio est vidée et mise en pause avant chaque boucle, plus aucun
  reste de son de la boucle précédente.
* Robustesse : une erreur de décodage au milieu du fichier relance la boucle au
  lieu de fermer l'application ; une piste audio illisible est abandonnée et
  la vidéo continue sur l'horloge système.
* Périphérique audio : la fréquence et la taille de tampon imposées par le
  moteur audio de la console sont acceptées (le rééchantillonnage suit).
* Sans périphérique audio, la vidéo est jouée quand même au lieu de quitter.
* Archive d'installation : elle contient maintenant `switch/gtaVI/`, donc
  directement décompressable à la racine de la carte SD.

## Limites connues

* Le NRO est compilé avec devkitA64, libnx et FFmpeg pour la Switch,
  mais il n'a pas été exécuté sur une console physique lors de cette
  publication : la validation sur matériel réel reste à faire.
* Pas de lecture réseau ni de flux : uniquement un fichier local.
* Décodage logiciel uniquement (pas d'accélération matérielle vidéo), d'où la
  recommandation ci-dessus concernant la résolution et la fréquence d'images.
