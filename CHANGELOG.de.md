# Änderungen

## 1.5 (2026-11-20)

Ab jetzt meldet Sailcast neue Versionen selbst – so kommen Fehlerbehebungen schnell bei dir an, auch wenn OpenRepos gerade nicht erreichbar ist. Dazu einige Wünsche aus dem Forum und Fehlerbehebungen am Player. Danke für all die Rückmeldungen!

### Neu

- **Sailcast meldet neue Versionen:** einmal am Tag fragt es bei GitHub nach (Schalter „Nach Updates suchen“ auf der Seite „Über Sailcast“). Eine Zeile auf der Seite Podcasts und in den Einstellungen sagt, wenn es eine gibt; herunterladen und installieren auf der Seite „Über Sailcast“. Nicht in der Version aus dem Jolla Store – die aktualisiert der Store
- **Versionsverlauf** auf der Seite „Über Sailcast“: was sich in jeder Version geändert hat, deine Version zuerst
- **Problem melden:** Wenn etwas nicht klappt, zeigt „Protokoll anzeigen“ auf der Seite „Über Sailcast“, was der Player und die Aktualisierungen getan haben – ein kurzes technisches Protokoll, das am Telefon bleibt und von der App nie gesendet wird. Dort lässt es sich auch ausschalten (es wird dann gelöscht); du kannst es lesen und bearbeiten, bevor du es teilst, speicherst oder kopierst, mit den Wegen zum Senden (Forum-Thread, GitHub-Issue, sailcast@proton.me). Passwörter und Tokens in Webadressen sowie E-Mail-Adressen werden unkenntlich gemacht; „Namen ersetzen“ macht aus den Namen deiner Podcasts und Folgen, ihren Ordnern und Webadressen Nummern („Podcast #12“) – ein geteiltes Protokoll zeigt dann, was passiert ist, aber nicht, was du hörst
- **Meine Podcasts durchsuchen, lockerer:** die Wörter dürfen in beliebiger Reihenfolge stehen und aus Podcastname und Folgentitel kommen („ct4004 iphone“), Leerzeichen und Bindestriche spielen keine Rolle („Sud Stream“ findet „Sudstream“), Umlaut-Schreibweisen auch nicht („Fruehdenker“ findet „Frühdenker“). Die Treffer haben die üblichen Menüs (Folge: Download, Playlist, gehört …; Podcast; Lesezeichen)
- **Kapitel als Lesezeichen:** ein Kapitel auf der Episodenseite (oder in der aufgeklappten Player-Leiste) gedrückt halten – ein Lesezeichen an seinem Anfang, benannt nach dem Kapitel
- **Feeds, die nicht aktualisiert werden konnten, werden genannt:** in der Meldung nach dem Aktualisieren, in einer Zeile auf der Seite Podcasts (Tippen: eine Liste mit den Gründen und was du tun kannst – einen beendeten Podcast als beendet markieren oder einen unzuverlässigen aus „Aktualisieren“ nehmen, auch per langem Druck) und mit einem „!“ am Ordner
- **Meine Podcasts teilen:** einzelne Podcasts für die Webseite auswählen, nicht nur Ordner
- „In Neue Folgen zeigen“ lässt sich schon beim Abonnieren ausschalten

### Geändert

- **„Empfehlungen aus meinen Abos“ ist jetzt aus, bis du es einschaltest** (Meine Interessen) – dabei werden die Adressen deiner 10 meistgehörten Podcasts an fyyd geschickt; solange es aus ist, weist eine Zeile oben in „Für dich“ auf den Schalter hin
- „Ungehört“ in der Podcast-Liste und auf der Podcastseite sagt auch, wie viele davon du angefangen hast: „2 ungehört (1 angefangen)“
- Die Playlist hält nicht mehr still an einer Folge an, die gerade nicht abspielbar ist (ein Video ohne WLAN bei gesperrtem Bildschirm): sie bleibt in der Playlist und die nächste spielt. Startet die nächste Folge nicht, versucht Sailcast es noch einmal und sagt es dir dann
- **Abo beenden fragt vorher, wenn der Podcast Downloads hat:** es sagt, wie viele geladene Folgen (und wie viel Platz) gelöscht würden, und bietet an, sie vorher in Musik zu speichern (Music/Podcasts/<Podcast>), wo sie bleiben und sich später wieder importieren lassen. Einen Ordner samt seinen Podcasts zu löschen nennt ebenfalls die Summe
- Das ✕ der Hinweiszeilen ist ein schlankes Kreuz
- Einheitlichere deutsche Texte: „Abo beenden“, „Pulldown-Menü“, „Ringseite“, „gehört“ statt „gespielt“, „Std.“, richtige Einzahl („1 privater Feed“); Hinweise nennen die Knöpfe so, wie sie heißen

### Behoben

- **Eine Folge konnte mittendrin als gehört gelten und beim nächsten Mal von vorne beginnen** (zum Beispiel nach dem Schließen von Sailcast) – wenn die Länge laut Podcast viel kürzer war als die echte Datei oder der Player die Länge nicht kannte
- **Bei gesperrtem Bildschirm startete die nächste Folge der Playlist oft erst, wenn du das Telefon aufgeweckt hast** (oder am Headset getippt) – das Telefon schlief in der kurzen Lücke zwischen zwei Folgen ein
- Eine Folge, die an ihrer Stelle fortgesetzt wird (nach einem Neustart oder über „Weiterhören“), spielt nicht mehr kurz ihren Anfang, bevor sie dorthin springt
- **Der Wechsel von einer Folge zu einer anderen konnte die neue von vorne starten** statt dort, wo du aufgehört hast (der bekannte Fehler aus 1.4.1)
- **Ohne WLAN und mit „Fragen“ erschien die Frage vor einem Video oder Download nicht** – stattdessen eine leere Fehlerseite („Seite konnte nicht geladen werden“), bei jedem Tippen auf Play eine weitere (seit 1.4)
- Beim Beenden eines Abos blieb der leere Download-Ordner des Podcasts zurück
- Archiv: hatte das Archiv schon eine Kopie einer heruntergeladenen Folge, blieb die eigene Kopie der App unbemerkt liegen
- Folgenzeilen: Download-Kennzeichen auf Neue Folgen mit Download-Filter, die Größe der Download-Ringe, lange Statuszeilen

## 1.4.1 (2026-11-17)

Ein kleines Release nach 1.4 – danke für die Rückmeldungen im Forum. Ein paar Dinge, die den Alltag leichter machen, und Fehlerbehebungen aus zwei Code-Reviews.

### Neu

- **Download-Filter auf Neue Folgen:** Pulldown-Menü „Nach Download filtern …“ – nur heruntergeladene oder nur nicht heruntergeladene Folgen. Solange er an ist, sagt eine Zeile unter den Ansichts-Chips das mit der Anzahl („Nur nicht geladene (4 von 12)“); Tippen schaltet um, ✕ zeigt wieder alles. „Neu leeren“ leert dann nur die angezeigten Folgen
- **Heruntergeladen auf einen Blick, wieder:** das Download-Symbol steht wieder neben der Größe auf Neue Folgen (und überall sonst außer auf der Seite Downloads)
- **Playlist und Download getrennt:** das Menü einer Folge hat „Zur Playlist ohne Laden“ (oder „… und laden“, wenn „Folgen der Playlist laden“ aus ist); ➕ gedrückt halten sagt, ob geladen wird
- **Einen Podcast in der Statistik ausblenden** statt seinen Hörverlauf zu löschen: aus der Rangliste, in den Podcast-Einstellungen („In der Statistik zeigen“) oder in der Liste „n Podcasts ausgeblendet“ am Ende der Statistikseite. Die Zeit bleibt erhalten und zählt wieder, sobald du den Podcast zeigst
- **Eine gehörte Folge, die du wieder in die Playlist legst,** wird ungehört, damit die nächste Aktualisierung sie nicht wieder herausnimmt (nur gehörte – eine angefangene behält ihre Position)
- **In Neue Folgen ausgeblendete Podcasts auf einen Blick:** eine Zeile „3 Podcasts ausgeblendet · 5 neue Folgen darin“ auf Neue Folgen („Alle Podcasts gezeigt“, wenn keiner ausgeblendet ist) – Tippen wählt die dort gezeigten Podcasts, alle auf einer Seite mit Alle / Keine, statt in den Einstellungen jedes Podcasts. Die Update-Meldung nennt die neuen Folgen in ausgeblendeten Podcasts („7 neue Folgen – 5 davon in Podcasts, die in Neu nicht gezeigt werden“), so ist eine leere Seite nach „7 neue Folgen“ kein Rätsel mehr
- **Ordnerauswahl** als eigene Seite (mit vielen Ordnern nicht mehr teils unter der Player-Leiste)

### Behoben

- Play startete die nächste Folge statt der, die du gerade wieder in die Playlist gelegt (oder als ungehört markiert) hattest, nachdem sie als gehört gegolten hatte
- Smart Shuffle: das Lösen eines Pins mischte die ganze Playlist; jetzt findet nur diese Folge ihren Platz, die laufende Folge und „Als Nächstes abspielen“-Folgen bleiben, wo sie sind
- „Zur Playlist und laden“, das auf WLAN wartete, wurde nie geladen, wenn „Folgen der Playlist laden“ aus war
- Eine Folge aus der Playlist zu entfernen konnte einen Download stoppen, den du selbst gestartet hattest
- Die geteilte Webseite (Meine Podcasts teilen, Statistik „Alles“) konnte Podcasts nennen, die nicht auf der Seite waren – ohne Abo gehört, Abo beendet, Feeds mit Anmeldung, andere Ordner
- Feeds und Downloads mit Anmeldung schicken sie nie nach einer Umleitung von https auf einfaches http, und so ein Feed behält seine https-Adresse
- Einen Video-Podcast archivieren: der Ordner Videos wird angelegt, und der Archiv-Import findet die Videos
- Die Player-Leiste verdeckt keine Auswahl- und Einstellungsseiten mehr (Sortierung, Filter, Ordnerauswahl, Bestätigungen)
- Kleinigkeiten: Tagesnamen um die Zeitumstellung, Rundung der Statistik-Kacheln, Größen mit Filter auf der Seite Downloads, ein „%“ in einem Podcasttitel auf der geteilten Webseite, „Alle aufklappen“

### Bekannter Fehler

- Der Wechsel von einer Folge zu einer anderen kann selten die neue von vorne starten statt dort, wo du aufgehört hast (auch in früheren Versionen). Auf der Episodenseite oder in der Player-Leiste von Hand zurückspringen; behoben in 1.5

## 1.4 (2026-11-15)

Der Weg, der mit 1.3 begonnen hat, geht weiter: Alltägliches soll weniger Tippen und weniger Nachdenken brauchen. Diesmal wurden die zwei Seiten neu gebaut, die du jeden Tag benutzt – Neue Folgen und Downloads –, zusammen mit Playlist, Player und ein paar Dingen nur zum Spaß.

### Neue Folgen: sehen, was neu ist, und aufräumen

- Zwei Ansichten auf der Seite: **Neu (12)** – was mit den Aktualisierungen kam und du noch nicht angesehen hast – und **Alle ungehörten**
- Eine Folge aus „Neu“ nehmen, ohne sie als gehört zu markieren: **Weglegen** (erster Eintrag im Menü); die Zeile bleibt gedimmt mit **Rückgängig**, bis du die Ansicht verlässt
- **Neu leeren** im Pulldown-Menü, mit Countdown
- ➕ (Playlist-Knopf) lädt die Folge auch herunter (Einstellung „Folgen der Playlist laden“, standardmäßig an; WLAN-Regeln gelten, Videos nur mit WLAN; ohne WLAN wird die Folge gemerkt und nach der nächsten Aktualisierung geladen)
- Umschalter **Nach Datum / Nach Podcast** auf der Seite; nach Podcast gruppiert mit dem Cover im Gruppenkopf, drei Folgen pro Podcast, „n weitere ungehörte“ öffnet den Rest, Gruppen klappen auf und zu (gemerkt), „Alle zuklappen / Alle aufklappen“
- Pro Podcast: **In Neue Folgen zeigen** (Podcast-Einstellungen, aus für tägliche Nachrichten-Feeds) und **n Downloads löschen (Größe)** für alle Downloads des Podcasts auf einmal
- **Ältere als gehört markieren** im Menü einer Folge (Podcastseite und Neue Folgen)
- Die Zahl der neuen Folgen im Chip ist die echte (kein Halt bei 100)
- Eine Größe rechts in einer Folgenzeile heißt „am Telefon“; keine Größe heißt, sie wird gestreamt
- Folgenstatus auf einen Blick: gehörte Zeilen deutlich gedimmt, angefangene zeigen die Restzeit in der Akzentfarbe und einen Ring um ▶

### Downloads

- **Downloads als Ringseite** (Einstellungen → Seiten und Navigation, standardmäßig aus): Neue Folgen → Downloads → Podcasts → Playlist
- **Mehrere löschen …**: Folgen auswählen und in einem Zug löschen – Zeilen antippen, eine zweite Zeile gedrückt halten markiert alles dazwischen, „Alle auswählen“, „Alle nicht in der Playlist auswählen“, „Auswahl umkehren“; die Leiste zeigt Anzahl und Größe; „Alle dieses Podcasts löschen …“ aus dem Menü einer Folge
- Die Kachel **Gehörte löschen** (mit dem kleinen roten Papierkorb) antippen löscht die gehörten Downloads, mit einem Countdown, der Anzahl und Größe nennt
- Sortieren nach: zuletzt geladen (Standard), neueste, älteste, nach Podcast, **größte zuerst**; die echte Dateigröße in jeder Zeile; Filterfeld
- „Ungehörte laden …“ für mehrere Podcasts auf einmal
- Archivierte Downloads (beendete Podcasts, „In Musik speichern“) in einem eigenen Abschnitt, unberührt von „Gehörte Downloads löschen“; die erste Kachel zählt alles, „gesamt inkl. Archiv“
- Downloads gehen nach einem Neustart weiter; kein Download startet, wenn weniger als 500 MB frei sind

### Playlist

- **Sortieren …**: älteste, neueste, kürzeste zuerst, zuletzt geladen, nach Podcast und **Smart Shuffle** (nach der Idee von AntennaPod: Folgen eines Podcasts werden verteilt, damit ein fleißiger Podcast nicht alles übernimmt)
- **Automatisch sortieren**: neue Folgen finden ihren Platz von selbst
- **Fixierungen**: eine Folge, die du von Hand verschoben hast, bleibt, wo du sie hingelegt hast (Pin-Symbol am Griff; „Fixierung lösen“)
- **Automatisch hinzufügen pro Podcast**: neue Folgen kommen ans Ende oder an den Anfang der Playlist, höchstens 1, 3, 5 oder alle pro Aktualisierung; am Griff gekennzeichnet
- Einstellungen → Wiedergabe und Playlist: ➕ fügt ans Ende oder als nächste hinzu
- Nach einer einzelnen Folge: wahlweise die nächste Folge desselben Podcasts
- **Zuletzt beendet** (war „Zuletzt gehört“): bis zum Ende gehörte oder übersprungene Folgen

### Player

- Setzt nach einer langen Pause an der richtigen Stelle fort (auch wenn das System den Stream fallen gelassen hatte)
- Viele Fehlerbehebungen aus einem Review der Player-Logik: schnelles Springen, Geschwindigkeit ohne Abweichung, Position alle 15 s und beim Wechsel in den Hintergrund gespeichert, Sleep-Timer, Headset-„Weiter“ ohne Kapitel
- Arbeitet besser mit anderen Playern am Telefon zusammen (MPRIS, jetzt über D-Bus)
- Die Episodenseite der laufenden Folge zählt die Restzeit live
- Aus einem zweiten Code-Review: eine Folge, deren Ende übersprungen wird („Das Ende überspringen“), gilt jetzt als gehört, „Als gehört markieren“ der geladenen Folge, die Position nach einem Sync mit einem anderen Gerät, der Sleep-Timer „Ende der Folge“, Anrufe und abgesteckte Kopfhörer, ein Stream, der abbricht (setzt an derselben Stelle fort, nicht bei 0)

### Hörstatistik (neu)

- Eine Zeile auf der Seite Podcasts („7 Tage: 3 h 20 min gehört · 5 Tage in Folge ›“), oder die Kachel „gehört“ eines Podcasts antippen; ohne die Zeile (Einstellungen → Seiten und Navigation) im Pulldown-Menü
- 7 Tage / 30 Tage / Alle: gehörte Zeit, durch schnelleres Abspielen gesparte Zeit, gehörte Folgen; Verlauf pro Tag, Woche oder Monat; Aktivität des letzten Jahres; Tage in Folge; Wochentage; Rekorde (längster Tag, meistgehörter Podcast); Ordner, Kategorien, Rangliste der Podcasts
- Zählt nur echtes Hören (die Position muss sich bewegen); von Hand als gehört markierte Folgen werden getrennt gezählt

### Meine Podcasts teilen (eine Webseite) – ein Spielplatz

Ein persönlicher Spielplatz, um auszuprobieren, was eine Podcast-App mit einer Webseite anfangen könnte. Vielleicht braucht es niemand – aber es hat Spaß gemacht, es zu bauen :)

