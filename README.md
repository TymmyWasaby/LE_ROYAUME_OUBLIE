# Tymbrawl NumWorks — adaptation EADK

Ce dépôt contient une adaptation de `Tymbrawl` pour être compilée comme application externe NumWorks `.nwa`.

## Compilation

La compilation utilise le compilateur ARM de NumWorks et `nwlink` :

```sh
make clean build
```

Le résultat est `target/mini-bs.nwa`. GitHub Actions exécute automatiquement cette compilation à chaque push, pull request ou lancement manuel, puis publie le fichier en tant qu'artefact de workflow.

## Notes techniques

- Le fichier source d'origine utilisait `ion.h`, `ion_main()` et des appels à l'API interne d'Epsilon.
- `src/ion.h` fournit une couche de compatibilité basée sur l'EADK public.
- `src/game.cpp` conserve le code de jeu d'origine et ajoute un point d'entrée `main()` pour l'application externe.
- La compilation à distance doit encore confirmer la compatibilité de toutes les fonctions et la mémoire disponible sur la calculatrice. Un build réussi ne remplace pas un test sur émulateur ou appareil réel.
