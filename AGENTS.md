# Istruzioni per questo progetto

- Prima di creare nuovi file che cambiano la struttura del progetto, dividere il codice in moduli o introdurre altre modifiche strutturali rilevanti, avvisare Luigi, spiegare cosa cambia e perche', e attendere il suo via libera. Le normali modifiche al codice nei file esistenti restano nell'ambito della richiesta autorizzata. Il vincolo vale anche per i subagenti.
- La compilazione viene eseguita da Luigi. Dopo le modifiche chiedergli di compilarle e comunicarne l'esito. Non avviare compilatori, build o compilazioni di test host senza autorizzazione esplicita per quella specifica esecuzione. Il vincolo vale anche per i subagenti.
- Il debug seriale segue FireAllarmRobot: `DEBUG_SERIAL`, macro `DEBUG_SERIAL_BEGIN/PRINT/PRINTLN`, stringhe costanti con `F()`. La modalita' produzione usa `DEBUG_SERIAL 0`; la diagnostica non deve influenzare la logica di protezione o la radio.
- I valori dei partitori nello sketch sono quelli del firmware originale. Il documento Word descrive componenti diversi: chiedere quali sono realmente montati prima di cambiare la calibrazione.