- Eine in sich geschlossene Seite: deine Podcasts nach Ordner mit Covern, Beschreibungen, neuesten Folgen mit Kapiteln und Lesezeichen, wahlweise Hörstatistik, eine OPML-Datei zum Import in jede Podcast-App, Suchlinks (fyyd, Podcast Index, Spotify, Apple)
- Einstellungen → Import und Export → „Meine Podcasts teilen“; Ordner wählen, in Downloads speichern oder teilen; die letzte Seite wird wieder geteilt, wenn sich nichts geändert hat
- Private Feeds (mit Passwort oder persönlichem Link) werden weggelassen

### Leichter zu bedienen

- **Einstellungen neu gegliedert** in sieben Seiten nach Zweck – Seiten und Navigation, Synchronisierung, Wiedergabe und Playlist, Downloads und Speicher, Import und Export, Sichern und wiederherstellen, Über Sailcast: jede Einstellung an einem Ort, Aktionen als Knöpfe, ein kurzer Satz pro Einstellung; Import und Export in drei Teilen (Import, Export, Aufräumen und ersetzen)
- Kürzere Pulldown-Menüs mit derselben Reihenfolge auf jeder Seite (Einstellungen oben, Suche und Aktualisieren unten); auf der Seite Podcasts steht „Neuer Ordner“ im Menü eines Ordnerkopfs, sobald es einen Ordner gibt, Lesezeichen stehen in den Menüs von Playlist und Neue Folgen
- Raster oder Liste auf der Seite Podcasts mit einem Tippen (Symbol neben „Ordner ausblenden“); ▲▼ mit kurzen Namen für die Sortierung auf Podcasts, Neue Folgen und Downloads
- Folgenmenüs: Playlist, dann gehört/ungehört, dann die Datei
- Einstellungen → Seiten und Navigation: Startseite, die zusätzlichen Ringseiten und die Hörzeile
- Besseres Layout mit der größten Schriftgröße (Podcastseite, Downloads)
- Anzahl ungehörter Folgen im Punkt der Podcast-Liste

