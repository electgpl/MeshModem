# MeshModemSA — modem Meshtastic® por comandos AT + analizador de espectro LoRa

Firmware para la **ESP32 LoRa Zero** (ELECTGPL): un nodo LoRa compatible con la red Meshtastic 2.x que se maneja con comandos AT desde la terminal serie, desde un navegador (WiFi) o desde el celular por Bluetooth LE. Suma un **analizador de espectro** del SX1262 en la misma web del ESP32, para ver qué tan poblada está la banda LoRa en un lugar.

```
 Terminal serie ── USB ──┐
 Navegador ─────── HTTP ─┼──► intérprete AT único ──► SX1262 ──► malla Meshtastic
 Celular / PC ──── BLE ──┘    (las tres interfaces ven la misma salida)
                               └─► /spec: analizador de espectro (modo exclusivo)
```

- **Malla:** canal primario por defecto (PSK pública `AQ==`), texto broadcast y NodeInfo, retransmisión con las reglas del firmware oficial (managed flooding), en tres modos: mudo, cliente y router. Detecta los ecos de los mensajes propios como ACK implícito («repetido por N»).
- **Interfaces:** UART por USB, web por WiFi (red de casa o red propia del modem) y Bluetooth LE (Nordic UART Service). Todas usan el mismo intérprete AT y ven la misma salida.
- **Historial:** los mensajes quedan guardados en la flash (LittleFS) con fecha y hora, y sobreviven a reinicios.
- **Analizador de espectro:** barrido del SX1262 en GFSK con el RxBw como RBW. Muestra traza en vivo, máximo, promedio, waterfall, grilla de slots Meshtastic y ocupación por slot. Se ve en el navegador, en `/spec`.

> Meshtastic® es una marca registrada de Meshtastic LLC. Este proyecto es independiente y no está afiliado. No es compatible con la app oficial de Meshtastic: esa app usa su propia API protobuf (`ToRadio`/`FromRadio`), no una serie por BLE.

---

## Contenido del repositorio

| Archivo | Qué es |
|---|---|
| `MeshModemSA/MeshModemSA.ino` | Firmware (Arduino IDE). La web del chat y la del analizador van embebidas |
| `MeshModemSA/partitions.csv` | Opcional: tabla de particiones «Custom» (ver más abajo) |
| `mesh_ble.html` | Chat por Bluetooth (Web Bluetooth), para servir desde GitHub Pages |

El Arduino IDE exige que el `.ino` esté en una carpeta con su mismo nombre (`MeshModemSA/`).

## Hardware: ESP32 LoRa Zero rev 1.0

| Bloque | Detalle |
|---|---|
| MCU | ESP32-S3 sin flash interna ni PSRAM, flash externa GD25Q128 de 16 MB, USB nativo en USB-C |
| Radio | SX1262 con TCXO de 32 MHz alimentado por DIO3 a 1,8 V, regulador DC-DC del SX1262, filtro IPD915, switch BGS12WN6E6327 conmutado por DIO2 y conector U.FL |
| Pines SX1262 | NSS 8, SCK 9, MOSI 10, MISO 11, RST 12, BUSY 13, DIO1 14 |
| Alimentación | USB-C o 5 V del conector 2×20 (OR con LM66200), buck-boost TPS631000 a 3,3 V |
| Otros | Botones BOOT (GPIO0) y RESET, LED de encendido fijo. Formato y conector 2×20 de Raspberry Pi Zero |

No tiene batería, fuel gauge ni LEDs por GPIO. En el PCBA armado van TPS2116 en lugar del LM66200 y TCXO TG2520SMN en lugar del X1G0054210205 del esquemático.

## Compilación (Arduino IDE)

**Bibliotecas:** RadioLib 7.x u 8.x (la única externa; probada con 7.8.1 y 8.0.0) y core arduino-esp32 3.x. WiFi, WebServer, DNSServer, ESPmDNS, Preferences, LittleFS y BLE (NimBLE) vienen con el core.

