# Traversal-Pfeiler: Schwung, Sprint, Gleiter, Reittier-Kategorien

Status: Design-Entwurf (2026-11-21), noch nichts implementiert.

## Intent

Alestia soll Dinge haben, an die man sich erinnert und die es nicht wie einen WoW-Klon wirken lassen.
Ein solcher Pfeiler ist **Fortbewegung als Werkzeugkasten**: Reittiere und Gleiter sind keine
Geschwindigkeits-Upgrades, sondern entscheiden, *welche Wege* einem Spieler offenstehen. Die Welt
öffnet sich über Fortbewegung (Metroidvania-artig) statt nur über Level und Zonen.

Die Kernschleife: mit einem Hochsprung-Reittier an die Klippe, abspringen, im Aufwind gleiten,
unten auf einem Wasserläufer weiter, alles aus derselben Ressource.

Bewusst **nicht** Teil dieses Pfeilers:

- **Flugmounts**: nicht geplant; sie würden Gleiter und Vertikalität entwerten.
- **Freies Klettern**: vorerst nicht. Später ggf. gezielte Kletterstellen (Ranken, Seile) als
  platzierte Weltobjekte, die Schwung verbrauchen (siehe Ausblick).

## 1. Ressource „Schwung“

Eine eigene Leiste, die ausschließlich Fortbewegung bedient.

**Name:** Nicht „Ausdauer“: das deutsche Wort ist im Genre bereits das Attribut Stamina
(`attributes.proto`, „You have gained %d stamina.“). Festgelegt: deDE **Schwung**, enUS **Vigor**.

### Verbrauch

| Aktion | Verbrauch |
|---|---|
| Sprint zu Fuß | kontinuierlich |
| Gleiten | kontinuierlich; im Aufwind kaum/gar nicht |
| Hochsprung (Reittier) | einmalig pro Sprung |
| Wasserlaufen (Reittier) | kontinuierlich: begrenzte Strecke, nicht „der ganze See“ |
| Tauchen (Reittier) | kontinuierlich, zusätzlich zum Atem |

### Regeln

- **Eine Leiste** für Spieler und Reittier: Sprung und Gleitstrecke konkurrieren um denselben
  Topf, das erzeugt Entscheidungen.
- **Nie im Kampf.** Schwung ist reine Fortbewegung und taucht nie in der Klassenbalance auf.
- **Regeneration** am Boden und beim normalen Reiten, nicht während einer Fähigkeit.
- **Erschöpfung hat faire Folgen:** Gleiter klappt zu → Fall (Fallschaden); Wasserlaufen endet →
  man sinkt ein und schwimmt; Tauchen → der Atem fällt danach schneller. Kein Sofort-Tod.

### Wachstum über Erkundung

- **Aussichtspunkte:** markante Gipfel/Türme, an denen man sich „einstimmt“; jede Einstimmung erhöht
  den maximalen Schwung dauerhaft. Das sind dieselben Orte, von denen man abspringt.
- **Professionen:** Speisen/Tränke (Kochen, Alchemie) für langsameren Verbrauch oder kurzen Bonus,
  also Bedarf auf dem Markt, auch bei Maximalstufe.
- **Reittier-Bindung (optional):** eine oft gerittene Kategorie verbraucht für *ihre* Fähigkeit
  etwas weniger. Kein Grind-Zwang: kleine Boni, früh gedeckelt.

## 2. Sprint

| | Sprint (alle) | Scout-Sprint |
|---|---|---|
| Zweck | Fortbewegung | Kampfwerkzeug |
| Kampf | nur außerhalb, bricht bei Kampfbeginn ab | funktioniert im Kampf |
| Kosten | Schwung | keine, Abklingzeit |
| Stärke | moderat (~+30–40 %) | deutlich stärker, kurz |
| Extra | nein | ggf. löst Verlangsamung/Wurzeln |

Damit bleibt der Scout-Sprint einzigartig: das **einzige Sprinten im Kampf** (Flucht, Kiten).
Zusätzlich bekommt der Scout eine kleine passive Traversal-Identität (weniger Schwungverbrauch
für Sprint/Gleiten), per Talent oder Klassenrang. Thematisch passend, ohne die Kampfbalance zu berühren.

## 3. Gleiter

- **Freischaltung** über eine Questreihe mit Lore (z. B. ein Orden/Volk, das von den Klippen
  sprang), nicht „ab Level X“.
- **Steuerung:** Sprungtaste in der Luft öffnet/schließt den Gleiter; langsames Sinken, lenkbar.
- **Aufwinde:** platzierte Windfelder, Thermik über Lagerfeuern/Schmiedeessen, und Klassenzauber
  (z. B. Magier-Feuer) als Werkzeug für die ganze Gruppe.
- **Windwege:** sichtbare, auf der Karte eingezeichnete Luftströmungen zwischen Klippen, quasi
  ein Straßennetz am Himmel.
- **Windschatten in der Gruppe:** wer hinter einem Gruppenmitglied gleitet, verliert langsamer
  Höhe. In der Gruppe gleitet man weiter als allein; das gibt es nur in einem MMO.
- **Herstellung und Sammlung:** Gleiterstoff aus Tailoring, Gestell aus einer weiteren Profession;
  Gleiter-Optiken als Sammlung (pro Account).
- **Kampf:** Schaden holt dich aus dem Gleitflug; im Gleiten kein Zaubern.
- **Inhalte, die ihn nutzen:** Höhlen in Steilwänden, Dungeon-Eingänge nur von oben erreichbar,
  Gleit-Zeitrennen als wiederkehrende Aktivität.

## 4. Reittier-Kategorien und Optiken