### Behoben

- Podcasts, die nur ohne Abo gehört wurden, behalten eine geladene Folge, die du noch nicht fertig gehört hast (sie wurde nach 30 Tagen mit dem Podcast entfernt)
- Datensicherheit: später geänderte Archivordner sind weiter bekannt; die Folge im Player wird nie gelöscht oder verschoben; ein beendetes Abo auf einem anderen Gerät räumt auf wie ein lokales
- Der Sync mit gpodder.net/Nextcloud markiert Folgen nicht mehr als neu
- Gleicher Dateiname für zwei Folgen mit gleichem Titel und Datum überschreibt keine mehr; der Import einer Datei, die keine Sailcast-Datenbank ist, wird abgelehnt (und vor jedem Import wird gesichert); ein Podcast, dessen Folge im Player ist, wird nicht unter ihr beendet (eine Meldung sagt warum); ein beendetes Abo auf einem anderen Gerät lässt als „beendet“ markierte Podcasts in Ruhe
- Die Kachel „geladen“ der Playlist öffnet wieder Downloads, wenn Downloads eine Ringseite ist; die Webseite deiner Podcasts rundet Zeiten wie die App
- Einen neuen Ordner anzulegen, während du einen Ordner wählst (Abonnieren, Podcast-Einstellungen, Import), zeigt nicht mehr die alte Ordnerliste

