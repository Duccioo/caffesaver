# Contributing to caffesaver

First off, thank you for considering contributing to caffesaver! It’s people like you that make this project so much fun. We're excited to see what you'll bring to our collection of ASCII art animations.

## Why Contribute?

Contributing to caffesaver is a great way to show off your creativity and practice your shell scripting and C coding skills. This project is all about having fun and making the command line a more exciting place. Whether you're a seasoned developer or just starting out, we welcome your ideas and contributions.

Here are a few reasons why you might want to contribute:

*   **Have fun!** This project is all about creativity and making cool things.
*   **Learn and practice.** Sharpen your `bash` and `C` skills and learn how to create animations in the terminal.
*   **Be part of a community.** Join a group of like-minded people who love to tinker with the command line.
*   **Make your mark.** Create a screensaver that will be used by people all over the world.

We welcome contributions of all kinds, from new screensavers to bug fixes, native C engines, and documentation improvements. If you have an idea, we'd love to hear it!

### Create Your Own Screensaver

Got an idea for a cool ASCII animation? Want to contribute to the collection? It's easy!

#### The Easy Way (Generator)

You can use the built-in generator to create a new screensaver with all the boilerplate code you need. Just run:

```bash
caffesaver --new my-awesome-screensaver
# or
./screensaver.sh --new my-awesome-screensaver
```
(You can also use `-n` instead of `--new`)

This will create a new directory `gallery/my-awesome-screensaver` with a starter `my-awesome-screensaver.sh` script and a `config.sh` file. All you have to do is edit the `.sh` file to add your animation logic!

#### The Hard Way (Manual)

1.  **Create a new directory** for your screensaver inside the `gallery` directory. For example, `gallery/my-awesome-screensaver`.
2.  **Create a shell script** inside your new directory with the same name as the directory, ending in `.sh`. For example, `gallery/my-awesome-screensaver/my-awesome-screensaver.sh`.
3.  **Write your masterpiece!** Your script should:
    - Be executable (`chmod +x your-script.sh`).
    - Handle cleanup gracefully. Use `trap` to catch `SIGINT` (`Ctrl+C`) and restore the terminal to its normal state.
    - Be awesome.

That's it! The main `screensaver.sh` script will automatically detect your new creation.

### Project Overview

* `./caffesaver` (o `./screensaver.sh`) è lo script principale del menu
    * mostra l'elenco degli screensaver disponibili
    * e richiede all'utente di selezionarne uno da eseguire.
* `./gallery` è la directory della galleria, in cui sono memorizzati tutti gli screensaver.
    * Ciascun screensaver ha la propria directory in `./gallery`
        * Il nome della directory è il nome dello screensaver.
        * Esempio: uno screensaver chiamato 'foo' si trova in `./gallery/foo`
    * Ciascun screensaver ha uno script di esecuzione nel formato 'name.sh' (e opzionalmente 'name.c' per il motore nativo C)
        * Esempio: `./gallery/foo/foo.sh`
    * Ciascun screensaver ha un file di configurazione con nome, tagline, ecc.
        * Esempio: `./gallery/foo/config.sh`
* `./jury` contiene la suite di test BATS per questo progetto
* `./Formula` contiene la Formula Homebrew per l'installazione su macOS

### Project Structure

```
.
├─ caffesaver        # Entrypoint CLI rapido
├─ screensaver.sh    # Script principale del menu e dispatcher
├─ install.sh        # Installer per /usr/local/bin/caffesaver
├─ Formula/          # Formula Homebrew per macOS (brew install caffesaver)
├─ LICENSE           # Licenza MIT
├─ README.md         # Documentazione principale con anteprime
├─ CONTRIBUTING.md   # Questa guida
├─ gallery/          # Galleria di tutti i 18 screensaver
│   ├─ <name>/
│   │   ├─ <name>.sh # Script Bash con logica JIT per C e fallback
│   │   ├─ <name>.c  # Sorgente C ad alte prestazioni (opzionale)
│   │   └─ config.sh # Metadati dello screensaver
├─ library/          # Librerie condivise (grafica, TTS)
├─ jury/             # Suite di test automatizzati BATS
└─ spotlight/        # Toolkit per recording e banner/anteprime
```
 
 
 
 
 
 
 
 
 
 
 
