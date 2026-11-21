# Grand Theft Auto VI — homebrew Nintendo Switch

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

L'archive `gtaVI-2.0.0.zip` contient l'arborescence `switch/gtaVI/` :
décompressez-la directement à la racine de la carte SD, puis ajoutez votre
`video.mp4` dans `sdmc:/switch/gtaVI/`.

```
gtaVI-2.0.0.zip
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
make dist       # produit dist/gtaVI-2.0.0.zip
```

ou, en une commande :

```bash
./build.sh
```

Détails de la compilation :

* Le NACP est généré par `nacptool` (nom affiché : **Grand Theft Auto VI**,
  version 2.0.0) puis embarqué par `elf2nro`.
* L'icône du Homebrew Menu est `icon.jpg`, conversion 256×256 du logo fourni
  dans le dépôt (`logovideo.png`).
* Le NRO final s'appelle exactement `gtaVI.nro`.

## Structure du dépôt

```
Makefile              compilation devkitPro/libnx
build.sh              script de compilation + archive d'installation
source/main.c         point d'entrée, boucle, phase de 2 s, bouton +
source/media.c/.h     démuxage MP4, décodage FFmpeg, synchronisation
source/audio.c/.h     sortie audio audout (libnx) et horloge de lecture
source/video.c/.h     framebuffer plein écran (libnx) et mise à l'échelle
source/log.c/.h       journal de diagnostic sur la carte SD
source/timebase.c/.h  horloge monotone en millisecondes
icon.jpg              icône 256×256 du Homebrew Menu (issue de logovideo.png)
logovideo.png         logo d'origine fourni dans le dépôt
packaging/README.txt  notice incluse dans l'archive ZIP
```

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
