# DiffSinger 推理接口 Level 1（修订版）

模型变量的表示方式主要参考 [ONNX Operators](https://github.com/onnx/onnx/blob/main/docs/Operators.md)。Package 与 contribution 的公共结构遵循 [DiffSinger 数据格式与推理接口规范 2.4](ds-spec-2.4.md)。

本文规定以下五个推理 interface 的 Level 1 契约。当前参考实现使用 `onnx` variant。`exports`与 import `options`属于 (`interface`, `level`) 契约，`configuration`及模型变量属于 `onnx` variant。

| Interface | Level | Reference variant | Description |
| :-- | --: | :-- | :-- |
| `org.openvpi.dsinfer.inference.Duration` | 1 | `onnx` | 音素时长预测 |
| `org.openvpi.dsinfer.inference.Pitch` | 1 | `onnx` | 音高预测 |
| `org.openvpi.dsinfer.inference.Variance` | 1 | `onnx` | 唱法参数预测 |
| `org.openvpi.dsinfer.inference.Acoustic` | 1 | `onnx` | 声学特征生成 |
| `org.openvpi.dsinfer.inference.Vocoder` | 1 | `onnx` | 波形生成 |

## 前置说明

- Exports：模块公开的能力，由对应 (`interface`, `level`) 契约规定
- Import options：导入方引用模块时为该次导入指定的选项，由目标模块的 (`interface`, `level`) 契约规定
- Configuration：解释器加载模块所需的实现配置，由 `variant` 规定
- Model variables：模型推理过程中涉及的输入、输出和中间变量，由 `variant` 规定

本文表格中的 `path` 是 JSON 字符串。相对路径以当前 contribution 声明文件所在目录为基准。`array<T>`表示 JSON array，`map<K, V>`表示成员名为 `K`、成员值为 `V` 的 JSON object。

所有 `exports`和 import `options`都必须是 JSON object。除下文明确要求的字段外，其余字段可以省略。Variance 的 `exports.predictions`和 import `options.predictions`必须存在且非空。`speakerMapping`可以省略。

除 Vocoder 外，`onnx` configuration 中的 `phonemes`必须存在。Duration、Pitch 与 Variance 必须提供 `encoder`和`predictor`，Acoustic 与 Vocoder 必须提供`model`。Duration、Pitch 与 Variance 必须提供 `frameWidth`，或者同时提供正数 `sampleRate`和 `hopSize`，后者按 `hopSize / sampleRate`计算 `frameWidth`。启用 `useLanguageId`时必须提供 `languages`。启用 `useSpeakerEmbedding`时必须提供 `hiddenSize`和 `speakers`。其他字段省略时使用对应 Level 1 API 类型定义的默认值。

DiffSinger Level 1 Singer 必须分别以`singer/acoustic`和`singer/vocoder` role 导入 Acoustic 与 Vocoder Level 1 contribution。`singer/duration`、`singer/pitch`和`singer/variance` role 可以省略。Package 加载时必须验证 Vocoder 能够消费 Acoustic 的输出。当前`onnx` variant 要求两者 configuration 中的`sampleRate`、`hopSize`、`winSize`、`fftSize`、`melChannels`、`melMinFreq`、`melMaxFreq`、`melBase`和`melScale`全部相同，不满足时整个 Package 加载失败。

## `org.openvpi.dsinfer.inference.Duration`

### Exports

|   name   |        type        |      description       |       example        |
| :------: | :----------------: | :--------------------: | :------------------: |
| speakers | array&lt;string&gt; | 说话人（音色）名称列表 | ["zhibin", "qixuan"] |

### Import options

|      name      |        type         |               description                |        example         |
| :------------: | :-----------------: | :--------------------------------------: | :--------------------: |
| speakerMapping | map<string, string> | 歌手全局音色名称 => 模块内部嵌入名称映射 | {"zhu": "zhibin-base"} |

### Configuration for `onnx`

|        name         |             type             |                    description                     |                         example                          |
| :-----------------: | :--------------------------: | :------------------------------------------------: | :------------------------------------------------------: |
|      phonemes       | path | 存储音素名称与音素 ID 对应表的 JSON 文件 | "./assets/phonemes.json" |
|      languages      | path | 存储语言名称与语言 ID 对应表的 JSON 文件 | "./assets/languages.json" |
|      speakers       |      map<string, path>       |      说话人（音色）与说话人嵌入文件路径对应表      |          {"zhibin": "./embeddings/zhibin.emb"}           |
|       encoder       |             path             |                 编码器模型文件路径                 |               "./weights/linguistic.onnx"                |
|      predictor      |             path             |                 预测器模型文件路径                 |                "./weights/duration.onnx"                 |
|     frameWidth      |            double            |                    帧宽度（秒）                    |                    0.011609977324263                     |
|    useLanguageId    |           boolean            |                是否启用语言 ID 嵌入                |                           true                           |
| useSpeakerEmbedding |           boolean            |                 是否启用说话人嵌入                 |                           true                           |
|     hiddenSize      |           integer            |           隐层维度（说话人嵌入向量维度）           |                           256                            |
|      dur_type       |             enum             | predictor 的词级输入：`"rel"` 声明 `word_div` 与 `word_dur`（group-aware attn 预测器），`"abs"` 与省略都不声明（绝对时长预测器） |                          "rel"                           |

### Model variables for `onnx`

|   model   |     variable     |  I/O   |  type   |            shape            |     description      |    activation condition     |
| :-------: | :--------------: | :----: | :-----: | :-------------------------: | :------------------: | :-------------------------: |
|  encoder  |      tokens      | input  |  int64  |        (1, n_tokens)        |       音素 ID        |              -              |
|  encoder  |    languages     | input  |  int64  |        (1, n_tokens)        |       语言 ID        |    useLanguageId == true    |
|  encoder  |     word_div     | input  |  int64  |        (1, n_words)         |       音节划分       |              -              |
|  encoder  |     word_dur     | input  |  int64  |        (1, n_words)         |    音节长度（帧）    |              -              |
|  encoder  |   encoder_out    | output | float32 | (1, n_tokens, `hiddenSize`) |          -           |              -              |
|  encoder  |     x_masks      | output | boolean |        (1, n_tokens)        |          -           |              -              |
| predictor | encoder_out^[1]^ | input  |    -    |              -              |          -           |              -              |
| predictor |   x_masks^[2]^   | input  |    -    |              -              |          -           |              -              |
| predictor |     ph_midi      | input  |  int64  |        (1, n_tokens)        | 音素粗略音高（半音） |              -              |
| predictor |     word_div     | input  |  int64  |        (1, n_words)         |       音节划分       |      dur_type == "rel"      |
| predictor |     word_dur     | input  |  int64  |        (1, n_words)         |    音节长度（帧）    |      dur_type == "rel"      |
| predictor |    spk_embed     | input  | float32 | (1, n_tokens, `hiddenSize`) |  说话人（音色）嵌入  | useSpeakerEmbedding == true |
| predictor |   ph_dur_pred    | output | float32 |        (1, n_tokens)        |    音素长度预测值    |              -              |

[1] 该输入绑定到 encoder.outputs.encoder_out

[2] 该输入绑定到 encoder.outputs.x_masks

按词分配帧预算的时长模型（`dur_type` 为 `"rel"`）输出的 `ph_dur_pred`是每个音素的帧数，同一词内各音素之和等于该词的 `word_dur`。运行时只取每个词内各预测值的比例，再按乐谱给出的音节时长换算，因此消费方得到的时长与模型输出所用的单位无关。

## `org.openvpi.dsinfer.inference.Pitch`

### Exports

|        name         |        type        |      description       |       example        |
| :-----------------: | :----------------: | :--------------------: | :------------------: |
|      speakers       | array&lt;string&gt; | 说话人（音色）名称列表 | ["zhibin", "qixuan"] |
| allowExpressiveness |      boolean       | 是否允许控制表现力因子 |         true         |

### Import options

|      name      |        type         |               description                |        example         |
| :------------: | :-----------------: | :--------------------------------------: | :--------------------: |
| speakerMapping | map<string, string> | 歌手全局音色名称 => 模块内部嵌入名称映射 | {"zhu": "zhibin-base"} |

### Configuration for `onnx`

|           name            |             type             |                    description                     |                         example                          |
| :-----------------------: | :--------------------------: | :------------------------------------------------: | :------------------------------------------------------: |
|         phonemes          | path | 存储音素名称与音素 ID 对应表的 JSON 文件 | "./assets/phonemes.json" |
|         languages         | path | 存储语言名称与语言 ID 对应表的 JSON 文件 | "./assets/languages.json" |
|         speakers          |      map<string, path>       |      说话人（音色）与说话人嵌入文件路径对应表      |          {"zhibin": "./embeddings/zhibin.emb"}           |
|          encoder          |             path             |                 编码器模型文件路径                 |               "./weights/linguistic.onnx"                |
|      linguisticMode       |             enum             |     语言学编码器的工作模式（word 或 phoneme）      |                        "phoneme"                         |
|         predictor         |             path             |                 预测器模型文件路径                 |                  "./weights/pitch.onnx"                  |
|        frameWidth         |            double            |                    帧宽度（秒）                    |                    0.011609977324263                     |
|       useLanguageId       |           boolean            |                是否启用语言 ID 嵌入                |                           true                           |
|    useSpeakerEmbedding    |           boolean            |                 是否启用说话人嵌入                 |                           true                           |
|        hiddenSize         |           integer            |           隐层维度（说话人嵌入向量维度）           |                           256                            |
|     useExpressiveness     |           boolean            |               是否启用表现力因子输入               |                           true                           |
|       useRestFlags        |           boolean            |               是否启用休止符记号输入               |                           true                           |
| useContinuousAcceleration |           boolean            |                是否使用连续加速采样                |                           true                           |

### Model variables for `onnx`

|   model   |     variable     |  I/O   |  type   |            shape            |        description         |        activation condition        |
| :-------: | :--------------: | :----: | :-----: | :-------------------------: | :------------------------: | :--------------------------------: |
|  encoder  |      tokens      | input  |  int64  |        (1, n_tokens)        |          音素 ID           |                 -                  |
|  encoder  |    languages     | input  |  int64  |        (1, n_tokens)        |          语言 ID           |       useLanguageId == true        |
|  encoder  |     word_div     | input  |  int64  |        (1, n_words)         |          音节划分          |      linguisticMode == "word"      |
|  encoder  |     word_dur     | input  |  int64  |        (1, n_words)         |       音节长度（帧）       |      linguisticMode == "word"      |
|  encoder  |      ph_dur      | input  |  int64  |        (1, n_tokens)        |       音素长度（帧）       |    linguisticMode == "phoneme"     |
|  encoder  |   encoder_out    | output | float32 | (1, n_tokens, `hiddenSize`) |             -              |                 -                  |
| predictor | encoder_out^[1]^ | input  |    -    |              -              |             -              |                 -                  |
| predictor |      ph_dur      | input  |  int64  |        (1, n_tokens)        |       音素长度（帧）       |                 -                  |
| predictor |    note_midi     | input  | float32 |        (1, n_notes)         |      音符音高（半音）      |                 -                  |
| predictor |    note_rest     | input  | boolean |        (1, n_notes)         |         休止符记号         |        useRestFlags == true        |
| predictor |     note_dur     | input  |  int64  |        (1, n_notes)         |       音符长度（帧）       |                 -                  |
| predictor |      pitch       | input  |  int64  |        (1, n_frames)        | 音高（半音，作为重录条件） |                 -                  |
| predictor |       expr       | input  | float32 |        (1, n_frames)        |         表现力因子         |     useExpressiveness == true      |
| predictor |      retake      | input  | boolean |        (1, n_frames)        |          重录标记          |                 -                  |
| predictor |    spk_embed     | input  | float32 | (1, n_frames, `hiddenSize`) |     说话人（音色）嵌入     |    useSpeakerEmbedding == true     |
| predictor |     speedup      | input  |  int64  |           scalar            |           加速比           | useContinuousAcceleration == false |
| predictor |      steps       | input  |  int64  |           scalar            |          采样步数          | useContinuousAcceleration == true  |
| predictor |    pitch_pred    | output | float32 |        (1, n_frames)        |         音高预测值         |                 -                  |

[1] 该输入绑定到encoder.outputs.encoder_out

## `org.openvpi.dsinfer.inference.Variance`

### Exports

|    name     |        type        |                      description                       |          example           |
| :---------: | :----------------: | :----------------------------------------------------: | :------------------------: |
|  speakers   | array&lt;string&gt; |                 说话人（音色）名称列表                 |    ["zhibin", "qixuan"]    |
| predictions | array&lt;enum&gt; | 预测输出参数列表（energy/breathiness/voicing/tension/mouth_opening） | ["breathiness", "tension"] |

### Import options

|      name      |        type         |               description                |        example         |
| :------------: | :-----------------: | :--------------------------------------: | :--------------------: |
| speakerMapping | map<string, string> | 歌手全局音色名称 => 模块内部嵌入名称映射 | {"zhu": "zhibin-base"} |
| predictions | array&lt;enum&gt; | 使用的预测参数列表 | ["tension"] |

### Configuration for `onnx`

|           name            |             type             |                    description                     |                         example                          |
| :-----------------------: | :--------------------------: | :------------------------------------------------: | :------------------------------------------------------: |
|         phonemes          | path | 存储音素名称与音素 ID 对应表的 JSON 文件 | "./assets/phonemes.json" |
|         languages         | path | 存储语言名称与语言 ID 对应表的 JSON 文件 | "./assets/languages.json" |
|         speakers          |      map<string, path>       |      说话人（音色）与说话人嵌入文件路径对应表      |          {"zhibin": "./embeddings/zhibin.emb"}           |
|          encoder          |             path             |                 编码器模型文件路径                 |               "./weights/linguistic.onnx"                |
|      linguisticMode       |             enum             |     语言学编码器的工作模式（word 或 phoneme）      |                        "phoneme"                         |
|         predictor         |             path             |                 预测器模型文件路径                 |                "./weights/multivar.onnx"                 |
|        frameWidth         |            double            |                    帧宽度（秒）                    |                    0.011609977324263                     |
|       useLanguageId       |           boolean            |                是否启用语言 ID 嵌入                |                           true                           |
|    useSpeakerEmbedding    |           boolean            |                 是否启用说话人嵌入                 |                           true                           |
|        hiddenSize         |           integer            |           隐层维度（说话人嵌入向量维度）           |                           256                            |
| useContinuousAcceleration |           boolean            |                是否使用连续加速采样                |                           true                           |

### Model variables for `onnx`

|   model   |     variable     |  I/O   |  type   |               shape               |     description      |        activation condition        |
| :-------: | :--------------: | :----: | :-----: | :-------------------------------: | :------------------: | :--------------------------------: |
|  encoder  |      tokens      | input  |  int64  |           (1, n_tokens)           |       音素 ID        |                 -                  |
|  encoder  |    languages     | input  |  int64  |           (1, n_tokens)           |       语言 ID        |       useLanguageId == true        |
|  encoder  |     word_div     | input  |  int64  |           (1, n_words)            |       音节划分       |      linguisticMode == "word"      |
|  encoder  |     word_dur     | input  |  int64  |           (1, n_words)            |    音节长度（帧）    |      linguisticMode == "word"      |
|  encoder  |      ph_dur      | input  |  int64  |           (1, n_tokens)           |    音素长度（帧）    |    linguisticMode == "phoneme"     |
|  encoder  |   encoder_out    | output | float32 |    (1, n_tokens, `hiddenSize`)    |          -           |                 -                  |
| predictor | encoder_out^[1]^ | input  |    -    |                 -                 |          -           |                 -                  |
| predictor |      ph_dur      | input  |  int64  |           (1, n_tokens)           |    音素长度（帧）    |                 -                  |
| predictor |      pitch       | input  | float32 |           (1, n_frames)           |     音高（半音）     |                 -                  |
| predictor |      energy      | input  | float32 |           (1, n_frames)           | 能量（作为重录条件） |      "energy" in predictions       |
| predictor |   breathiness    | input  | float32 |           (1, n_frames)           | 气声（作为重录条件） |    "breathiness" in predictions    |
| predictor |     voicing      | input  | float32 |           (1, n_frames)           | 发声（作为重录条件） |      "voicing" in predictions      |
| predictor |     tension      | input  | float32 |           (1, n_frames)           | 发声（作为重录条件） |      "tension" in predictions      |
| predictor |  mouth_opening   | input  | float32 |           (1, n_frames)           | 嘴型开合（作为重录条件） | "mouth_opening" in predictions |
| predictor |      retake      | input  | boolean | (1, n_frames, `len(predictions)`) |       重录标记       |                 -                  |
| predictor |    spk_embed     | input  | float32 |    (1, n_frames, `hiddenSize`)    |  说话人（音色）嵌入  |    useSpeakerEmbedding == true     |
| predictor |     speedup      | input  |  int64  |              scalar               |        加速比        | useContinuousAcceleration == false |
| predictor |      steps       | input  |  int64  |              scalar               |       采样步数       | useContinuousAcceleration == true  |
| predictor |   energy_pred    | output | float32 |           (1, n_frames)           |      能量预测值      |      "energy" in predictions       |
| predictor | breathiness_pred | output | float32 |           (1, n_frames)           |      气声预测值      |    "breathiness" in predictions    |
| predictor |   voicing_pred   | output | float32 |           (1, n_frames)           |      发声预测值      |      "voicing" in predictions      |
| predictor |   tension_pred   | output | float32 |           (1, n_frames)           |      张力预测值      |      "tension" in predictions      |
| predictor | mouth_opening_pred | output | float32 |         (1, n_frames)           |    嘴型开合预测值    | "mouth_opening" in predictions |

[1] 该输入绑定到 encoder.outputs.encoder_out

## `org.openvpi.dsinfer.inference.Acoustic`

### Exports

|        name        |        type        |                         description                          |          example           |
| :----------------: | :----------------: | :----------------------------------------------------------: | :------------------------: |
|      speakers      | array&lt;string&gt; |                    说话人（音色）名称列表                    |    ["zhibin", "qixuan"]    |
|  varianceControls  | array&lt;enum&gt; | 需要输入的唱法参数列表（energy/breathiness/voicing/tension/mouth_opening） | ["breathiness", "tension"] |
| transitionControls | array&lt;enum&gt; |        支持的偏移变换类型参数列表（gender/velocity）         |   ["gender", "velocity"]   |

### Import options

|      name      |        type         |               description                |        example         |
| :------------: | :-----------------: | :--------------------------------------: | :--------------------: |
| speakerMapping | map<string, string> | 歌手全局音色名称 => 模块内部嵌入名称映射 | {"zhu": "zhibin-base"} |

### Configuration for `onnx`

|           name            |             type             |                             description                              |                         example                          |
| :-----------------------: | :--------------------------: | :------------------------------------------------------------------: | :------------------------------------------------------: |
|         phonemes          | path | 存储音素名称与音素 ID 对应表的 JSON 文件 | "./assets/phonemes.json" |
|         languages         | path | 存储语言名称与语言 ID 对应表的 JSON 文件 | "./assets/languages.json" |
|         speakers          |      map<string, path>       |               说话人（音色）与说话人嵌入文件路径对应表               |          {"zhibin": "./embeddings/zhibin.emb"}           |
|           model           |             path             |                           声学模型文件路径                           |                "./weights/acoustic.onnx"                 |
|       useLanguageId       |           boolean            |                         是否启用语言 ID 嵌入                         |                           true                           |
|    useSpeakerEmbedding    |           boolean            |                          是否启用说话人嵌入                          |                           true                           |
|        hiddenSize         |           integer            |                    隐层维度（说话人嵌入向量维度）                    |                           256                            |
|        parameters         | array&lt;enum&gt; | 启用的参数列表（energy/breathiness/voicing/tension/mouth_opening/gender/velocity） | ["breathiness", "tension", "gender", "velocity"] |
| useContinuousAcceleration |           boolean            |                         是否使用连续加速采样                         |                           true                           |
|     useVariableDepth      |           boolean            |                         是否使用可变深度采样                         |                           true                           |
|         maxDepth          |            double            |                            允许的最大深度                            |                           0.6                            |
|        sampleRate         |           integer            |                              音频采样率                              |                          44100                           |
|          hopSize          |           integer            |                            梅尔频谱帧跨度                            |                           512                            |
|          winSize          |           integer            |                            梅尔频谱窗大小                            |                           2048                           |
|          fftSize          |           integer            |                          梅尔频谱 FFT 维度                           |                           2048                           |
|        melChannels        |           integer            |                            梅尔频谱通道数                            |                           128                            |
|        melMinFreq         |           integer            |                        梅尔频谱最小频率（Hz）                        |                            40                            |
|        melMaxFreq         |           integer            |                        梅尔频谱最大频率（Hz）                        |                          16000                           |
|          melBase          |             enum             |                         梅尔频谱底数（e/10）                         |                           "e"                            |
|         melScale          |             enum             |                        melScale（slaney/htk）                        |                         "slaney"                         |

### Model variables for `onnx`

|  variable   |  I/O   |  type   |            shape             |     description      |                      activation condition                      |
| :---------: | :----: | :-----: | :--------------------------: | :------------------: | :------------------------------------------------------------: |
|   tokens    | input  |  int64  |        (1, n_tokens)         |       音素 ID        |                               -                                |
|  languages  | input  |  int64  |        (1, n_tokens)         |       语言 ID        |                     useLanguageId == true                      |
|  durations  | input  |  int64  |        (1, n_tokens)         |    音素长度（帧）    |                               -                                |
|     f0      | input  | float32 |        (1, n_frames)         |      基频（Hz）      |                               -                                |
|   energy    | input  | float32 |        (1, n_frames)         |         能量         |                     "energy" in parameters                     |
| breathiness | input  | float32 |        (1, n_frames)         |         气声         |                  "breathiness" in parameters                   |
|   voicing   | input  | float32 |        (1, n_frames)         |         发声         |                    "voicing" in parameters                     |
|   tension   | input  | float32 |        (1, n_frames)         |         张力         |                    "tension" in parameters                     |
| mouth_opening | input | float32 |        (1, n_frames)         |       嘴型开合       |                "mouth_opening" in parameters                 |
|   gender    | input  | float32 |        (1, n_frames)         |       性别偏移       |                     "gender" in parameters                     |
|  velocity   | input  | float32 |        (1, n_frames)         |       发音速度       |                    "velocity" in parameters                    |
|  spk_embed  | input  | float32 | (1, n_frames, `hiddenSize`)  |  说话人（音色）嵌入  |                  useSpeakerEmbedding == true                   |
|    depth    | input  |  int64  |            scalar            | 采样深度（离散加速） | useVariableDepth == true && useContinuousAcceleration == false |
|    depth    | input  | float32 |            scalar            | 采样深度（连续加速） | useVariableDepth == true && useContinuousAcceleration == true  |
|   speedup   | input  |  int64  |            scalar            |        加速比        |               useContinuousAcceleration == false               |
|    steps    | input  |  int64  |            scalar            |       采样步数       |               useContinuousAcceleration == true                |
|     mel     | output | float32 | (1, n_frames, `melChannels`) |       梅尔频谱       |                               -                                |

## `org.openvpi.dsinfer.inference.Vocoder`

### Exports

无

### Import options

无

### Configuration for `onnx`

|       name        |  type   |     description      |         example          |
|:-----------------:|:-------:|:--------------------:|:------------------------:|
|       model       |  path   |      声码器模型文件路径       | "./weights/vocoder.onnx" |
|    sampleRate     | integer |        音频采样率         |          44100           |
|      hopSize      | integer |       梅尔频谱帧跨度        |           512            |
|      winSize      | integer |       梅尔频谱窗大小        |           2048           |
|      fftSize      | integer |      梅尔频谱FFT维度       |           2048           |
|    melChannels    | integer |       梅尔频谱通道数        |           128            |
|    melMinFreq     | integer |     梅尔频谱最小频率（Hz）     |            40            |
|    melMaxFreq     | integer |     梅尔频谱最大频率（Hz）     |          16000           |
|      melBase      |  enum   |     梅尔频谱底数（e/10）     |           "e"            |
|     melScale      |  enum   | melScale（slaney/htk） |         "slaney"         |
| pitchControllable | boolean |   是否支持和声学模型不同的音高输入   |           true           |

### Model variables for `onnx`

| variable |  I/O   |  type   |            shape             | description | activation condition |
| :------: | :----: | :-----: | :--------------------------: | :---------: | :------------------: |
|   mel    | input  | float32 | (1, n_frames, `melChannels`) |  梅尔频谱   |          -           |
|    f0    | input  | float32 |        (1, n_frames)         | 基频（Hz）  |          -           |
| waveform | output | float32 |        (1, n_samples)        |    波形     |          -           |

## `org.openvpi.dsinfer.singer.DiffSinger`

### Configuration for `openvpi`

|       name        |         type         | 必填 |                  description                   |          example           |
| :---------------: | :------------------: | :--: | :--------------------------------------------- | :------------------------- |
|      `dict`       |        `path`        |  是  | 歌手发音词典。本 variant 不读取该文件，仅解析路径后提供给宿主 | `"../../assets/dict.txt"`  |

保留音素（`reservedPhonemes`，即可以在歌词中直接书写的音素记号）不属于本 variant 的 configuration，而是 `singer` 类别的追加字段，位于声明的根 object，由 synthrt 读取（见 spec 2.4「Singer 模块」一节的「声明文件」）。

**保留音素是不经音素转换、直接送入模型的记号。** 歌手模型的音素表包含两类记号：绝大多数由歌词经音素转换得到，另有少数不代表言语，例如换气、喉塞、哼鸣，这类记号由用户在歌词位置直接书写后进入模型。哪些记号属于后一类由歌手决定，因此由歌手声明，而不依赖带外约定。宿主应当**将保留音素完全排除在音素转换路径之外**，因为保留音素不是词语，不能交给语言进行转换。

**加载时按模块逐一校验。** 本 variant 在导入准备完成后，要求每个保留音素都出现在**所有已导入且带音素表的模块**（Duration、Pitch、Variance、Acoustic）的 `phonemes` 中。任一模块缺少任一保留音素时，整个 Package 加载失败，错误信息列出每个模块缺少的记号。该校验是声明保留音素的主要目的：**模型音素表中不存在的记号在合成结果中表现为静音，而不是预期的标记**。保留音素绕开了音素转换路径，因此此处是唯一的校验点，下游任何环节都无法区分这两种情况。

四个模块**分别**校验，而不是只校验 Acoustic。四个音素表是相互独立的文件，若四者不一致，保留音素可能在时长预测中有效，而在声学合成中无效。