### Gut zu wissen

- Die Datenbank bekommt eine neue Version (Playlist-Einträge merken sich jetzt, wann sie hinzugefügt wurden – für eine Funktion nach 1.4). Eine mit 1.4 gemachte Sicherung lässt sich mit 1.3 nicht öffnen; zurück zu 1.3 braucht eine Sicherung von vor dem Update.

### Bekannte Fehler in 1.4

- Weiterhin wie in 1.3: während ein Video läuft, pausieren Bild und Ton kurz beim Wechsel zu oder von seiner Episodenseite (das Video wird an derselben Stelle neu geladen). Ein Umbau mit einem Bild, das zwischen den Seiten wandert, ist geplant.

## 1.3 (2026-11-06)

Zwei Themen: Video-Podcasts und eine App, die leichter zu bedienen ist – ein Usability-Check mit Blindtests und die Rückmeldungen aus dem Forum.

### Leichter zu bedienen – die letzte Runde (6. Nov.)

- Ein Stil für alle Knöpfe: abgerundete Pillen – gefüllt für die Hauptaktion einer Seite, umrandet für die anderen, rot für Löschen und Abo beenden
- Ein dezenter Pfeil an jeder Zeile, die eine eigene Seite öffnet (Einstellungen, „Ordner“, „Gerät“, „Downloads verschieben“ …)
- Die Player-Leiste nach oben wischen öffnet sie, nach unten schließt sie – auch nach unten über ihre Reiter (Kapitel, Shownotes, Lesezeichen, Als Nächstes); der Pfeilknopf bleibt
- „Nur die neueste als ungehört behalten?“ nach dem Abonnieren: klarerer Text, die zwei Möglichkeiten als Knöpfe direkt darunter
- Das Menü des letzten „Zuletzt gehört“-Eintrags in der Playlist scrollt ins Bild
- Seite Downloads: „Download-Einstellungen“ im Pulldown-Menü; „Gehörte Downloads löschen“ als roter Knopf
- Die Legende auf der Seite Podcasts sagt, was der Punkt bedeutet: „neu, seit du den Podcast zuletzt geöffnet hast“
- Die Geschwindigkeit gilt ab der ersten Sekunde auch für die erste Folge nach dem Start und für Video

### Bekannte Fehler in 1.3

- Während ein Video läuft, pausieren Bild und Ton kurz beim Wechsel zu oder von seiner Episodenseite: das Bild wird an den Player gehängt oder von ihm genommen, der das Video an derselben Stelle neu lädt (nach der Seitenanimation, damit der Bildschirm nicht mehr ruckelt).

### Außerdem in 1.3