| Opción | Valor |
|---|---|
| Board | ESP32S3 Dev Module |
| USB CDC On Boot | **Enabled** (terminal por el USB-C) |
| Flash Size | **16MB (128Mb)** |
| PSRAM | **Disabled** |
| Partition Scheme | **16M Flash (3MB APP/9.9MB FATFS)** (recomendado) o **Custom** |

Con «USB CDC On Boot: Disabled», la serie sale por U0TXD/U0RXD (GPIO43/44) del conector 2×20.

Hay dos esquemas de particiones válidos, y el firmware se adapta solo:

- **«16M Flash (3MB APP/9.9MB FATFS)»**, sin `partitions.csv` en la carpeta: dos slots de 3 MB para la aplicación y unos 9,9 MB de historial en la partición `ffat`.
- **«Custom»** con el `partitions.csv`: dos slots de 4 MB y unos 7,9 MB de historial en la partición `spiffs`.

Al arrancar, el firmware busca una partición `spiffs` y, si no la encuentra, usa `ffat`. En ambos casos la formatea como LittleFS la primera vez.

**Si se usa el esquema del menú, hay que borrar `partitions.csv` de la carpeta del sketch:** el core lo toma automáticamente si está presente, sin importar lo que se elija en el menú. Al cambiar de tabla de particiones se pierde el historial; la NVS (WiFi y configuración) se conserva. El firmware ocupa alrededor de 1,4 MB.

Antes de compilar conviene revisar el bloque `CONFIG` del `.ino`: `NODE_ID` (único por placa), nombres, preset, banda (ANZ 915–928 MHz por defecto), PIN de BLE y clave de la red propia.

---

## Formas de acceder al dispositivo

| Método | Necesita | Chat y AT | Analizador | Ideal para |
|---|---|---|---|---|
| 1. Terminal serie | Cable USB-C | Sí | Solo comandos | Banco, configuración inicial |
| 2. Web por la red de casa | WiFi de casa | Sí | Sí (`/spec`) | PC y celular en casa |
| 3. Web por la red propia | Nada (el modem es el AP) | Sí | Sí (`/spec`) | Calle, desde el celular |
| 4. BLE con apps de terminal | App BLE | Sí (texto AT) | Solo comandos | Pruebas rápidas sin WiFi |
| 5. `mesh_ble.html` | Navegador con Web Bluetooth | Sí | No | Chat en la calle sin WiFi |

### 1. Terminal serie (USB)

Monitor serie del Arduino IDE, PuTTY, Tera Term o Thonny, a 115200 8N1 (con USB CDC la velocidad no importa). Cada comando termina en CR, LF o CR+LF. Para empezar: `ATI` (toda la configuración y el estado) y `AT+HELP`.

