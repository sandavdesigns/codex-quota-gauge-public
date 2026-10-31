# Grafik im öffentlichen Paket

Das öffentliche Paket enthält eine eigene, direkt im C++-Code gezeichnete
Laptop-/Terminalanimation. Es benötigt keine externen Bilddateien und ist vollständig
kompilierbar. Ein Limit zeigt diese Animation, zwei Limits feste Ringe.

Die private Entwicklungsfassung konnte das eingebaute OpenAI-Pet Rocky anzeigen.
Dessen Sprite-Datei wird wegen ungeklärter Weiterverteilungsrechte nicht in das
öffentliche ZIP aufgenommen. Wenn du eigene kompatible Sprite-Daten verwendest,
kannst du `firmware/include/rocky_typing.h` ergänzen. Das Format beschreibt die
Verwendung von `PET_WIDTH`, `PET_HEIGHT`, `PET_FRAMES`, `PET_PALETTE` und `PET_PIXELS`
in `firmware/src/main.cpp`. Sichtgrenzen und Animationsplan müssen dazu passen.
Ohne diese Datei wird automatisch die mitgelieferte Laptopgrafik verwendet.
