# 72Volts Level Charger Cutoff System

Sistema di monitoraggio delle tensioni di un pacco composto da **6 batterie in serie**. Il firmware controlla i singoli elementi e comanda un relè per interrompere la ricarica quando viene confermata una condizione di sovratensione.

> **Nota:** il progetto è un sistema di controllo sperimentale. Le soglie e i collegamenti devono essere verificati sull'hardware reale; non sostituisce le protezioni elettriche o un BMS adeguato.

## Funzionalità principali

- **6 batterie monitorate indipendentemente** tramite gli ingressi analogici **A0–A5**.
- **Acquisizione ogni 250 ms** (intervallo minimo tra gli inizi dei cicli di misura).
- Acquisizione completa dei sei canali prima della valutazione delle anomalie.
- Calcolo della tensione di ciascuna batteria a partire dalle tensioni dei nodi del pacco.
- **10 letture HIGH consecutive sulla stessa batteria** necessarie per confermare lo sgancio.
- Un **contatore indipendente per ogni batteria**: le anomalie rilevate su batterie differenti non si sommano.
- Una lettura senza anomalia HIGH (normale, LOW o non valida) **azzera il contatore** della batteria interessata.
- Le anomalie **LOW** vengono riconosciute e possono essere mostrate nel debug, ma **non provocano lo sgancio**.
- Dopo lo sgancio, il relè **non viene riarmato automaticamente**: il sistema richiede un riavvio.
- La rilevazione delle tensioni continua anche dopo lo sgancio, ma la stampa seriale di debug termina dopo il resoconto finale.

## Soglie attualmente impostate nel firmware

| Grandezza | Condizione LOW | Condizione HIGH | Effetto sullo sgancio |
| --- | --- | --- | --- |
| Tensione letta dall'ADC | < 2,00 V | >= 4,80 V | Conta soltanto HIGH |
| Tensione della singola batteria | < 11,00 V | >= 14,50 V | Conta soltanto HIGH |

La conferma di **10 letture consecutive** si applica quando è presente **almeno una** condizione HIGH sul canale: `ADC_HIGH` oppure `BATTERY_HIGH`. Non è necessario che sia sempre lo stesso tipo di HIGH; deve invece essere **la stessa batteria** a presentare un'anomalia HIGH in ciascuna delle 10 letture.

Le soglie sono costanti del codice; vanno considerate valori configurati nel firmware, non limiti universalmente validi per qualunque batteria.

## Logica di sgancio

1. A ogni ciclo vengono acquisiti i sei ingressi analogici e calcolate le tensioni delle sei batterie.
2. Per ogni batteria si determina l'eventuale anomalia (ADC LOW/HIGH, batteria LOW/HIGH o lettura non valida).
3. Se per quella batteria è presente un'anomalia HIGH, il suo contatore aumenta di 1, fino a 10.
4. Se per quella batteria **non** è presente un'anomalia HIGH, il suo contatore torna a 0.
5. Quando il contatore di **una singola batteria** raggiunge 10, il firmware porta il pin del relè a LOW e memorizza la causa del primo sgancio.
6. Il relè resta nello stato di sgancio fino al riavvio; i controlli non ne provocano la riattivazione.

**Esempio:** B1 registra 7 HIGH consecutivi e B2 ne registra 3: **nessuno sgancio**, perché i contatori non si sommano. Se B1 torna normale, il suo contatore riparte da 0. Se in seguito B2 arriva a 10 HIGH consecutivi, viene comandato lo sgancio.

La precedente condizione aggiuntiva dei **500 ms è stata rimossa**. Rimane l'intervallo di lettura da **250 ms**, senza un ulteriore vincolo temporale per confermare lo sgancio.

## Relè e acquisizione

| Parametro | Configurazione attuale |
| --- | --- |
| Microcontrollore | Arduino Nano / ATmega328P (configurazione del progetto Visual Micro) |
| Ingressi delle batterie | A0, A1, A2, A3, A4, A5 |
| Pin di controllo del relè | D5 |
| Stato iniziale del pin relè | HIGH |
| Comando di sgancio | LOW |
| Campioni ADC validi per canale | 16 (dopo una lettura iniziale scartata) |
| Riferimento ADC impostato nel firmware | DEFAULT, con costante di calcolo a 5,00 V |

Il livello elettrico HIGH/LOW del pin deve essere coerente con il circuito di pilotaggio del relè effettivamente utilizzato.

## Diagnostica e registrazione dello sgancio

Il firmware conserva in memoria RAM le informazioni relative al **primo sgancio**: batteria coinvolta, flags dell'anomalia, tensione ADC, tensione della singola batteria e istante misurato con `millis()`.

Con `DEBUG_SERIAL=1` (configurazione attuale), la seriale funziona a **115200 baud**. Durante il funzionamento riporta le misure delle sei batterie e i contatori, con frequenza maggiore quando esistono allarmi in corso. Al momento dello sgancio stampa:

- l'ultima fotografia completa delle sei batterie;
- un messaggio finale `CUTOFF` con tempo, batteria, valori e causa.

Dopo il messaggio finale, il firmware non produce ulteriori messaggi seriali di debug. Questi dati **non sono salvati in EEPROM** e vengono persi al riavvio.

La trasmissione radio è attualmente disabilitata dalla costante `RF_ENABLED=0`.

## Codice sorgente

Il firmware principale è in [`72Volts-Charger-Cutoff-System/72Volts-Charger-Cutoff-System.ino`](72Volts-Charger-Cutoff-System/72Volts-Charger-Cutoff-System.ino).

Le funzionalità descritte sono riferite alla configurazione del codice disponibile sul branch `main` alla data del **14 ottobre 2026**.