- Reittiere sind in **Kategorien** gruppiert (z. B. Pferd, Säbler, Wolf, Widder …). Ein Reittier
  pro Kategorie.
- Pro Kategorie wählt man eine **Optik** aus seiner Sammlung (schwarzes Pferd, weißes Pferd …).
  Optiken sind rein kosmetisch und dürfen großzügig verteilt werden (Quests, Erfolge, Ruf, Drops,
  Herstellung). **Optiken pro Account.**
- **Kategorien** werden über eine Questreihe freigeschaltet, die zugleich die Fähigkeit erklärt.
  Ob pro Charakter oder pro Account: offen (pro Charakter, wenn die Freischaltung Teil der
  Geschichte ist).
- Jede Kategorie hat eine **Fähigkeit, die Wege öffnet, nicht stärker macht**:
  - *Hochsprung*: Vorsprünge, Klippen-Abkürzungen, Startrampe für den Gleiter.
  - *Schwimmen*: Flüsse ohne Absteigen, Küstenrouten.
  - *Wasserlaufen*: Abkürzungen über Seen, Sümpfe.
  - *Tauchen*: Höhlen unter Wasser, versunkene Ruinen, Schätze am Seegrund.
- **Gleiche Grundgeschwindigkeit** für alle Kategorien. Höchstens kleine, geländeabhängige
  Unterschiede (Pferd auf Straßen, Säbler im Wald, Widder am Hang), damit keine Kategorie
  die anderen verdrängt.
- **Schnelles Wechseln** (Radialmenü oder eine Taste pro Kategorie), damit „hier wechsle ich auf
  den Wasserläufer“ zum natürlichen Spielfluss gehört.
- **Abwurf bei Schaden** gilt weiter; auf dem See abgeworfen heißt: man landet im Wasser.

## 5. Weltgestaltung

Ohne Höhe zeigt der Pfeiler nichts. Ohne freies Klettern kommt Höhe nur über:

- **Gelände und Wege** zu jedem Aussichtspunkt (Pfade, Treppen, Serpentinen). Jede Klippe, von
  der man abspringen soll, braucht einen erreichbaren Weg nach oben.
- **Hochsprung-Reittiere** für Vorsprünge.
- **Aufwinde**, die beim Gleiten nach oben tragen.

Der bestehende Alestia-Kontinent ist auf WoW-Maßstab angelegt. Mindestens eine Zone muss gezielt
vertikal gebaut werden, zuerst als Testfeld.

## 6. Technik (grobe Einordnung)

- **Bewegung mit Server-Autorität:** `movement_info.h` kennt bereits `Swimming`, `WaterWalking`,
  `SlowFall`, `Falling/FallingFar`. Neu: ein Gleit-Zustand (eigene Sinkrate/Lenkung, kein Fallschaden
  während des Gleitens), Sprint-Zustand, Hochsprung-Höhe pro Reittier. Alle Zustände müssen in die
  Speed-/Fall-Prüfungen des Servers. Jede Wire-Änderung → `game::ProtocolVersion` hochzählen.
- **Schwung** als replizierte Unit-Feld(er) mit Client-Vorhersage für den eigenen Spieler, nach dem
  Muster der Echtzeit-Regeneration (Server autoritativ, Client sagt voraus, Sync bei Abweichung).
- **Daten (Proto):** `MountCategory` (Fähigkeit, Parameter wie Sprunghöhe/Tauchdauer/Verbrauch,
  Rig) und `MountSkin` (Display, Herkunft); Gleiter-Optiken analog. Sammlung pro Account (realm-seitig,
  ähnlich der Pet-Ablage). Reittiere laufen heute über Mount-Auren in Spells; ob Kategorien darauf
  aufsetzen oder ein eigenes System werden, entscheidet die Implementierungsplanung.
- **Aufwinde und Windwege** als platzierte Weltobjekte/Trigger-Volumen; Thermik von Zaubern als
  temporäres Volumen (vgl. PersistentAreaAura-Bodenzonen).
- **Creature-AI:** Verhalten gegenüber Spielern in der Luft (Aggro, Leash/Evade) muss definiert werden.
- **Streaming:** Aus der Höhe sieht man weiter und überquert schneller Pages: Far-Terrain-LOD und
  Prefetch werden wichtiger.
- **Assets sind der Engpass:** jede Kategorie braucht ein eigenes Rig mit Lauf-, Sprung- und
  Schwimm-Animationen; Optiken sind danach nur Texturen/Varianten. Tauchen ist der teuerste Teil
  (3D-Bewegung unter Wasser, Kamera, Animationen).

## 7. Weg in Schritten

1. **Testfeld:** eine kleine vertikale Zone, Schwung-Leiste, Sprint, Gleiter (ein Aufwind-Typ),
   Datenmodell Kategorie + Optik mit zwei Kategorien: Pferd (Basis) und eine Hochsprung-Kategorie.
   Ziel: fühlt sich Springen → Gleiten → Landen gut an?
2. Aussichtspunkte und Schwung-Wachstum; Gleiter-Questreihe; Optik-Sammlung + Auswahl-UI.
3. Wasser-Kategorien: Schwimmen, Wasserlaufen.
4. Windschatten in der Gruppe, Windwege, Thermik durch Zauber, Herstellung über Professionen.
5. Tauchen und Inhalte unter Wasser.
6. Ausblick: gezielte Kletterstellen als Weltobjekte.

## Offene Fragen

- Kategorien pro Charakter oder pro Account?
- Reittier-Bindung ja/nein, und wie stark?
- Welche Zone wird das Testfeld (Umbau oder neue Zone)?
- Verhalten von Creatures gegenüber gleitenden Spielern.