- Eine Player-Ansicht: die Player-Leiste und der Querformat-Player zeigen dieselben Reiter (Kapitel, Shownotes, Lesezeichen, Als Nächstes); die Episodenseite hat dieselben Reiter und immer Zurück-/Weiter-Knöpfe (auch in der Videoansicht)
- Jeden Symbolknopf gedrückt halten zeigt seinen Namen
- Ordnerköpfe wie bisher, mit einer kleinen Erklärung von Punkt und Zahl unter dem ersten offenen Ordner
- Podcastseite: „Angezeigt: alle Folgen / nur ungehörte“ über der Liste; „Podcast anzeigen“ im Folgenmenü
- Behoben: die Sleep-Zeile bleibt nicht mehr über der eingeklappten Leiste offen; „Weiter“ im letzten Kapitel der Episodenseite geht zur nächsten Folge; gehörte Folgen starten nicht mehr an ihrem Ende (der Player lief durch sie durch); kein „−--:--“ mehr am Ende
- Klarer für Neulinge (aus einem blinden Usability-Test): „Meine Podcasts durchsuchen“ in den Pulldown-Menüs, eine kleine Legende für Punkte und Zahlen auf der Seite Podcasts, „3110 ungehört“ und ein klarer Klapp-Pfeil in Ordnerköpfen, „4 geladen“ in der Liste, „Lesezeichen“ auch in den Pulldown-Menüs von Neue Folgen und Podcasts
- Ein Lesezeichen-Band statt des Sterns für Lesezeichen
- Die Namen der Nachbarseiten (wischen oder tippen) unter dem Seitenkopf der vier Hauptseiten
- „Archiv“ heißt nur noch der Ordner mit Kopien: der Schalter heißt jetzt „Podcast ist beendet“, der Knopf „Podcast in Archivordner kopieren“
- Sleep-Timer: 5, 10, 20, 30, 45, 60 Minuten und ein klares „Ende der Folge“; der Geschwindigkeitsknopf ist auch im Querformat-Player; „Zurück“ über den Reitern des Players
- Playlist-Knopf in jeder Folgenzeile (Linien mit „+“: ans Ende hinzufügen; mit Haken: in der Playlist, Tippen nimmt sie heraus) – auch auf der Episodenseite, statt des Symbols, das wie ein Play-Knopf aussah
- „Nur ungehörte zeigen“ / „Alle Folgen zeigen“ als Knopf auf der Podcastseite
- Direkt nach dem Abonnieren: „Nur die neueste als ungehört behalten?“ auf der Podcastseite (einmal pro Podcast)
- Eine mittendrin übersprungene Folge startet wieder dort, wo du sie verlassen hast (aus „Zuletzt gehört“), auch wenn du sie wieder verlassen hast, bevor sie startete, oder die App geschlossen hast
- Die nächste Folge der Playlist spielt jetzt von Anfang an mit der eingestellten Geschwindigkeit (sie begann bis zum ersten Sprung mit 1,0×)
- Einstellungen → Wiedergabe: dort lässt sich die globale Geschwindigkeit einstellen (vorher nur im Player)
- Einfache Worte: was die Synchronisierung tut, „Sonstige Podcasts“ (= kein Ordner), „Podcast archivieren“, „Import und Export“, und dass Ziehen in der Playlist die Abspielreihenfolge festlegt
- „Vorige: …“ in der Player-Leiste (Reiter „Als Nächstes“): ein Tippen zurück zur zuvor gehörten Folge
- „Aus Neue Folgen entfernen“ im Folgenmenü – raus aus der Liste, ohne als gehört zu gelten
- Der Punkt an einem Podcast verschwindet auch, wenn seine neuen Folgen anderswo gehört wurden
- Downloads leichter zu finden: in den Pulldown-Menüs von Neue Folgen und Podcasts, in den Download-Einstellungen und durch Tippen auf die Kachel „geladen“; „Gehörte Downloads löschen“ als Knopf auf der Seite Downloads
- Den Lesezeichen- oder Sleep-Timer-Knopf in der Player-Leiste gedrückt halten zeigt seinen Namen
- Klarere Worte: „Ordner ausblenden“ / „Ordner einblenden“, „Das Ende überspringen (Outro)“, „Endet hier“, die Suche in Entdecken

- Seite Podcasts: „Ordner: an/aus“ zeigt alle Podcasts in einer Liste in der gewählten Reihenfolge (zum Beispiel die neuesten Abos)
- Kamera auf den Covern von Video-Folgen
- Neue Einstellung „Als gehört werten, wenn weniger übrig ist“ (Wiedergabe): Pausieren, Wechseln oder Schließen kurz vor dem Ende markiert die Folge als gehört
- Video-Streams: kein hörbarer Stream-Anfang mehr nach einem Seitenwechsel, die Position wird zuverlässig gefunden
- „Nur Ton“ sagt, wann es keine Daten spart

- Doppelte Abos (Feed-Adressen, die sich nur in Groß-/Kleinschreibung unterscheiden, aus dem Sync) werden automatisch zusammengeführt; vorher wird gesichert
- „Alle als gehört markieren …“ (Einstellungen → Allgemein) lässt dich die Podcasts wählen
- Kamerasymbol auf dem Cover in der Podcast-Liste
- Korrekte englische Einzahl („1 episode“)
- „Ungespielte zur Playlist“ zeigt die Anzahl und lässt sich rückgängig machen
- Einen Sprungknopf gedrückt halten zeigt die laufende Summe
- Ordner-Export: die Schalter zeigen, was exportiert wird

### Video-Podcasts (neu)

- Video-Folgen zeigen ihr Bild auf der Episodenseite; Tippen für Vollbild im Querformat (✕ beendet es); das Display bleibt an, solange ein Video läuft, das Folgenbild erscheint, solange es pausiert
- Umschalter „Nur Ton“ / „Video“ auf dem Bild
- Feeds, die eine Audio- und eine Video-Fassung anbieten: pro Podcast wählen („Fassung“) oder mit dem Umschalter – die Audio-Fassung ist die, die wirklich Daten spart; Downloads bleiben Audio
- HLS-Video-Streams
- Kamerasymbol an Video-Folgen und Video-Podcasts (Liste und Raster), „Video-Podcast“ auf der Podcastseite und in „Podcast hinzufügen“
- Mobile Daten: „Videos ohne WLAN streamen“ – fragen (mit der Größe), immer oder nie (dann die Audio-Fassung, falls es eine gibt); Video-Downloads fragen ohne WLAN immer
- Videodateien von „Downloads im Musik-Ordner“ und „Speichern in …“ kommen nach Videos/Podcasts in einen Ordner pro Podcast (neue Berechtigung „Videos“)

### Mehr

