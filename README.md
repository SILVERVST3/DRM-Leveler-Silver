# DRM_Leveler SILVER 1.1

VST3 / Standalone de nivelamento de bateria por detecção de batidas, com interface prata, azul-marinho, laranja e azul.

## Processamento

O detector localiza a batida, mede seu pico em uma janela de 8 ms e calcula um ganho limitado por Gain Range, Max Boost e Max Cut. Uma rampa de 1,5 ms antecipa o transiente; Hold e Recovery controlam a manutenção e o retorno ao Gate Range. Compression positivo aproxima a batida do Target; negativo expande sua dinâmica.

- **Measure Input desligado:** comportamento original; o pico do sinal de detecção, após o SCF ou recebido pelo sidechain externo, determina o ganho.
- **Measure Input ligado:** o sinal de detecção localiza e seleciona a batida pelos limiares HI/LO, enquanto o pico do áudio principal na mesma janela determina o ganho. A intensidade da chave externa deixa de definir o nível de destino do áudio principal.
- **Adaptive:** detecção opcional com contraste de subida e rearme com histerese. Ajuda a evitar redisparos e a identificar notas suaves após batidas fortes. Ajuste Sensitivity e Min Retrigger conforme a execução; não substitui a avaliação auditiva em gravações reais.
- **Wet Mix:** mistura linear entre a batida nivelada e o sinal sem nivelamento, ambos após o ganho de entrada e com a mesma compensação de latência. Ganho de saída e clipper atuam depois da mistura. Listen tem prioridade sobre o Mix.
- **Soft Clip:** joelho começa 3 dB abaixo do Clip Ceiling. A opção **4x Oversampling** reduz aliasing do estágio não linear. Ceiling é o teto nominal da curva, não uma garantia de true peak; a reconstrução dos filtros pode gerar overshoots.

A latência informada ao host inclui a janela de 8 ms, a rampa de 1,5 ms e a compensação inteira do oversampling. Ela permanece constante ao alterar Mix, bypass, clipper ou qualidade. Com o clipper desativado, o caminho de áudio permanece um atraso puro quando o ganho de nivelamento é zero. O bypass usa o áudio original com latência compensada e crossfade de 20 ms.

A troca de Dual Mono / Stereo / M/S faz uma transição de 5 ms para o áudio original, reinicia o processamento no novo domínio, aquece os atrasos e retorna gradualmente ao efeito. Durante a transição, os ajustes de entrada/saída do efeito são temporariamente cruzados com o áudio original.

## Interface

| Área | Controles |
|---|---|
| Detection | Sensitivity, Min Retrigger, HI/LO no gráfico, filtro SCF, Listen, sidechain externo, Adaptive |
| Leveling | Gain Range, Compression/Expansion, Target no gráfico, Max Boost, Max Cut, Wet Mix |
| Envelope | Hold, Recovery, Gate Range, S-Curve |
| Quality | Soft Clip, Clip Ceiling, 4x Oversampling |
| Input / Output | Faders por canal, link relativo e ativação dos canais |

- Duplo clique no valor de um knob permite digitar; duplo clique no knob restaura seu padrão. Shift permite ajuste fino durante o arraste.
- O gráfico tem pausa, zoom temporal e tooltip com nível/ganho. Cinza = entrada; branco = saída do nivelador antes do estágio final de saída; laranja = aumento; azul = redução. BOTH mostra o maior pico dos dois canais, não uma soma de áudio.
- Os medidores clássicos são o padrão. Clique para alternar para bandas espectrais; Shift-clique limpa o pico e o indicador de clipping.
- Os medidores **Output** mostram o áudio final L/R, após reconstrução M/S, Mix, clipper e bypass. Em M/S, os faders e botões M/S continuam controlando o domínio Mid/Side. Os medidores Input seguem o domínio de entrada selecionado.
- O status do sidechain informa a origem da chave. Sem barramento externo disponível, SC usa o sinal interno e informa essa condição.
- A/B, os dois snapshots e o slot ativo são salvos na sessão. A primeira troca para um slot vazio inicializa esse slot com o estado atual.
- O asterisco indica alterações nos parâmetros desde a seleção ou gravação do preset. SAVE/LOAD trabalham com arquivos `.drmpreset` e incluem os estados A/B. A pasta padrão é `SilverAudio/DRMLeveler/Presets` nos dados de aplicativo do usuário.
- O tamanho da janela é salvo na sessão. A análise espectral é suspensa quando os medidores estão no modo clássico ou a interface está fechada.

## Compatibilidade

IDs e ordem dos parâmetros existentes foram preservados; os novos parâmetros foram acrescentados ao final. Sessões antigas recebem Mix = 100%, limites adicionais = 80 dB, Ceiling = 0 dB e Measure Input / Adaptive / Oversampling desligados. Gain Range continua limitando os dois sentidos. O host recebe a nova latência ao preparar o plugin.

## Compilar e testar

O workflow real é `.github/workflows/built.yml`. A cada push/PR ele compila no Windows, executa os testes e disponibiliza VST3, Standalone e uma prévia do editor na aba Actions.

```sh
cmake -B build -G "Visual Studio 17 2022" -A x64 -DDRM_BUILD_TESTS=ON
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure
```

JUCE 8.0.4 é obtido pelo CMake. Para usar uma cópia local, passe `-DJUCE_LOCAL_PATH=/caminho/JUCE`.

Instale a pasta completa `DRM_Leveler SILVER.vst3` em `C:\Program Files\Common Files\VST3` e faça uma nova varredura no host.

Os testes cobrem automação do envelope em várias taxas, limites assimétricos, medição independente, detecção sintética, lookahead, Mix em diferentes tamanhos de bloco, mono com sidechain estéreo, medição final, transições de modo/qualidade, redução de aliasing em tom sintético, A/B e migração de estados antigos. A validação auditiva em DAWs e com gravações reais continua necessária antes de uma versão estável.
