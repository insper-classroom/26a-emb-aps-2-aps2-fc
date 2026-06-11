# bridge.py — Pico 2 CDC → DSU UDP para Dolphin

Lê dados do MPU6050 enviados pela CDC USB do Pico 2 e expõe como servidor
DSU (Cemuhook protocol) na porta UDP 26760 para o Dolphin consumir.

## Instalação

```bash
pip install pyserial
```

## Uso

1. Plugue o Pico 2 já flashado com o firmware da Fase 3+.
2. Descubra a COM port do Pico no Gerenciador de Dispositivos do Windows
   (procure por "Dispositivo Serial USB" — geralmente algo como `COM5`).
3. Rode:
   ```bash
   python bridge.py --port COM5 -v
   ```
4. No Dolphin:
   - `Config → Controllers → Configure (Wii Remote 1)`
   - Em **Alternate Input Sources**, clique em `Add` e insira `127.0.0.1:26760`.
   - No campo **Device** (topo), troque para `DSUClient/0/...`.
   - Na aba `Simulação de Movimentos`, mapeie:
     - **IR** (mira): pode deixar como mouse por enquanto.
     - **Swing**: arraste do "DSU" → eixo Y do acelerômetro.
     - **Tilt**: eixo do gyro/orientação.

## Formato de entrada esperado

Linhas CSV terminadas em `\n`:

```
ax,ay,az,gx,gy,gz
```

Todos inteiros raw 16-bit do MPU6050 nos ranges default
(±2 g para accel, ±250 dps para gyro). O bridge faz a conversão para
unidades físicas (g e °/s) antes de empacotar no DSU.

## Atalhos úteis

- `-v` mostra throughput de amostras/segundo e estado do motion.
- O servidor DSU também responde requisições de descoberta — abrir o
  Dolphin antes ou depois do bridge funciona em qualquer ordem.
