# 🎳 Controle de Bowling — Wii Sports

Projeto de computação embarcada que implementa um controle gestual para **Wii Sports Bowling** rodando no emulador **Dolphin**, baseado no **Raspberry Pi Pico 2 (RP2350)** com **FreeRTOS SMP** nos dois cores e classificação de gestos **on-device com Edge Impulse**.

---

## Jogo

**Wii Sports — Bowling**

O jogo simula uma partida de boliche em que o jogador realiza o movimento físico de arremessar a bola. O controle detecta o gesto e o traduz em comandos compatíveis com o emulador Dolphin via dois caminhos paralelos:

- **Motion contínuo** (acelerômetro + giroscópio) → Dolphin via protocolo DSU (Cemuhook)
- **Botões + gesto especial** (HOME via shake reconhecido pela EI) → Dolphin como gamepad HID

---

## Ideia do controle

Inspirado visualmente no Wiimote, com:

- **TSW (C&K TSWB-3N)** — pad de navegação com **5 botões push** (centro + 4 direções) e **1 encoder rotativo** (24 detents, quadratura)
- **Botão B externo** (separado do TSW) — dedicado ao hold da bola durante o swing
- **1 LED de status** — controle conectado
- **Buzzer** — bipes no boot
- **Motor de vibração** — pulso no boot
- **Speaker (PCM)** — toca o som do "Wii Home" quando a EI reconhece o gesto de shake
- **IMU MPU6050** — acelerômetro + giroscópio (motion + dataset da EI)

| Sketch do projeto | Controle real (referência) |
|:-----------------:|:--------------------------:|
| ![Sketch](img/sketch.png) | ![Controle Real](img/controle_real.png) |

---

## Inputs e Outputs

### Inputs (sensores)

| Componente | Tipo | Pino | Função |
|---|---|---|---|
| TSW S1 (central push) | Digital — GPIO pull-up | GP10 | Botão A (confirmar / ação principal) |
| TSW S2/S3/S4/S5 (4 direções) | Digital — GPIO pull-up | GP11, GP12, GP13, GP15 | D-Pad UP / RIGHT / DOWN / LEFT (mapeamento depende da orientação física) |
| Botão B externo | Digital — GPIO pull-up | GP20 | Hold da bola durante o swing (lado direito do Pico, acessível ao polegar oposto) |
| TSW encoder (rotação) | Quadratura — **GPIO IRQ** | GP2 (A) / GP3 (B) | Fonte alternativa pra LEFT/RIGHT (cada detent = 1 evento) |
| MPU6050 (accel + gyro) | Analógico — **I²C0** @ 400 kHz | GP4 (SDA) / GP5 (SCL) | Motion completo (swing, tilt) + dataset da EI |

### Outputs (atuadores)

| Componente | Tipo | Pino | Função |
|---|---|---|---|
| LED de status | Digital — GPIO | GP14 | Aceso = controle conectado |
| Buzzer passivo | PWM | GP17 | Bipes de boot (440 Hz / 880 Hz) |
| Motor de vibração | Digital — GPIO + MOSFET | GP16 | Pulso no boot ("vivo") |
| Speaker (PCM) | PWM + IRQ + filtro RC | GP19 | Som do Wii Home ao detectar shake (PCM 8-bit @ 11 kHz, 17827 samples) |
| USB HID Gamepad | USB nativo | — | Botões físicos + HOME (acionado pela EI) |
| USB CDC Serial | USB nativo | — | Stream de motion (CSV) pro PC |
| UART debug | UART0 | GP0 (TX) / GP1 (RX) | Logs via conversor USB-Serial externo |

---

## Protocolo utilizado

O firmware expõe **um único dispositivo USB composite** com dois canais lógicos:

### 1. **HID Gamepad** (botões)
Report de 32 botões. O PC enxerga como joystick padrão.
- Bit 0 → A (TSW S1)
- Bit 1 → B (botão externo)
- Bit 2 → D-Pad LEFT (TSW direcional **ou** encoder CCW)
- Bit 3 → D-Pad RIGHT (TSW direcional **ou** encoder CW)
- Bit 4 → **HOME** (virtual — disparado pela EI ao detectar shake sustentado)
- Bit 5 → D-Pad UP (TSW direcional)
- Bit 6 → D-Pad DOWN (TSW direcional)

### 2. **CDC Serial** (motion)
Stream contínuo a ~100 Hz no formato CSV:

```
ax,ay,az,gx,gy,gz\n
```

Inteiros 16-bit raw do MPU6050 (range default ±2 g e ±250 dps).

### 3. **PC bridge → Dolphin via DSU/Cemuhook**

Um script Python (`bridge/bridge.py`) roda no PC, lê o CSV da CDC, converte pra unidades físicas (g, °/s) e expõe como **servidor DSU UDP em `127.0.0.1:26760`**. O Dolphin consome esse servidor como Wii Remote emulado (Config → Controllers → Fontes de Entrada Adicionais → DSU Client).

