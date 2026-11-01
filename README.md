# DRM_Leveler SILVER

Plugin VST3 de nivelamento de bateria por **detecção de batidas**, inspirado no Sound Radix Drum Leveler. Visual prata da família SILVER (RefPro SILVER, SILVER-MB, VX_Leveler SILVER).

## Como funciona

Em vez de um compressor clássico (envelope follower + VCA), o plugin:

1. **detecta cada batida** no sinal de detecção (filtrado pelo SCF ou vindo do side-chain externo);
2. **mede o pico** da batida numa janela de 8 ms;
3. calcula **um único ganho para a batida inteira**, puxando-a em direção ao **Target Level**;
4. aplica o ganho com **lookahead**: a rampa de 1,5 ms termina exatamente no início do transiente, então o ataque não é amassado.

Latência: 9,5 ms, informada ao host (o REAPER compensa sozinho).

## Controles

| Controle | Função |
|---|---|
| **HI THRESH** (linha no display) | batidas acima passam sem processamento |
| **LO THRESH** (linha) | batidas abaixo não são consideradas batidas (o Gate age nelas) |
| **TARGET LVL** (linha laranja) | nível de destino de cada batida |
| **SENSITIVITY** | quanto maior, mais batidas leves são detectadas |
| **MIN RETRIGGER** | tempo mínimo entre duas batidas |
| **GAIN RANGE** | ganho máximo (±dB) aplicado a uma batida |
| **COMPRESSION** | +100% = todas as batidas vão para o alvo; 50% = metade do caminho; valores negativos = **expansão** (afasta as batidas do alvo). O anel em volta mostra o ganho atual (laranja = boost, azul = redução) |
| **HOLD / RECOVERY** | forma do ganho: tempo mantido após a batida e tempo de retorno |
| **GATE RANGE** | ganho fora das batidas: negativo = gate (reduz vazamento), positivo = realça o vazamento/ambiência |
| **SCF** + BAND-PASS / BAND-STOP + barra de 2 alças | filtro de 48 dB/oct só na detecção (ex.: caixa BP 180 Hz a 2,5 kHz; bumbo BS 200 Hz a 3 kHz) |
| **S** | solo: ouvir o sinal de detecção |
| **SC** | usar a entrada de side-chain externa (no REAPER: pinos 3/4 do plugin) |
| **PROCESS** | DUAL MONO (L e R independentes), STEREO (ligado), M/S |
| **VIEW** | LEFT/MID, SUM, RIGHT/SIDE: em SUM a edição vale para os dois canais; em LEFT/RIGHT só para aquele canal |
| **A / A>B / B** | comparação A/B e cópia de estado |
| **SOFT CLIP** | arredonda picos acima de 0 dBFS |
| **S-CURVE** | rampas de attack/recovery em curva S |
| **INPUT / OUTPUT** | faders por canal, com LINK (mantém o offset entre L e R) |

Display: entrada em cinza, boost em laranja, redução em azul, saída em branco; triângulos marcam as batidas detectadas (laranja = processada, cinza = ignorada por estar acima do HI). Duplo clique numa linha volta ao valor padrão. A janela pode ser redimensionada pelo canto.

## Compilar (GitHub Actions)

1. Suba esta pasta num repositório do GitHub.
2. O workflow `.github/workflows/build.yml` compila no Windows a cada push.
3. Baixe o artefato **DRM_Leveler_SILVER-VST3-Windows** na aba *Actions* e copie `DRM_Leveler SILVER.vst3` para `C:\Program Files\Common Files\VST3`.

Localmente: `cmake -B build -G "Visual Studio 17 2022" -A x64` e depois `cmake --build build --config Release`.