- Die Podcast-Liste sortieren: alphabetisch, neueste Folge, zuletzt abonniert oder meiste ungehörte Folgen („Sortiert: … ›“ oben auf der Seite Podcasts)
- Die Sprungknöpfe gedrückt halten für einen größeren Schritt (1 min, einstellbar), wiederholt, solange gehalten
- Einstellungen → Allgemein: „Alle als gehört markieren …“ – alle Folgen oder alle außer der neuesten jedes Podcasts (Forum)
- Die Playlist-Kacheln zählen die laufende Folge in beiden Zahlen mit, folgen der Wiedergabe und zeigen die Restzeit bei der aktuellen Geschwindigkeit
- Chips „Alle“ / „Keine“ in jeder Auswahlliste (Downloads verschieben, Folgen herunterladen, Musik-Ordner, nie löschen, Podcasts in einem Ordner, zu exportierende Ordner)
- Die Hörzeit wird pro Tag aufgezeichnet (für die Statistik von 1.4)
- Behoben: Abstand über der Podcast-Statistik, „0 MB“ ohne Downloads, die letzte Zeile von „Downloads verschieben“ war verdeckt, „1 Download“

## 1.2 (2026-11-01)

Die erste Version auf OpenRepos und im Jolla Store.

### Entdecken (neu)

- Aus „Podcast suchen / hinzufügen“ wurde „Entdecken“: mit leerem Suchfeld zeigt es Vorschläge, Tippen sucht wie bisher in Podcast Index; eine Feed-Adresse wird sofort geprüft
- Drei Reiter:
  - „Für dich“ – persönliche Vorschläge aus dem, was du wirklich hörst (Massen-Markierungen zählen nicht): deine Sprachen, Kategorien und Stichwörter und wahlweise Podcasts, die deinen meistgehörten ähneln; jeder Vorschlag sagt warum („Weil du Geschichte hörst“, „Ähnlich wie …“); Spam wird gefiltert, „Kein Interesse“ blendet einen Podcast für immer aus
  - „Im Trend“ – Podcast Index und fyyd zusammen; jeder Podcast mit seiner Hauptkategorie, „Nie“-Kategorien weggelassen
  - „Neu“ – erste Folgen deutschsprachiger Podcasts, neu auf fyyd (Hörsuppes Liste „Nullnummern“); auf Englisch die Trailer neuer und bemerkenswerter Shows von Podnews („New Podcasts“) – ▶ spielt den Trailer, dann die erste Folge der Show; gekennzeichnet, wenn sie zu deinen Interessen passen; Shows in anderen Sprachen als deinen weggelassen; lässt sich ausschalten
  - eine Sprachzeile auf „Im Trend“ und „Neu“: alle deine Sprachen nebeneinander, „ändern ›“ führt zu „Meine Interessen“
  - „Als Nächstes“ in der Player-Leiste zeigt die Folge, die auf den Trailer folgt
  - lange Listen (20 weitere beim Scrollen), kompakt oder ausführlich mit der Beschreibung (Pulldown-Menü)
