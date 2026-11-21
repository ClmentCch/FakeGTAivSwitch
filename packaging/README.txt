GRAND THEFT AUTO VI - homebrew Nintendo Switch
==============================================

Lecteur video plein ecran, avec son, en boucle, sans aucune interface.
Bouton "+" : quitte l'application.


INSTALLATION SUR LA CARTE SD
----------------------------

Cette archive contient deja l'arborescence "switch/gtaVI/". Decompressez-la
directement a la racine de la carte SD, puis placez VOTRE fichier video dans
le dossier gtaVI en le nommant exactement "video.mp4".

Structure attendue sur la carte SD :

    sdmc:/
    +-- switch/
        +-- gtaVI/
            +-- gtaVI.nro
            +-- video.mp4      <-- votre video, a ajouter
            +-- README.txt     <-- cette notice

La video n'est PAS fournie avec cette archive. L'application lit toujours :

    sdmc:/switch/gtaVI/video.mp4

Aucune selection de fichier n'est necessaire : lancez simplement
"Grand Theft Auto VI" depuis le Homebrew Menu.


UTILISATION
-----------

- Au lancement, la premiere image reste affichee environ 2 secondes,
  puis la lecture demarre avec le son.
- A la fin de la video, elle recommence automatiquement depuis le debut
  (premiere image pendant environ 2 secondes, puis lecture avec son).
- Le bouton "+" quitte proprement l'application.
- Aucune connexion Internet n'est utilisee.


FORMAT RECOMMANDE
-----------------

MP4 / H.264, 1280x720 ou 1920x1080, 30 images par seconde, audio AAC stereo.
Le decodage etant logiciel, un fichier trop lourd (1080p 60 i/s) peut perdre
des images, mais le son reste synchronise.


VERSION
-------

Grand Theft Auto VI 2.2.0

En cas de probleme au lancement, un journal est ecrit a cote du NRO :

    sdmc:/switch/gtaVI/gtaVI.log

Il indique chaque etape du demarrage et la raison d'un eventuel plantage.