```
Pico 2  ──CDC USB──>  bridge.py  ──UDP 26760──>  Dolphin
        ──HID USB──>  Windows joystick  ──>  Dolphin (mapeamento direto)
```

---

## Arquitetura do firmware

Projeto rodando em **FreeRTOS SMP nos 2 cores do RP2350**, com isolamento por affinity:

- **Core 0** — I/O e timing crítico (USB, IMU, motion TX, botões, feedback)
- **Core 1** — Inferência Edge Impulse (isolada, prioridade baixa, fila dedicada)

A separação garante que o classificador de gestos **nunca interfere no pipeline de motion** que vai pro jogo.

![Diagrama](img/diagrama_aps2_.png)

### Tasks

| Task | Core | Descrição |
|---|---|---|
| `heartbeat_task` | qualquer | Pisca LED onboard (GP25) a 1 Hz |
| `imu_task` | Core 0 | Lê MPU6050 a 200 Hz, publica em 2 filas (auto-recovery se chip travar) |
| `motion_tx_task` | Core 0 | Drena `g_imu_queue`, decima pra 100 Hz, envia CSV via CDC |
| `button_task` | Core 0 | Polling de 6 botões a 100 Hz com debounce **assimétrico** (3 amostras pra press, 15 pra release — ignora bounces durante swing) |
| `encoder_task` | Core 0 | Drena `g_encoder_queue` populada pela ISR e dispara pulses HID curtos (50 ms) em LEFT/RIGHT |
| `usb_task` | Core 0 | Loop TinyUSB (`tud_task()`), envia HID reports quando estado muda |
| `feedback_task` | qualquer | Sequência de boot (LED + buzzer + vibra), chama `audio_init()` |
| `ei_inference_task` | **Core 1** | Coleta janela de 1 s (100 amostras × 6 eixos), `run_classifier()`, dispara HOME + áudio quando shake é **sustentado por 2 janelas** (~2 s contínuos) |

### Filas (Queues)

| Queue | Produtor → Consumidor | Função |
|---|---|---|
| `g_imu_queue` (16 slots, drop-old) | `imu_task` → `motion_tx_task` | Stream a 200 Hz pra bridge |
| `g_imu_ei_queue` (32 slots, drop-old, decimada 100 Hz) | `imu_task` → `ei_inference_task` | Stream pra inferência (fila separada evita competição com motion) |
| `g_encoder_queue` (32 slots, items `int8_t`) | `encoder_gpio_irq` → `encoder_task` | Eventos do encoder (+1 = CW, -1 = CCW) |

### Semáforos / Mutexes

| Recurso | Sinaliza | Função |
|---|---|---|
| `s_audio_sem` (binário) | `pwm_audio_irq` → `play_audio()` caller | Sinaliza fim da reprodução PCM |
| `s_state_mutex` | `button_task` + `ei_inference_task` ↔ `usb_task` | Protege estado do HID buttons (interno do usb_task) |

### ISRs

| ISR | Trigger | Função |
|---|---|---|
| `pwm_audio_irq` | `PWM_IRQ_WRAP` do slice de GP19 @ 88 kHz | Lê próximo sample do array PCM, escreve `pwm_set_gpio_level`. Ao terminar, libera `s_audio_sem` via `xSemaphoreGiveFromISR` |
| `encoder_gpio_irq` | GPIO IRQ em GP2 (ambas as bordas) | Lê GP3, determina sentido (CW/CCW), posta em `g_encoder_queue` via `xQueueSendFromISR` |

> **Botões físicos e IMU NÃO usam ISR** — são lidos por polling com debounce/decimação. TinyUSB tem ISR USB interna gerenciada pelo SDK (encapsulada na `usb_task`).

---

## Edge Impulse (gesto on-device)

Modelo binário **idle / shake_horizontal** treinado no EI Studio:

- **Input**: 6 eixos (aX, aY, aZ, gX, gY, gZ) raw do MPU6050
- **Janela**: 100 amostras × 6 eixos = 600 features, a 100 Hz (~1 s por inferência)
- **DSP block**: Spectral Analysis (oscilação lateral vira assinatura espectral limpa)
- **Classificador**: rede densa pequena, int8-quantizada

### Defesas contra falso positivo

1. **Threshold de confiança**: shake só conta se `score ≥ 0.92`
2. **Sustained windows**: exige **2 janelas consecutivas** classificadas como shake (≈ 2 s contínuos) antes de disparar. Movimentos laterais curtos durante o jogo nunca acumulam streak suficiente.
3. **Cooldown**: 2 s entre disparos
4. **Dataset balanceado**: classe `idle` foi treinada incluindo swings de bowling rotulados como idle, ensinando o modelo a não confundir