Por la terminal se configura todo el nodo (ver [Comandos AT](#comandos-at)) y también se puede usar la malla:

```
AT+SEND=hola desde la terminal      envía un mensaje al canal
+TX:1a2b3c4d                        transmitido
+ECO:1a2b3c4d,relay=5C,ABCD,-92,6.5,2   otro nodo lo repitió
+RX:!a1b2c3d4,!ffffffff,-101,-3.2,1,hola   mensaje recibido
AT+NODES                            nodos escuchados
AT+LOG=20                           últimos 20 mensajes del historial
```

Todo lo que pasa en la web y por BLE se refleja en la terminal: los comandos recibidos por esas vías aparecen como `[web] comando` y `[ble] comando`.

### 2. Web por la red de casa (modo STA)

```
AT+WIFI=MiRed,MiClave
```

Las credenciales se guardan y el modem se conecta. `ATI` y `AT+WIFI?` informan la IP:

- **Chat:** `http://meshmodem.local/` o `http://<ip>/`
- **Analizador:** `http://meshmodem.local/spec`
- **Historial completo:** `http://meshmodem.local/log.jsonl`

La PC y el celular siguen con internet porque están en la misma red que el modem. Si el nombre `.local` no resuelve (pasa en algunos Android), hay que usar la IP. También se puede cargar la red sin terminal: sin credenciales guardadas, o manteniendo BOOT 5 s, el modem levanta un portal cautivo abierto `MeshModem-XXXX` con la página de configuración en `http://192.168.4.1`.

### 3. Web por la red propia del modem (modo AP, para la calle)

Sin la red de casa, el modem levanta **su propia red WiFi** con la web completa. No necesita internet.

| `AT+WIFIMODE` | Comportamiento |
|---|---|
| `AUTO` (de fábrica) | Intenta la red de casa. Si en 20 s no conecta, levanta la red propia hasta el próximo reinicio. Sin credenciales, va directo a la red propia |
| `STA` | Solo la red de casa. Sin credenciales, abre el portal de configuración |
| `AP` | Siempre la red propia |

- **Red:** `MeshModem-XXXX`, WPA2 con clave `meshmodem` (`AP_PASS`). Se cambia con `AT+APPASS=nueva_clave` (8 a 63 caracteres); `AT+APPASS=` sin valor la deja abierta.
- **Chat:** `http://192.168.4.1/` · **Analizador:** `http://192.168.4.1/spec` · **Red de casa:** `http://192.168.4.1/setup`.
- **iPhone:** Ajustes → Wi-Fi → `MeshModem-XXXX` → clave. iOS avisa «Sin conexión a internet», pero se queda conectado y usa los datos móviles para el resto. Con *Compartir → Agregar a inicio*, el chat queda como una app.
- **PC:** también puede conectarse a esta red, pero mientras tanto su placa WiFi no tiene internet (salvo que use cable de red). Para la PC conviene el método 2.
- **Hora:** sin NTP, el modem toma la hora del navegador al abrir la página (`AT+TIME=`), así el historial queda con hora real.

`AT+WIFI?` responde `+WIFI:AP,MeshModem-XXXX,192.168.4.1,CLIENTES=1,MODO=AUTO`.

### 4. Bluetooth LE con apps de terminal

Usa el **Nordic UART Service (NUS)**, el estándar de hecho para una serie por BLE:

| | UUID |
|---|---|
| Servicio | `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` |
| RX (escribe el celular) | `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` |
| TX (notifica el modem) | `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` |

- **Nombre:** `MeshModem-XXXX`. Funciona sin WiFi.
- **Emparejamiento:** escribir exige emparejar con PIN (passkey fijo, MITM, Secure Connections y bonding). El PIN de fábrica es `123456` (`BLE_PIN`) y se cambia con `AT+BLEPIN=nnnnnn`, que se aplica al reiniciar. Con `AT+BLEPIN=0` queda abierto: cualquiera cercano podría mandar comandos.
- **Comandos:** cada uno termina en CR o LF. Si la app no agrega terminador, se ejecuta igual tras 300 ms de silencio.
- **Respuestas:** notificaciones de hasta MTU−3 bytes (se pide MTU 247).

Apps probadas:

- **Android:** *Serial Bluetooth Terminal* (Kai Morich). Devices → Bluetooth LE → `MeshModem-XXXX`, y escribir `ATI`.
- **Android / iOS:** *nRF Connect*. Conectar, abrir el servicio Nordic UART, activar las notificaciones en TX y escribir texto UTF-8 en RX.
- **Bluefruit Connect:** modo UART.

### 5. Chat por Bluetooth: `mesh_ble.html`

Página Web Bluetooth con el mismo chat que la web del modem, conectada por BLE, sin WiFi:

- **Pestañas:** *Chat* (mensajes, NodeInfo, «✓ repetido por N» / «✗ sin eco»), *Nodos* y *Terminal* (líneas crudas del modem y una línea para escribir comandos AT).
- **Al conectar** pide `ATI`, `AT+NODES` y `AT+LOG=30`, toma la hora del teléfono y sigue en vivo. En el chat, una línea que empieza con `/` se manda como comando AT.

La página tiene que servirse por `https://`. Con GitHub Pages queda en `https://<usuario>.github.io/<repo>/mesh_ble.html`. No depende del modem, así que se puede guardar en la pantalla de inicio.

| Plataforma | Navegador |
|---|---|
| iPhone / iPad | **Bluefy** (gratis). Safari y Chrome de iOS no implementan Web Bluetooth |
| Android | Chrome |
| Windows / Mac / Linux | Chrome o Edge |

---

## Analizador de espectro

Herramienta para ver qué tan poblada está la banda LoRa en un lugar. Usa la misma radio que la malla, así que funciona en **modo exclusivo**:

- **Mientras está activo, el nodo no recibe, no envía y no repite paquetes.** Al entrar se descartan las retransmisiones pendientes. Los NodeInfo pedidos en ese tiempo (periódico, `AT+NAME`, `AT+PRESET`...) quedan pendientes y se envían al volver. `AT+SEND` y `AT+NI` responden `ERROR` con `+SPEC:ACTIVO`.
- **Los cambios de radio** (`AT+PRESET`, `AT+FREQ`, `AT+PWR`...) se aceptan y se aplican al volver a la malla.
- **Vuelve solo a la malla:** con «Detener», con `AT+SPEC=0`, al cerrar la página, a los **20 s sin pedidos** de `/api/spec` (`SA_LEASE_MS`) o a los **30 min** (`SA_MAX_MS`). Al volver, la radio se reinicializa con la misma secuencia de `setup()`: reset por RST y `begin()`.
- **Medición** (la del LoRaSA): modem en GFSK con el RxBw como RBW (4,8 a 467 kHz). Por punto: `SetRfFrequency` → `SetRx` continuo → espera → N × `GetRssiInst`, con detector de pico y promedio en potencia lineal. Va por SPI directo, sin depender de la versión de RadioLib. Se conservan el TCXO por DIO3 y el switch por DIO2.
- **El barrido se hace por tramos** de 8 ms por vuelta del `loop()`: la web, el BLE y la UART siguen respondiendo.
- **Por defecto barre la banda del nodo** (`AT+BAND`) con paso de 50 kHz, RBW de 117,3 kHz y N = 4. En ANZ son 261 puntos y unos 80 ms de medición.
- **Ocupación:** fracción de barridos en que el pico supera el piso (mediana del promedio) más un umbral ajustable (10 dB por defecto), en toda la banda y por slot Meshtastic. Depende del RBW y del umbral.

| Comando | Función |
|---|---|
| `AT+SPEC=1` / `AT+SPEC=0` | Entra al analizador (suspende la malla) / vuelve a la malla |
| `AT+SPEC?` | `+SPEC:ON,915.000,928.000,50.0,117.3,4,261,SETTLE=218us,BARRIDOS=n,QUEDAN=s` |
| `AT+SPECCFG=f0,f1,paso,rbw,n` | MHz, MHz, kHz, kHz (al RBW válido más cercano), muestras por punto 1..64; hasta 2048 puntos |

Si se entra con `AT+SPEC=1` desde la UART o BLE y nadie abre `/spec`, el modem vuelve a la malla a los 20 s. Repetir `AT+SPEC=1` renueva el plazo.

**Un solo cliente del analizador a la vez.** Cada lectura de `/api/spec` entrega el máximo acumulado desde la lectura anterior y lo reinicia: con dos navegadores abiertos en `/spec`, cada uno vería solo parte de los picos.

### Qué se puede ver y qué no

- **Piso de ruido** ≈ −174 + 10·log(RBW) + NF: con 117 kHz, unos −117 dBm. La malla decodifica hasta unos −127 dBm (SF9 / BW 250 kHz), así que los nodos con SNR negativa no aparecen en el espectro. Se ven la interferencia, la ocupación de la banda, las ráfagas fuertes y LoRaWAN.
- **Espurias propias a verificar** con carga de 50 Ω en el U.FL: 904, 912, 920 y 928 MHz (armónicas del SPI a 8 MHz), 920 MHz (XTAL de 40 MHz × 23), 928 MHz (TCXO de 32 MHz × 29). Una espuria fija aparece como un slot ocupado al 100 %. Las ráfagas de TX de WiFi y BLE pueden levantar el piso de forma momentánea.
- **Fuera de 902–928 MHz** el filtro IPD915 y la red de adaptación atenúan: las lecturas allí son solo relativas.

---

## Comandos AT

Los cambios se aplican en el momento. `AT+SAVE` guarda la configuración de radio en la flash (NVS); sin `AT+SAVE` se pierde al reiniciar. `AT&F` vuelve a los valores del bloque `CONFIG`.

| Comando | Función |
|---|---|
| `AT`, `AT+HELP`, `ATI` / `AT+INFO?` | Prueba, ayuda, configuración y estado completos |
| `AT+NODE=!aabbccdd` | NodeNum de 32 bit en hex (8 dígitos) |
| `AT+NAME=largo[,corto]` | Nombre largo (≤ 39) y corto (≤ 4) |
| `AT+PRESET=?` / `AT+PRESET=MediumFast` | Lista de presets / preset por nombre o número (0..8) |
| `AT+BAND=915,928` | Banda en MHz (ANZ 915,928 · US 902,928 · EU_868 869.4,869.65 · BR_902 902,907.5) |
| `AT+FREQ=926.125` / `AT+FREQ=0` | Frecuencia manual / slot automático |
| `AT+HOP=3` | Saltos máximos de lo que origina el nodo (0..7) |
| `AT+RELAY=0\|1\|2` | Retransmisión: mudo / cliente (por defecto) / router |
| `AT+PWR=20` | Potencia de TX en dBm (−9..22) |
| `AT+SEND=texto` | Texto broadcast al canal primario (≤ 232 bytes) |
| `AT+NI`, `AT+NODES` | Envía NodeInfo / lista de nodos escuchados |
| `AT+SAVE`, `AT&F`, `AT+RST` | Guardar en flash / valores de fábrica / reinicio |
| `AT+WIFI=ssid,clave`, `AT+WIFI?` | Credenciales de la red de casa / estado de WiFi |
| `AT+WIFIMODE=AUTO\|STA\|AP` | Modo WiFi (se guarda) |
| `AT+APPASS=clave` | Clave de la red propia (vacía = abierta) |
| `AT+PORTAL` | Portal de configuración WiFi (o BOOT 5 s) |
| `AT+TIME=epoch` | Hora en segundos UTC, si no hay NTP |
| `AT+BLE=0\|1`, `AT+BLE?`, `AT+BLEPIN=nnnnnn` | Bluetooth apagado / encendido, estado, PIN (0 = sin PIN) |
| `AT+SPEC=1\|0`, `AT+SPEC?`, `AT+SPECCFG=...` | Analizador de espectro (ver arriba) |
| `AT+LOG=20`, `AT+LOGCLR` | Últimas N entradas del historial / borrar historial |
| `AT+RAW=0\|1` | Traza de todo lo recibido |

**Respuestas no solicitadas:**

| Línea | Significado |
|---|---|
| `+RX:!from,!to,rssi,snr,saltos,texto` | Mensaje de texto recibido |
| `+NODE:!from,rssi,snr,corto,largo` | NodeInfo recibido |
| `+TX:id` | Mensaje propio transmitido |
| `+ECO:id,relay=XX,nombre,rssi,snr,hop_limit` | Otro nodo repitió un mensaje propio (`hop_limit` = HOP−1: vecino directo; menor: más lejos) |
| `+ECO:id,0` | Nadie lo repitió en 15 s |
| `+RELAY:!from,id,hop_limit` | Paquete retransmitido (con `AT+RAW=1`) |
| `+WIFI:UP,...`, `+WIFI:DOWN`, `+WIFI:AP,...`, `+WIFI:PORTAL,...` | Cambios de WiFi |
| `+BLE:CONECTADO`, `+BLE:DESCONECTADO` | Cambios de conexión Bluetooth |
| `+SPEC:ON,...`, `+SPEC:OFF,motivo,RADIO=e` | Entrada y salida del analizador |
| `[web] comando`, `[ble] comando` | Comando recibido por la web o por BLE |

---

## API HTTP

Es la que usa la web embebida, y sirve para integrar el modem en scripts propios.

| Ruta | Método | Respuesta |
|---|---|---|
| `/` | GET | Chat web (o el portal, si está activo) |
| `/spec` | GET | Página del analizador de espectro |
| `/setup` | GET | Configuración de la red de casa |
| `/api/at` | POST `cmd=<comando>` | Salida del comando AT en texto, terminada en `OK` o `ERROR` |
| `/api/info` | GET | JSON con la configuración y el estado: nodo, nombres, preset, banda, frecuencia, slot, `nch`, `bw`, potencia, retransmisión, WiFi, BLE, `spec`, nodos escuchados |
| `/api/ev?since=N&seen=0\|1` | GET | Eventos nuevos desde la secuencia N: `{"next":N,"ev":[...]}`. `seen=1` marca los mensajes como leídos |
| `/api/log?n=N` | GET | Últimas N entradas del historial (1..500): `{"next":N,"log":[...]}` |
| `/log.jsonl` | GET | Historial completo, una línea JSON por evento |
| `/api/spec` | GET | Último barrido en binario (ver abajo). Renueva el plazo del analizador. `409 OFF` si el analizador no está activo |
| `/scan`, `/wifi` | GET / POST | Escaneo de redes y alta de credenciales (portal) |

Ejemplo con curl:

```
curl -d "cmd=ATI" http://meshmodem.local/api/at
curl -d "cmd=AT+SEND=hola desde la PC" http://meshmodem.local/api/at
```

**Formato de `/api/spec`** (little-endian, cabecera de 32 B):

| Offset | Tipo | Campo |
|---|---|---|
| 0 | 2 × char | `'S' 'A'` |
| 2 | u8 | versión (1) |
| 3 | u8 | muestras por punto |
| 4 | u32 | número de barrido |
| 8 | u32 | duración del último barrido [µs] |
| 12 | f32 | frecuencia inicial [MHz] |
| 16 | f32 | paso [kHz] |
| 20 | f32 | RBW [kHz] |
| 24 | u16 | n, número de puntos |
| 26 | u16 | barridos acumulados desde la lectura anterior (0 = sin datos nuevos) |
| 28 | u16 | segundos que quedan de la sesión |
| 30 | u16 | espera tras `SetRx` [µs] |
| 32 | n × u8 | pico: máximo desde la lectura anterior |
| 32 + n | n × u8 | promedio del último barrido |

Cada byte vale −2·P [dBm], el formato nativo de `GetRssiInst`: P = −byte / 2. En Python: `struct.unpack_from("<2sBBIIfffHHHH", body)`.

---

## Notas técnicas

- **Pila BLE:** NimBLE, la que trae arduino-esp32 3.x para el S3. En el core 3.3.0 el Security Manager se configura directo en `ble_hs_cfg`, porque `BLESecurity::setStaticPIN()` no enlaza (la biblioteca declara miembros estáticos que nunca define). Desde 3.3.1 se usa `BLESecurity::setPassKey()`.
- **Concurrencia:** la tarea de NimBLE solo escribe en un anillo de 512 bytes protegido con spinlock. Todos los comandos (UART, web y BLE) se ejecutan en el `loop()`, así que no hay acceso concurrente a la radio. El analizador también corre en el `loop()`.
- **Coexistencia:** WiFi y BLE comparten la radio de 2,4 GHz del S3 por time-slicing. Para chat y comandos no se nota.
- **RadioLib:** compila con 7.8.1 y con 8.0.0. Una capa de compatibilidad aísla el cambio de API de la versión 8 (`ConfigLoRa_t`, frecuencias en Hz).
- **AES-128 propio** en modo CTR, compatible con el cifrado del firmware oficial (contador = id | from).

## Licencia

MIT. Autor: Electgpl.