- „Meine Interessen“: Sprachen, Kategorie-Kacheln (mehr, weniger, nie, normal) und alle Unterkategorien, Stichwörter „Mehr davon“ / „Nie“ (mit eingebauter deutsch/englischer Wortliste, z. B. Klassik = classical), Schalter „Empfehlungen aus meinen Abos“; ein Kasten auf der Seite Entdecken zeigt, wovon „Für dich“ abhängt; beim allerersten Start „Was interessiert dich?“ mit den 12 beliebtesten Kategorien
- „Kategorien durchstöbern“: Kategorie-Kacheln → eine Seite mit den aktiven Podcasts dieser Kategorie in deinen Sprachen (Podcast Index und fyyd), Unterkategorien als Chips; von beiden Quellen genannte und zu dir passende Podcasts zuerst, abonnierte zuletzt; standardmäßig ausführlich
- „Ähnliche Podcasts“ auf der Podcastseite (neben dem Cover) und in „Podcast hinzufügen“; kennt fyyd einen Podcast nicht oder nichts Ähnliches, stattdessen aktive Podcasts seiner Kategorie; schon abonnierte kommen zuletzt („Podcast hinzufügen“ zeigt nur neue); kompakt oder ausführlich
- Hören ohne Abo: ▶ an den neuesten Folgen in „Podcast hinzufügen“ und in „Neu“, „Alle Folgen“ öffnet die Podcastseite als Vorschau; der Podcast bleibt versteckt (nicht in Listen, Sync oder Aktualisierungen), seine Folgen sagen „nicht abonniert“ und bieten „Abonnieren ›“ direkt auf der Seite an (was du gehört hast, bleibt; mit Ordnern wählst du zuerst einen); entfernt nach 30 Tagen ohne Benutzung
- Geteilte Folgen-Links (die Webseite einer Folge, auch Sailcasts eigene Links mit „#t=“ für die Zeit): „Podcast hinzufügen“ zeigt die geteilte Folge zuerst und spielt sie ab dieser Zeit
- Entdecken als Ringseite zwischen Neue Folgen und Podcasts (standardmäßig an, lässt sich in Einstellungen → Allgemein ausschalten)
- Datenschutz: nur Sprachen, Kategorien und Suchbegriffe verlassen das Telefon; mit „Empfehlungen aus meinen Abos“ werden die Adressen deiner 10 meistgehörten Podcasts an fyyd geschickt

### Podcasts

- Podcast-Einstellungen: ein eigener Name für den Podcast (leer = der Name aus dem Feed; nur auf diesem Telefon, nicht synchronisiert)
- Podcast-Einstellungen: neue Folgen automatisch herunterladen – wie global eingestellt, immer oder nie für diesen Podcast
- Podcast-Einstellungen: gehörte Downloads löschen – wie global eingestellt, nie (auch von „Gehörte Downloads löschen“ verschont), direkt nach dem Hören oder nach N Tagen
- Podcast-Einstellungen: „Folgen herunterladen …“ – alle oder nur die ungehörten, gruppiert nach Staffel oder Monat/Jahr; einzelne Folgen oder ganze Gruppen auswählen, mit Anzahl und Größe
- Podcastseite: das Öffnen der Beschreibung zeigt auch Statistik (gehörte Stunden, angefangene Folgen, Platz für Downloads, Veröffentlichungsrhythmus wie „wöchentlich, Montag“, voraussichtlich nächste Folge – Rhythmus und nächste Folge auch in „Podcast hinzufügen“) und „Über diesen Podcast“ – Kategorien (deutsche Namen), Sprache, Folgen, neueste Folge, Feed-Adresse, Webseite und Unterstützung
- Lange Folgenlisten (mehr als 50): Abschnitte nach Staffel (wenn der Feed Staffeln hat) oder nach Monat und Jahr, ein Griff am rechten Rand zum Springen, die ersten 50 und „Ältere Folgen zeigen“; einen Gruppenkopf antippen, um ihn herunterzuladen, seine ungehörten Folgen zur Playlist hinzuzufügen oder ihn als gehört zu markieren
- „Als gehört / ungehört markieren …“ auf der Podcastseite: die Folgen wählen – alle, alle außer der neuesten, einzelne oder eine und alle älteren (gedrückt halten); auch gehörte zurück auf ungehört
- „Ungespielte zur Playlist“ auf der Podcastseite: ein ganzer (kurzer, abgeschlossener) Podcast auf einmal; Podcast und Gruppen kommen älteste zuerst hinein
- Suche am Telefon: Schalter „Auch in Beschreibungen“ – Folgen, die in ihren Shownotes gefunden werden, mit der passenden Stelle
- Seite Podcasts: „Suchen“ (am Telefon) im Pulldown-Menü; Entdecken ist jetzt eine Ringseite (im Menü nur, wenn es aus dem Ring genommen ist)
- Podcastseite: „Mehr: Statistik und Infos“ unter der Beschreibung; Podcast hinzufügen: die Beschreibung oben, vier Zeilen, „Mehr“ für alles
- Kürzeres Pulldown-Menü auf der Podcastseite: Abo beenden, Anmeldung, Unterstützen und „Downloads in Musik speichern“ sind in die Podcast-Einstellungen gewandert
- Sprache und Kategorien jedes Podcasts werden aus seinem Feed gelesen (jeder Feed wird nach dem Update einmal vollständig geladen)

### Player und Playlist

- Player-Leiste: ein „Weiter“-Knopf (Kapitel, dann Folge) statt des Mondes; ein langer Kapitel- oder Podcastname in der zweiten Zeile läuft durch
- Aufgeklappte Player-Leiste: eine kompakte Positionsleiste zum Spulen, Springen, Geschwindigkeit, Lesezeichen (Name und Notiz im Feld von unten) und Sleep-Timer, darunter Reiter für die Kapitel, die Lesezeichen (mit Notizen und ihrem Menü) und „Als Nächstes“ – die nächsten Playlist-Folgen, mit „Als Nächstes abspielen“, „Aus der Playlist entfernen“ und „Ganze Playlist“
- Playlist: was du siehst, ist die Reihenfolge – die laufende Folge steht immer oben, die erste darunter spielt als nächste („Als Nächstes abspielen“ legt eine Folge dorthin)
- „Weiter“ überspringt eine Folge: sie gilt als gehört und verlässt die Playlist; „Zuletzt gehört“ unten auf der Playlist-Seite holt sie zurück, ab der Stelle, an der du sie verlassen hast
- Sleep-Timer: „Andere …“ mit eigener Dauer (Stunden und Minuten)
- Eine neue Geschwindigkeit wirkt beim Abspielen sofort (vorher erst nach dem nächsten Sprung)
- Streams, deren Länge der Player viel zu lang schätzt, behalten die Länge aus dem Feed für die Positionsleiste; ein fehlgeschlagener Sprung kehrt dorthin zurück, wo er war
- Eine Wiedergabe, die bei schwachem Netz hängt, zeigt ▶ statt Pause, und „Play“ lädt sie an derselben Stelle neu (einmal auch von selbst); während ein Stream lädt, zeigt die Positionsleiste, wo er weitergeht
- Eine Meldung, wenn das Markieren als gehört Downloads löscht („direkt nach dem Hören“)
- Playlist- und Downloads-Seite mit Kacheln: Restzeit, Folgen, geladen; Größe, gehört und freier Platz

### Folgen und Downloads

- Einzelne Folgen: „Datei teilen“ (die geladene Datei, z. B. per Mail oder Bluetooth) und „In Musik speichern“ (in den Archivordner des Podcasts in Musik/Podcasts verschoben, nie aufgeräumt)
- Podcast-Einstellungen: „Downloads in Musik speichern“ für alle seine Downloads
- Downloads im Musik-Ordner (standardmäßig aus – der Musikplayer würde sie auflisten): für alle Podcasts (Einstellungen → Downloads und Archiv) oder pro Podcast („Speicherort“ in den Podcast-Einstellungen, z. B. Hörbücher in Musik, Nachrichten am Telefon); dort sind sie für andere Apps sichtbar und werden wie jeder Download aufgeräumt – „In Musik speichern“ oder „Gehörte Downloads löschen: Nie“ behält sie
- Einstellungen → Downloads und Archiv: die Podcasts im Musik-Ordner und die, deren Downloads nie gelöscht werden, aus einer Liste aller Podcasts wählen (die zweite gruppiert nach Speicherort)
- Ein geänderter Speicherort bietet an, die vorhandenen Downloads zu verschieben – alle Podcasts oder ausgewählte, mit Anzahl und Größe (auch aus den Podcast-Einstellungen); die Seite Downloads zeigt, wie viel im Musik-Ordner liegt
- Halbfertige Downloads fehlgeschlagener Versuche werden nach einer Woche entfernt; in Musik werden nur Sailcasts eigene Dateien angefasst
- Seite Downloads: „Ungespielte zur Playlist“ (älteste zuerst), z. B. vor einer Reise ohne Netz

### Sonstiges

- OPML-Import: Schalter „Nur die neueste Folge ungehört“ (standardmäßig aus) – nach dem Laden gelten die älteren Folgen der importierten Podcasts als gehört, praktisch beim Umzug aus einer anderen App
- Links wie podcast://, pcast:// und itpc:// öffnen Sailcast (auch wenn es geschlossen ist) und zeigen den Podcast zum Abonnieren
- gpodder.net: vom Server umgeschriebene Feed-Adressen werden übernommen
- Ein sanfterer erster Start: Knöpfe auf der leeren Startseite und der Seite Podcasts, um Podcasts zu entdecken, den Sync einzurichten oder eine Liste zu importieren (mit einer Zeile, wo man sie später findet), „Was interessiert dich?“ in Entdecken mit live aktualisierten Vorschlägen und kurze einmalige Hinweise, wo sie helfen (Player-Leiste, Podcast-Menü, Ordner, Lesezeichen, Springen, „Meine Interessen“), auf lesbarem Hintergrund und nur einmal gezeigt
- Design: Playlist-Symbol und „↓ N“ statt Wörtern in Listen, kleineres ▶ in Zeilen, schlichte Gruppenköpfe, überarbeitete Erstbenutzer-Hinweise; der Schatten über der Player-Leiste passt zu hellen Ambiences
- Neue Datenbank-Version – nach dem Update können Versionen vor 1.2 die Datenbank nicht mehr öffnen (vorher sichern, falls du vielleicht zurück willst)

## 1.1 (2026-10-24)

Erstes Release als Paket, auf GitHub.

- Sailcast im Teilen-Menü („Podcast hinzufügen“): ein Link oder Text, geteilt aus dem Browser oder einer anderen App, öffnet „Podcast suchen / hinzufügen“ damit (auch wenn Sailcast geschlossen ist)
- Suche am Gerät: „Suchen“ in den Pulldown-Menüs von Neue Folgen und Playlist findet deine Podcasts, Folgentitel und Lesezeichen (auch mit Umlauten in jeder Schreibung) und führt für neue zu „Podcast suchen / hinzufügen“
- Eine Adresse (auch nur eine Domain wie „cre.fm“) wird sofort geprüft: eine Webseite wird nach dem von ihr genannten Podcast-Feed mit den meisten Folgen durchsucht; gibt es keinen, wird der Titel der Seite oder der Name der Site in Podcast Index gesucht (Links von Spotify, YouTube und anderen Plattformen suchen nach dem Namen des Podcasts, Apple-Podcasts-Links werden über ihre ID nachgeschlagen); ein gefundener Podcast erscheint als Karte mit seinem Cover
- Einen Folgen-Link aus ihrem Menü oder von der Episodenseite teilen, wahlweise ab der aktuellen Position
- Abschnitt „Sperrbildschirm und Cover“ in den Einstellungen: jede der beiden Tasten am Sperrbildschirm und Headset (Kapitel, Folge oder Sprung) und der zweite Knopf auf dem App-Cover (Weiter, zurück springen, vor springen, Sleep-Timer oder ein Lesezeichen)
- Automatische Datenbank-Sicherung (aus, täglich, wöchentlich) nach Downloads/Sailcast-Backups und, mit einem Nextcloud-Sync-Konto, in den Nextcloud-Ordner „Sailcast-Backups“ (die neuesten 5 bleiben); „Jetzt sichern“; eine Sicherung vom Telefon oder aus der Nextcloud wiederherstellen (der Stand davor bleibt erhalten und lässt sich wiederherstellen)
- Staffel- und Folgennummern aus dem Feed in den Folgenlisten und auf der Episodenseite; „Podcast unterstützen“ auf der Podcastseite (wenn der Feed einen Weg nennt)
- gpodder.net: wählen, welche Geräte ihre Abos teilen (Häkchen pro Gerät, aktuelle Sync-Gruppe vorgewählt)
- Ein Lesezeichen zu setzen öffnet ein kleines Feld für Name und Notiz (daneben tippen zum Überspringen)
- Einstellungen in Unterseiten (Allgemein, Synchronisierung, Wiedergabe, Downloads und Archiv, Sicherung, Import und Export), jede mit einer Zusammenfassung
- Einstellung „Sync und Update zusammen“ (aus: getrennt „Synchronisieren“ und „Aktualisieren“ in den Pulldown-Menüs); „Jetzt synchronisieren“ in den Einstellungen

## 1.0 (2026-10-22)

Erstes öffentliches Release (Quellcode auf GitHub).

- Neue Folgen, Podcasts (Raster oder Liste) und Playlist als Ringseiten; Startseite und Sync/Update beim Start wählbar
- Ordner für Podcasts: klappbare Abschnitte in Raster und Liste, „Sonstige Podcasts“, Ordnermenü (umbenennen, Reihenfolge, nur von Hand aktualisieren, als gehört markieren, löschen), mehrere Podcasts auf einmal wählen, Ordner im OPML-Import/-Export
- Podcast-Suche über Podcast Index mit Details; eine Feed-Adresse hinzufügen mit Prüfung vor dem Abonnieren (auch aus der Zwischenablage; eine Webseite: der Feed, den sie nennt)
- Playlist mit Ziehen zum Sortieren, „Läuft gerade“, „Weiterhören“; Wiedergabe-Kontext (Playlist oder einzelne Folge) mit einer Einstellung, was folgt
- Player auf der Episodenseite mit beschrifteter Werkzeugleiste (Geschwindigkeit, Download, Playlist, Lesezeichen, Sleep-Timer), Kapitel, Sprungknöpfe; Steuerung über Sperrbildschirm, App-Cover und Headset (nächstes/voriges Kapitel)
- Lesezeichen mit Notizen, Seite mit allen Lesezeichen
- Einstellungen pro Podcast: Ordner, bei „Aktualisieren“ einbeziehen, Geschwindigkeit, Intro und Outro überspringen
- Downloads mit Fortsetzen, nur mit WLAN, automatische Downloads, Aufräumen
- Archiv ganzer Podcasts in den Musik-Ordner, Cloud-Archiv-Adresse (rclone), Import von Archivordnern
- Sync mit Nextcloud GPodder Sync (Anmeldung im Browser) und der gpodder.net-API
- Import/Export: OPML, gpodder-sailfish-Datenbank, Datenbank-Sicherung
- Feeds mit Anmeldung (Passwort in Sailfish Secrets), Reparatur kaputter Feeds, private Feed-Tokens in Meldungen verborgen
- Querformat: Player als Spalte rechts, zweispaltige Episodenseite
- Deutsche Übersetzung
